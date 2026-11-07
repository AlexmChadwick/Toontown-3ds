#!/usr/bin/env python3
"""Time the old per-texture seek path against one read of a baked .ttm.

The 3DS loader used to fseek+fread once per texture. Textures are already
PICA-tiled at bake time, so the runtime copy is one fread of the vertex, index
and texture span. This script measures that difference on a host file. It does
not boot a 3DS.

    python3 -I tools/dev/profile_load.py romfs/baked/ttc_sz.ttm
"""
import os
import struct
import sys
import time

sys.dont_write_bytecode = True


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else 'romfs/baked/ttc_sz.ttm'
    data = open(path, 'rb').read()
    magic, ver, flags, ntex, nbatch, nv, ni = struct.unpack_from('<4sHHHHII', data)
    if magic != b'TTM1':
        raise SystemExit(f'{path}: not a TTM')
    tex_off, batch_off, v_off, i_off = struct.unpack_from('<IIII', data, 44)
    tex_bytes = 0
    seeks = []
    for i in range(ntex):
        _w, _h, _fmt, _mips, _ws, _wt, off, size, _name = struct.unpack_from(
            '<HHBBBBII32s', data, tex_off + 48 * i)
        tex_bytes += size
        seeks.append((off, size))
    geom = nv * 16 + ni * 2
    span_lo = min([v_off, i_off] + [off for off, _ in seeks])
    span_hi = max([v_off + nv * 16, i_off + ni * 2] + [off + size for off, size in seeks])

    def time_seeks(repeats=30):
        fd = os.open(path, os.O_RDONLY)
        try:
            t0 = time.perf_counter()
            total = 0
            for _ in range(repeats):
                os.lseek(fd, 0, os.SEEK_SET)
                total += len(os.read(fd, 64))
                os.lseek(fd, batch_off, os.SEEK_SET)
                total += len(os.read(fd, 24 * nbatch))
                os.lseek(fd, tex_off, os.SEEK_SET)
                total += len(os.read(fd, 48 * ntex))
                os.lseek(fd, v_off, os.SEEK_SET)
                total += len(os.read(fd, nv * 16))
                os.lseek(fd, i_off, os.SEEK_SET)
                total += len(os.read(fd, ni * 2))
                for off, size in seeks:
                    os.lseek(fd, off, os.SEEK_SET)
                    total += len(os.read(fd, size))
            return (time.perf_counter() - t0) / repeats * 1e6, total // repeats
        finally:
            os.close(fd)

    def time_one(repeats=30):
        fd = os.open(path, os.O_RDONLY)
        try:
            t0 = time.perf_counter()
            total = 0
            for _ in range(repeats):
                os.lseek(fd, span_lo, os.SEEK_SET)
                total += len(os.read(fd, span_hi - span_lo))
            return (time.perf_counter() - t0) / repeats * 1e6, total // repeats
        finally:
            os.close(fd)

    seek_us, seek_bytes = time_seeks()
    one_us, one_bytes = time_one()
    calls = 5 + ntex  # header, batches, textures table, verts, indices, each texture
    print(f'file: {path}')
    print(f'version {ver} flags {flags}  batches {nbatch}  textures {ntex}')
    print(f'vertices {nv}  triangles {ni // 3}  geometry {geom} bytes  textures {tex_bytes} bytes')
    print(f'file bytes {len(data)}  gpu span {span_hi - span_lo} bytes ({span_lo}..{span_hi})')
    print(f'old path: {calls} reads, {seek_bytes} bytes, {seek_us:.0f} us')
    print(f'one read: 1 read, {one_bytes} bytes, {one_us:.0f} us')
    if one_us > 0:
        print(f'speedup: {seek_us / one_us:.2f}x')
    print('textures are already Morton-tiled / ETC at bake time; the runtime does not swizzle')
    if flags & 1:
        print('indices are stored absolute (header flags bit0); the loader skips the rebase')
    else:
        print('indices are batch-relative (flags 0); the loader rebases them once in memory')


if __name__ == '__main__':
    main()
