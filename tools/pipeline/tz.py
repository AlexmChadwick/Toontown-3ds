"""TZ v1, self-contained little-endian chunked Toontown zone container.

Header (64): <4sHH14I: magic TZ01, version=1, flags=0; branch zone,
chunk count, vis-index count, floor count, trigger count, chunk offset, vis offset,
floor offset, trigger offset, collision offset, collision bytes, file bytes, 0, 0.
Chunk (80): <9I6f2I12s: server zone, first vis index, vis count, embedded TTM
 offset/bytes, geometry bytes (16*vertices+2*indices), texture bytes (all mips),
 draws, triangles, min[3], max[3], first floor, floor count, name (NUL terminated).
Vis stream: u16 chunk indices (self included, sorted, unique). Server interests
are branch + the referenced chunks' zone ids, deduplicated.
Floor (40): <9fHH: world triangle vertices, owning chunk index, server zone id.
Trigger (156): <6fI64s64s: world min/max, kind (1 door, 2 tunnel, 0 other),
 NUL-terminated original name and DNA owner. AABBs are broad-phase notifications.
Collision: embedded unmodified TTC1 v1, for the existing CollisionWorld.
Models: one unmodified TTM1 v1 per nonempty chunk, with only textures it uses.
All sections/blobs start 128-byte aligned except fixed table entries. TTM offsets
are relative to their own blob, all TZ offsets are absolute. Empty chunks have
model offset/size and draw/memory counts zero. The full collision and floor tables
stay on the regular heap; only selected TTM blobs reside in linear memory.

Atlases are shared during baking but copied into each standalone TTM. Residency
budgets count these copies. This allows the unchanged Model API to draw chunks.
Synchronous runtime materialization to a caller-supplied SD scratch file bridges
Model::load(path); see streets.md for the proposed offset-loader improvement.
"""
import math
import struct
from pathlib import Path

HEADER = struct.Struct('<4sHH14I')
CHUNK = struct.Struct('<9I6f2I12s')
FLOOR = struct.Struct('<9fHH')
TRIGGER = struct.Struct('<6fI64s64s')
GEOM_BUDGET, TEX_BUDGET, DRAW_BUDGET = 6*1024*1024, 8*1024*1024, 150


def align(n):
    return (n + 127) & ~127


def cstr(s, n):
    b = s.encode('ascii', 'replace')
    if len(b) >= n:
        raise ValueError(f'name too long for {n} bytes: {s}')
    return b.ljust(n, b'\0')


def write(path, branch, chunks, floors, triggers, collision):
    vis = [i for c in chunks for i in c['vis']]
    co = 128
    vo = align(co + CHUNK.size * len(chunks))
    fo = align(vo + 2 * len(vis))
    to = align(fo + FLOOR.size * len(floors))
    clo = align(to + TRIGGER.size * len(triggers))
    cursor = align(clo + len(collision))
    for c in chunks:
        c['offset'] = cursor if c['model'] else 0
        cursor = align(cursor + len(c['model']))
    out = bytearray(cursor)
    HEADER.pack_into(out, 0, b'TZ01', 1, 0, branch, len(chunks), len(vis), len(floors), len(triggers),
                     co, vo, fo, to, clo, len(collision), len(out), 0, 0)
    first = 0
    for i, c in enumerate(chunks):
        s = c['stats']
        own = [j for j, f in enumerate(floors) if f[1] == i]
        CHUNK.pack_into(out, co+i*80, c['zone'], first, len(c['vis']), c['offset'], len(c['model']),
                        s['geometry_bytes'], s['texture_bytes'], s['batches'], s['triangles'],
                        *s['bounds'][0], *s['bounds'][1], own[0] if own else 0, len(own), cstr(c['name'], 12))
        first += len(c['vis'])
        out[c['offset']:c['offset']+len(c['model'])] = c['model']
    if vis:
        struct.pack_into(f'<{len(vis)}H', out, vo, *vis)
    for i, (verts, cell, zone) in enumerate(floors):
        FLOOR.pack_into(out, fo+i*40, *[float(v) for p in verts for v in p], cell, zone)
    for i, t in enumerate(triggers):
        TRIGGER.pack_into(out, to+i*156, *t['min'], *t['max'], t['kind'], cstr(t['name'], 64), cstr(t['owner'], 64))
    out[clo:clo+len(collision)] = collision
    validate(bytes(out))
    Path(path).write_bytes(out)
    return report(chunks)


