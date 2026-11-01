"""TZ v1/v2, self-contained little-endian chunked Toontown zone container.

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
Runtime reads embedded blobs directly through Model/CollisionWorld.loadFrom.

Version 2 retains the 64-byte header and all v1 sections except chunk entries:
 each is 104 bytes: the complete 80-byte v1 entry followed by <6I> low-model
 offset, size, geometry bytes, texture bytes, draws, triangles. A zero low blob
 represents an empty far LOD. PVS/floors/triggers remain unchanged. The low set
 must fit 15000 triangles/150 draws/6 MiB geometry/8 MiB textures for EVERY cell.
 Runtime starts with all low blobs and upgrades nearest chunks within those caps.
 The writer can still emit v1 with version=1; readers accept both versions.

Version 3 is v2 plus a shared texture pool: the header's two reserved words become
pool offset and size. Every distinct texture payload (byte-identical mip chain) of all
near and far TTMs is stored once, 128-byte aligned, after the last model blob. Each
embedded TTM keeps its complete texture table, but its blob ends after the index data
and each entry's dataOffset (still relative to its own blob, as Model::loadFrom rebases
it) points forward into the pool. Atlas pages shared by many chunks are therefore stored
once on SD; residency budgets are unchanged (each loaded chunk still holds its copy).
model_bytes() rebuilds the standalone TTM1 for host tools.
"""
import math
import struct
from pathlib import Path

HEADER = struct.Struct('<4sHH14I')
CHUNK = struct.Struct('<9I6f2I12s')
LOW = struct.Struct('<6I')
CHUNK_V2_SIZE = 104
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


TTM_HEADER = struct.Struct('<4sHHHHII6f5I')
TTM_TEX = struct.Struct('<HHBBBBII32s')


def _pool_models(models):
    """Strip texture payloads from TTM blobs. Returns (stripped blobs, per-blob list of
    (entry offset, pool index), pool payloads in first-use order)."""
    pool, index, out = [], {}, []
    for blob in models:
        if not blob:
            out.append((b'', []))
            continue
        h = TTM_HEADER.unpack_from(blob)
        nt, tt = h[3], h[13]
        refs, end = [], len(blob)
        for i in range(nt):
            e = TTM_TEX.unpack_from(blob, tt + i*48)
            payload = bytes(blob[e[6]:e[6]+e[7]])
            end = min(end, e[6])
            if payload not in index:
                index[payload] = len(pool)
                pool.append(payload)
            refs.append((tt + i*48, index[payload]))
        out.append((bytes(blob[:align(end)]) if nt else bytes(blob), refs))
    return out, pool


def model_bytes(data, off, size):
    """Standalone TTM1 for an embedded blob (v3 pooled textures copied back in)."""
    blob = bytearray(data[off:off+size])
    if not size:
        return bytes(blob)
    h = TTM_HEADER.unpack_from(blob)
    nt, tt = h[3], h[13]
    if all(TTM_TEX.unpack_from(blob, tt+i*48)[6] + TTM_TEX.unpack_from(blob, tt+i*48)[7] <= size
           for i in range(nt)):
        return bytes(blob)
    for i in range(nt):
        e = list(TTM_TEX.unpack_from(blob, tt + i*48))
        payload = data[off+e[6]:off+e[6]+e[7]]
        if len(payload) != e[7]:
            raise ValueError('pooled texture outside the file')
        e[6] = len(blob)
        TTM_TEX.pack_into(blob, tt + i*48, *e)
        blob += payload + bytes(align(len(blob) + len(payload)) - len(blob) - len(payload))
    return bytes(blob)


