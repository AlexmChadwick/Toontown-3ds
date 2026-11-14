#!/usr/bin/env python3
"""Split long ground triangles inside an existing TTM1 file.

Textures and the position scale/offset stay as they were, so unsplit vertices
requantize to the same int16 values. Used for the RomFS playground mesh, which
is one resident model and is not produced by bake_street.py. Collision files
are not touched.
"""
import struct
import sys
from pathlib import Path

import numpy as np

import ground_split
import ttm

HEADER = struct.Struct('<4sHHHHII3f3fIIIII')
TEX = struct.Struct('<HHBBBBII32s')
BATCH = struct.Struct('<HBBBBBBIIII')
VERT = np.dtype([('p', '<i2', 4), ('uv', '<i2', 2), ('c', 'u1', 4)])


def _world(q, scale, offset):
    return q.astype(np.float64) * scale + offset


def _quantize(world, scale, offset):
    q = np.rint((world - offset) / scale)
    return np.clip(q, -32767, 32767).astype(np.int16)


def rewrite(path, out_path=None, max_edge=None, tri_limit=15000):
    """Split long ground edges. With tri_limit set and no explicit max_edge, raise
    the cap from 20 until the file has at most tri_limit triangles. The search
    always rereads `path`, so it can loosen a split; it cannot merge one.
    """
    if tri_limit is not None and max_edge is None:
        edge = ground_split.MAX_EDGE
        while True:
            report = rewrite(path, out_path, max_edge=edge, tri_limit=None)
            if report['triangles'] <= tri_limit:
                return report
            edge += 2.0
            if edge > 64.0:
                raise ValueError(
                    f'{path}: {report["triangles"]} triangles at edge {edge - 2:g}, over {tri_limit}')
    if max_edge is None:
        max_edge = ground_split.MAX_EDGE
    data = Path(path).read_bytes()
    h = HEADER.unpack_from(data)
    if h[0] != ttm.MAGIC or h[1] != ttm.VERSION:
        raise SystemExit(f'{path}: not a TTM1 v1 file')
    ntex, nbatch, nvert, nidx = h[3:7]
    scale = np.array(h[7:10], dtype=np.float64)
    offset = np.array(h[10:13], dtype=np.float64)
    tex_off, batch_off, vert_off, index_off = h[13:17]
    textures = [TEX.unpack_from(data, tex_off + i * TEX.size) for i in range(ntex)]
    tex_payloads = [bytes(data[t[6]:t[6] + t[7]]) for t in textures]
    verts = np.frombuffer(data, dtype=VERT, count=nvert, offset=vert_off).copy()
    indices = np.frombuffer(data, dtype='<u2', count=nidx, offset=index_off).copy()

    new_vert_chunks = []
    new_idx_chunks = []
    new_batches = []
    stats = {'triangles_in': 0, 'triangles_out': 0, 'split': 0}
    max_ground = 0.0
    for i in range(nbatch):
        tex, blend, flags, tint, layer, slot, pad, first_v, num_v, first_i, num_i = BATCH.unpack_from(
            data, batch_off + i * BATCH.size)
        batch_v = verts[first_v:first_v + num_v]
        batch_i = indices[first_i:first_i + num_i].astype(np.int64)
        world = _world(batch_v['p'][:, :3], scale, offset)
        uv = batch_v['uv'].astype(np.float64) / ttm.UV_SCALE
        rgba = batch_v['c'].astype(np.float64) / 255.0
        flat = np.concatenate([world, uv, rgba], axis=1)
        pieces = []
        split_any = False
        for tri_ix in batch_i.reshape(-1, 3):
            stats['triangles_in'] += 1
            tri = flat[tri_ix]
            longest = float(np.max(ground_split._edges(tri)))
            if ground_split._up(tri) >= ground_split.MIN_UP:
                max_ground = max(max_ground, longest)
            if ground_split._up(tri) >= ground_split.MIN_UP and longest > max_edge:
                parts = ground_split.split_triangle(tri, max_edge)
                stats['split'] += 1
                split_any = True
            else:
                parts = [tri]
            pieces.extend(parts)
        stats['triangles_out'] += len(pieces)
        block = np.concatenate(pieces, axis=0) if pieces else np.zeros((0, 9))
        # Unsplit batches keep their original bytes, including shared vertices.
        if not split_any:
            new_vert_chunks.append(batch_v.tobytes())
            new_idx_chunks.append(indices[first_i:first_i + num_i].tobytes())
            new_batches.append((tex, blend, flags, tint, layer, slot, pad, num_v, num_i))
            continue
        qpos = _quantize(block[:, :3], scale, offset)
        quv = np.clip(np.rint(block[:, 3:5] * ttm.UV_SCALE), -32768, 32767).astype(np.int16)
        qc = np.clip(np.rint(block[:, 5:9] * 255.0), 0, 255).astype(np.uint8)
        rec = np.zeros(len(block), dtype=VERT)
        rec['p'][:, :3] = qpos
        rec['uv'] = quv
        rec['c'] = qc
        # A batch can grow past the u16 vertex range; split it the way ttm.write does.
        for start in range(0, len(rec), ttm.MAX_BATCH_VERTS):
            part = rec[start:start + ttm.MAX_BATCH_VERTS]
            new_vert_chunks.append(part.tobytes())
            new_idx_chunks.append(np.arange(len(part), dtype='<u2').tobytes())
            new_batches.append((tex, blend, flags, tint, layer, slot, pad, len(part), len(part)))

    vert_blob = b''.join(new_vert_chunks)
    idx_blob = b''.join(new_idx_chunks)
    out_verts = np.frombuffer(vert_blob, dtype=VERT).copy()
    out_idx = np.frombuffer(idx_blob, dtype='<u2').copy()
    after = 0.0
    cursor_v = 0
    cursor_i = 0
    for entry in new_batches:
        num_v, num_i = entry[7], entry[8]
        q = out_verts['p'][cursor_v:cursor_v + num_v][:, :3]
        world = _world(q, scale, offset)
        ii = out_idx[cursor_i:cursor_i + num_i].astype(np.int64)
        for tri_ix in ii.reshape(-1, 3):
            tri = np.concatenate([world[tri_ix], np.zeros((3, 6))], axis=1)
            if ground_split._up(tri) >= ground_split.MIN_UP:
                after = max(after, float(np.max(ground_split._edges(tri))))
        cursor_v += num_v
        cursor_i += num_i

    header_size = 64
    batch_table = (header_size + TEX.size * ntex + 3) & ~3
    vertex_at = ttm._align(batch_table + BATCH.size * len(new_batches))
    index_at = ttm._align(vertex_at + len(vert_blob))
    cursor = ttm._align(index_at + len(idx_blob))
    tex_at = []
    for payload in tex_payloads:
        tex_at.append(cursor)
        cursor = ttm._align(cursor + len(payload))
    out = bytearray(cursor)
    HEADER.pack_into(out, 0, ttm.MAGIC, ttm.VERSION, 0, ntex, len(new_batches),
                     len(out_verts), len(out_idx), *scale.astype(np.float32), *offset.astype(np.float32),
                     header_size, batch_table, vertex_at, index_at, 0)
    out[20:44] = data[20:44]  # keep the original scale and offset bits
    for i, t in enumerate(textures):
        TEX.pack_into(out, header_size + i * TEX.size, t[0], t[1], t[2], t[3], t[4], t[5],
                      tex_at[i], len(tex_payloads[i]), t[8])
    vpos = 0
    ipos = 0
    for i, entry in enumerate(new_batches):
        tex, blend, flags, tint, layer, slot, pad, num_v, num_i = entry
        BATCH.pack_into(out, batch_table + i * BATCH.size, tex, blend, flags, tint, layer, slot, pad,
                        vpos, num_v, ipos, num_i)
        vpos += num_v
        ipos += num_i
    out[vertex_at:vertex_at + len(vert_blob)] = vert_blob
    out[index_at:index_at + len(idx_blob)] = idx_blob
    for at, payload in zip(tex_at, tex_payloads):
        out[at:at + len(payload)] = payload
    dest = Path(out_path or path)
    dest.write_bytes(out)
    same_tex = tex_payloads == [bytes(out[a:a + len(p)]) for a, p in zip(tex_at, tex_payloads)]
    report = {
        'file_bytes': len(out), 'vertices': len(out_verts), 'triangles': len(out_idx) // 3,
        'batches': len(new_batches), 'textures': ntex,
        'ground_split': stats, 'max_edge': max_edge,
        'max_ground_edge_before': max_ground, 'max_ground_edge_after': after,
        'textures_unchanged': same_tex,
    }
    return report


def main():
    import json
    if len(sys.argv) != 2:
        raise SystemExit('usage: rewrite_ground_ttm.py <file.ttm>')
    print(json.dumps(rewrite(sys.argv[1]), indent=2))


if __name__ == '__main__':
    main()