def report(chunks):
    rows = []
    for i, c in enumerate(chunks):
        row = {'cell': c['name'], 'visible_cells': len(c['vis'])}
        for key in ('batches', 'triangles', 'geometry_bytes', 'texture_bytes'):
            row[key] = sum(chunks[j]['stats'][key] for j in c['vis'])
        rows.append(row)
    return {'chunks': len(chunks), 'whole': {k: sum(c['stats'][k] for c in chunks)
            for k in ('batches', 'triangles', 'geometry_bytes', 'texture_bytes')},
            'worst': {k: max((r[k] for r in rows), default=0) for k in
                      ('batches', 'triangles', 'geometry_bytes', 'texture_bytes')}, 'visible_sets': rows}


def validate(data):
    """Strict host reader, also used by previews/tests (raises ValueError)."""
    def check(ok, msg):
        if not ok:
            raise ValueError(msg)
    check(len(data) >= 64, 'short TZ header')
    h = HEADER.unpack_from(data)
    check(h[:4] == (b'TZ01', 1, 0, h[3]), 'bad magic/version/flags')
    branch, nc, nv, nf, nt, co, vo, fo, to, clo, cls, size, r0, r1 = h[3:]
    check(size == len(data) and not (r0 or r1) and 0 < nc <= 256 and 0 < branch <= 65535, 'bad size/count')
    def section(off, length):
        check(off >= 64 and off % 128 == 0 and off <= size and length <= size-off, 'invalid section')
    for off, n in ((co,nc*80),(vo,nv*2),(fo,nf*40),(to,nt*156),(clo,cls)):
        section(off,n)
    intervals = [(co,co+nc*80),(vo,vo+nv*2),(fo,fo+nf*40),(to,to+nt*156),(clo,clo+cls)]
    vis = list(struct.unpack_from(f'<{nv}H',data,vo))
    floors = [FLOOR.unpack_from(data, fo+i*40) for i in range(nf)]
    chunks = []
    for i in range(nc):
        c = CHUNK.unpack_from(data, co+i*80)
        zone, first, count, off, length, geom, tex, draws, tris = c[:9]
        check(first <= nv and count <= nv-first, 'bad vis range')
        ids = vis[first:first+count]
        check(ids == sorted(set(ids)) and i in ids and all(j < nc for j in ids), 'bad vis indices')
        check(all(math.isfinite(x) for x in c[9:15]) and all(c[9+k]<=c[12+k] for k in range(3)), 'bad bounds')
        check(0 < zone <= 65535 and b'\0' in c[17], 'bad zone/name')
        check(c[15] <= nf and c[16] <= nf-c[15], 'bad floor range')
        check(all(f[9] == i and f[10] == zone for f in floors[c[15]:c[15]+c[16]]), 'floor owner mismatch')
        if length:
            section(off,length)
            intervals.append((off,off+length))
            validate_ttm(data[off:off+length], geom,tex,draws,tris)
        else:
            check(not (off or geom or tex or draws or tris), 'nonempty metrics for empty chunk')
        chunks.append({'zone':zone,'vis':ids,'name':c[17].split(b'\0')[0].decode(),
                       'offset':off,'size':length,'bounds':(c[9:12],c[12:15]),
                       'stats':dict(zip(('geometry_bytes','texture_bytes','batches','triangles'),c[5:9]))})
    intervals = sorted((a,b) for a,b in intervals if b>a)
    check(all(b<=c for (_,b),(c,_) in zip(intervals,intervals[1:])), 'overlapping sections')
    for f in floors:
        check(all(math.isfinite(v) for v in f[:9]) and f[9] < nc and f[10] == chunks[f[9]]['zone'], 'invalid floor')
    triggers = [TRIGGER.unpack_from(data,to+i*156) for i in range(nt)]
    for t in triggers:
        check(all(math.isfinite(x) for x in t[:6]) and all(t[k]<=t[k+3] for k in range(3))
              and t[6] <= 2 and b'\0' in t[7] and b'\0' in t[8], 'invalid trigger')
    for c in chunks:
        for key, budget in (('geometry_bytes',GEOM_BUDGET),('texture_bytes',TEX_BUDGET),('batches',DRAW_BUDGET)):
            check(sum(chunks[j]['stats'][key] for j in c['vis']) <= budget, f'{c["name"]} exceeds {key} budget')
    validate_collision(data[clo:clo+cls])
    return {'branch':branch,'chunks':chunks,'floors':floors,'triggers':triggers,'collision':data[clo:clo+cls]}