def write(path, branch, chunks, floors, triggers, collision, version=3):
    if version not in (1,2,3):
        raise ValueError("unsupported TZ version")
    stride=80 if version==1 else CHUNK_V2_SIZE
    models = [c['model'] for c in chunks]
    lows = [c['low_model'] for c in chunks] if version>=2 else []
    if version==3:
        stripped, pool = _pool_models([m for pair in zip(models, lows) for m in pair])
        models, lows = [m for m, _ in stripped[0::2]], [m for m, _ in stripped[1::2]]
        refs = [r for _, r in stripped]
    vis = [i for c in chunks for i in c['vis']]
    co = 128
    vo = align(co + stride * len(chunks))
    fo = align(vo + 2 * len(vis))
    to = align(fo + FLOOR.size * len(floors))
    clo = align(to + TRIGGER.size * len(triggers))
    cursor = align(clo + len(collision))
    offsets, lows_at = [], []
    for i, c in enumerate(chunks):
        offsets.append(cursor if models[i] else 0)
        cursor = align(cursor + len(models[i]))
        if version>=2:
            lows_at.append(cursor if lows[i] else 0)
            cursor = align(cursor + len(lows[i]))
    pool_off = pool_size = 0
    if version==3 and pool:
        pool_off, pool_at = cursor, []
        for payload in pool:
            pool_at.append(cursor)
            cursor = align(cursor + len(payload))
        pool_size = cursor - pool_off
    out = bytearray(cursor)
    HEADER.pack_into(out, 0, b'TZ01', version, 0, branch, len(chunks), len(vis), len(floors), len(triggers),
                     co, vo, fo, to, clo, len(collision), len(out), pool_off, pool_size)
    if version==3:
        for payload, at in zip(pool, pool_at if pool else []):
            out[at:at+len(payload)] = payload
        blob_at = [a for pair in zip(offsets, lows_at) for a in pair]
        blobs = [m for pair in zip(models, lows) for m in pair]
        for base, blob, rr in zip(blob_at, blobs, refs):
            blob = bytearray(blob)
            for entry, k in rr:
                e = list(TTM_TEX.unpack_from(blob, entry))
                e[6] = pool_at[k] - base
                TTM_TEX.pack_into(blob, entry, *e)
            out[base:base+len(blob)] = blob
    first = 0
    for i, c in enumerate(chunks):
        s = c['stats']
        own = [j for j, f in enumerate(floors) if f[1] == i]
        CHUNK.pack_into(out, co+i*stride, c['zone'], first, len(c['vis']), offsets[i], len(models[i]),
                        s['geometry_bytes'], s['texture_bytes'], s['batches'], s['triangles'],
                        *s['bounds'][0], *s['bounds'][1], own[0] if own else 0, len(own), cstr(c['name'], 12))
        if version>=2:
            ls=c['low_stats']
            LOW.pack_into(out,co+i*stride+80,lows_at[i],len(lows[i]),ls['geometry_bytes'],ls['texture_bytes'],ls['batches'],ls['triangles'])
            if version==2:
                out[lows_at[i]:lows_at[i]+len(lows[i])]=lows[i]
        first += len(c['vis'])
        if version!=3:
            out[offsets[i]:offsets[i]+len(models[i])] = models[i]
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
    check(h[0]==b'TZ01' and h[1] in (1,2,3) and h[2]==0, 'bad magic/version/flags')
    version=h[1]
    stride=80 if version==1 else CHUNK_V2_SIZE
    branch, nc, nv, nf, nt, co, vo, fo, to, clo, cls, size, r0, r1 = h[3:]
    check(size == len(data) and (version==3 or not (r0 or r1)) and 0 < nc <= 256 and 0 < branch <= 65535, 'bad size/count')
    def section(off, length):
        check(off >= 64 and off % 128 == 0 and off <= size and length <= size-off, 'invalid section')
    for off, n in ((co,nc*stride),(vo,nv*2),(fo,nf*40),(to,nt*156),(clo,cls)):
        section(off,n)
    if r1:
        section(r0,r1)
    intervals = [(co,co+nc*stride),(vo,vo+nv*2),(fo,fo+nf*40),(to,to+nt*156),(clo,clo+cls),(r0,r0+r1)]
    def blob(off,length,*stats):
        if version==3:  # pooled payloads must lie inside the pool (or the blob itself)
            th=TTM_HEADER.unpack_from(data,off)
            for k in range(th[3]):
                e=TTM_TEX.unpack_from(data,off+th[13]+k*48)
                at=off+e[6]
                check(e[6]+e[7]<=length or (r1 and r0<=at and at+e[7]<=r0+r1), 'texture outside blob/pool')
        validate_ttm(model_bytes(data,off,length),*stats)
    vis = list(struct.unpack_from(f'<{nv}H',data,vo))
    floors = [FLOOR.unpack_from(data, fo+i*40) for i in range(nf)]
    chunks = []
    for i in range(nc):
        c = CHUNK.unpack_from(data, co+i*stride)
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
            blob(off,length,geom,tex,draws,tris)
        else:
            check(not (off or geom or tex or draws or tris), 'nonempty metrics for empty chunk')
        low = LOW.unpack_from(data,co+i*stride+80) if version>=2 else (off,length,geom,tex,draws,tris)
        if version>=2:
            if low[1]:
                section(low[0],low[1])
                intervals.append((low[0],low[0]+low[1]))
                blob(low[0],low[1],*low[2:])
            else:
                check(not any(low), 'invalid empty low model')
        chunks.append({'zone':zone,'vis':ids,'name':c[17].split(b'\0')[0].decode(),
                       'offset':off,'size':length,'low_offset':low[0],'low_size':low[1],
                       'low_stats':dict(zip(('geometry_bytes','texture_bytes','batches','triangles'),low[2:])), 'bounds':(c[9:12],c[12:15]),
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
        stat='low_stats' if version>=2 else 'stats'
        if version>=2:
            check(sum(chunks[j][stat]['triangles'] for j in c['vis'])<=15000, 'triangle budget exceeded')
        for key, budget in (('geometry_bytes',GEOM_BUDGET),('texture_bytes',TEX_BUDGET),('batches',DRAW_BUDGET)):
            check(sum(chunks[j][stat][key] for j in c['vis']) <= budget, f'{c["name"]} exceeds {key} budget')
    validate_collision(data[clo:clo+cls])
    return {'version':version,'branch':branch,'chunks':chunks,'floors':floors,'triggers':triggers,'collision':data[clo:clo+cls]}


def validate_ttm(data, geom, tex, draws, tris):
    """Validate the offsets/counts that the existing permissive Model loader trusts."""
    def check(ok):
        if not ok:
            raise ValueError('invalid embedded TTM')
    check(len(data)>=64)
    h = struct.unpack_from('<4sHHHHII6f5I',data)
    check(h[0] == b'TTM1' and h[1] == 1 and h[2] in (0, 1))
    absolute = h[2] == 1
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
        check(w>=8 and hh>=8 and w<=512 and hh<=512 and w&(w-1)==0 and hh&(hh-1)==0)
        check(fmt in (0,2,3,4,5,7,8,9,12,13) and 1<=mips<=7 and ws in (0,2) and wt in (0,2))
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
        if absolute:
            check(all(fv <= v < fv + nverts for v in ids))
        else:
            check(all(v < nverts for v in ids))


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


def repack(path, version=3):
    """Rewrite an existing .tz in another version (same chunks, floors, triggers, collision)."""
    data = Path(path).read_bytes()
    zone = validate(data)
    floors = [([f[0:3], f[3:6], f[6:9]], f[9], f[10]) for f in zone['floors']]
    triggers = [{'min': t[:3], 'max': t[3:6], 'kind': t[6], 'name': t[7].split(b'\0')[0].decode(),
                 'owner': t[8].split(b'\0')[0].decode()} for t in zone['triggers']]
    chunks = [dict(c, stats=dict(c['stats'], bounds=c['bounds']),
                   model=model_bytes(data, c['offset'], c['size']),
                   low_model=model_bytes(data, c['low_offset'], c['low_size']) if zone['version'] >= 2 else b'',
                   low_stats=c['low_stats'] if zone['version'] >= 2 else dict(c['stats']))
              for c in zone['chunks']]
    write(path, zone['branch'], chunks, floors, triggers, zone['collision'], version=version)
    return len(data), Path(path).stat().st_size


if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser(description='Repack baked .tz files (default: to v3, shared textures).')
    ap.add_argument('files', nargs='+')
    ap.add_argument('--version', type=int, default=3, choices=(1, 2, 3))
    args = ap.parse_args()
    for f in args.files:
        before, after = repack(f, args.version)
        print(f'{f}: {before} -> {after} bytes')