def validate_ttm(data, geom, tex, draws, tris):
    """Validate the offsets/counts that the existing permissive Model loader trusts."""
    def check(ok):
        if not ok:
            raise ValueError('invalid embedded TTM')
    check(len(data)>=64)
    h = struct.unpack_from('<4sHHHHII6f5I',data)
    check(h[0:3] == (b'TTM1',1,0))
    nt, nb, nv, ni = h[3:7]
    tt, bt, vb, ib = h[13:17]
    check(nb==draws and ni//3==tris and ni%3==0 and nv*16+ni*2==geom)
    check(all(math.isfinite(v) for v in h[7:13]) and all(v>0 for v in h[7:10]))
    def span(off,n):
        check(off>=64 and off<=len(data) and n<=len(data)-off)
    for off,n in ((tt,nt*48),(bt,nb*24),(vb,nv*16),(ib,ni*2)):
        span(off,n)
    total=0
    for i in range(nt):
        w,hh,fmt,mips,ws,wt,off,n,_ = struct.unpack_from('<HHBBBBII32s',data,tt+i*48)
        check(w>=8 and hh>=8 and w<=256 and hh<=256 and w&(w-1)==0 and hh&(hh-1)==0)
        check(fmt in (0,2,3,4,5,7,8,9,12,13) and 1<=mips<=6 and ws in (0,2) and wt in (0,2))
        bpp={0:32,2:16,3:16,4:16,5:16,7:8,8:8,9:8,12:4,13:8}[fmt]
        expected=0
        for level in range(mips):
            check((w>>level)>=8 and (hh>>level)>=8)
            expected+=(w>>level)*(hh>>level)*bpp//8
        check(expected==n)
        span(off,n)
        total+=n
    check(total==tex)
    for i in range(nb):
        b=struct.unpack_from('<H6B4I',data,bt+i*24)
        tid,blend,flags,tint,layer,slot,pad,fv,nverts,fi,nidx=b
        check((tid==65535 or tid<nt) and blend<=2 and flags<=1 and tint==0 and layer<=4 and slot==0 and pad==0)
        check(fv<=nv and nverts<=nv-fv and fi<=ni and nidx<=ni-fi and nidx%3==0)
        ids=struct.unpack_from(f'<{nidx}H',data,ib+fi*2)
        check(all(v<nverts for v in ids))


def cell_at(zone,x,y,z,reach=4):
    best,cell=-float('inf'),None
    for f in zone['floors']:
        a,b,c=[f[i:i+3] for i in (0,3,6)]
        den=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
        if abs(den)<1e-8:
            continue
        u=((b[1]-c[1])*(x-c[0])+(c[0]-b[0])*(y-c[1]))/den
        v=((c[1]-a[1])*(x-c[0])+(a[0]-c[0])*(y-c[1]))/den
        height=u*a[2]+v*b[2]+(1-u-v)*c[2]
        if u>=-1e-5 and v>=-1e-5 and u+v<=1.00001 and height<=z+reach and height>best:
            best,cell=height,f[9]
    return cell


def validate_collision(data):
    """Check TTC sections and cell/trigger indices before runtime queries trust them."""
    def check(ok):
        if not ok:
            raise ValueError('invalid embedded TTC')
    check(len(data)>=96)
    h=struct.unpack_from('<4sHH3fHH4I6f8I',data)
    check(h[:3]==(b'TTC1',1,0) and h[25]==len(data) and all(math.isfinite(v) for v in h[3:6]) and h[5]>0)
    gw,gh,nt,nc,np,ng=h[6:12]
    check(gw>0 and gh>0 and nt<=65535 and nc<=65535)
    check(all(math.isfinite(v) for v in h[12:18]))
    tri,cap,plane,table,cell,ncell,trig,_=h[18:26]
    for off,count,stride in ((tri,nt,56),(cap,nc,32),(plane,np,20),(table,gw*gh,4),(cell,ncell,2),(trig,ng,100)):
        check(off>=96 and off%4==0 and off<=len(data) and count<=(len(data)-off)//stride)
    for (start,) in struct.iter_unpack('<I',data[table:table+gw*gh*4]):
        check(start<=ncell and ncell-start>=6)
        counts=struct.unpack_from('<6H',data,cell+start*2)
        check(sum(counts)<=ncell-start-6)
        cur=cell+(start+6)*2
        for i,count in enumerate(counts):
            ids=struct.unpack_from(f'<{count}H',data,cur)
            check(all(x<(nc if i%2 else nt) for x in ids))
            cur+=count*2
    for i in range(ng):
        kind,_,count,first=struct.unpack_from('<BBHI',data,trig+i*100)
        check(kind<=1 and first<=(nt if kind else nc) and count<=(nt if kind else nc)-first)
