"""Share texture data between the TTM models of one directory (pooled TTM v2 + textures.ttp).

Building interiors repeat the same wallpaper, floor, moulding and furniture textures: the
538 interiors hold about 120 MB of texture copies but only a few MB of distinct payloads.
This pass stores every distinct payload once in `<dir>/textures.ttp` and rewrites each
model as TTM v2: byte-identical to v1 except version=2, the texture data is cut off after
the index data and each texture entry's dataOffset is an offset into textures.ttp
(source/render/model.cpp reads it from there; v1 models keep loading unchanged).

    TTP1 v1, little endian: char[4] "TTP1"; u16 version=1; u16 flags=0; u32 count;
    u32 fileBytes; u32 reserved[4] (header 32 bytes), then the payloads, each 128-byte
    aligned, in first-use order over the models sorted by file name.

The pass is deterministic and idempotent: pooled models are restored from the existing
pool first, so rerunning it (after rebaking any one model as v1, or adding models) gives
the same bytes as pooling the whole directory from scratch. `--unpool` restores every
model to its standalone v1 bytes and removes the pool.

    python -I tools/pipeline/ttm_pool.py sdmc-out/3ds/toontown3ds/data/interiors
"""
import argparse
import os
import struct
import sys
from pathlib import Path

HEADER = struct.Struct('<4sHHHHII6f5I')
TEX = struct.Struct('<HHBBBBII32s')
POOL = struct.Struct('<4sHHII4I')
POOL_NAME = 'textures.ttp'


def align(n):
    return (n + 127) & ~127


def read_pool(directory):
    path = directory / POOL_NAME
    if not path.exists():
        return None
    data = path.read_bytes()
    magic, version, flags, count, size = POOL.unpack_from(data)[:5]
    if magic != b'TTP1' or version != 1 or flags or size != len(data):
        raise ValueError(f'{path}: bad pool header')
    return data


def standalone(blob, pool):
    """v1 bytes of a v1 or v2 model."""
    h = HEADER.unpack_from(blob)
    if h[0] != b'TTM1' or h[1] not in (1, 2):
        raise ValueError('not a TTM1 v1/v2 model')
    if h[1] == 1:
        return bytes(blob)
    if pool is None:
        raise ValueError('pooled model without textures.ttp')
    out = bytearray(blob)
    struct.pack_into('<H', out, 4, 1)
    for i in range(h[3]):
        e = list(TEX.unpack_from(out, h[13] + 48*i))
        payload = pool[e[6]:e[6] + e[7]]
        if e[6] < POOL.size or len(payload) != e[7]:
            raise ValueError('pooled texture outside textures.ttp')
        e[6] = len(out)
        TEX.pack_into(out, h[13] + 48*i, *e)
        out += payload + bytes(align(len(out) + e[7]) - len(out) - e[7])
    return bytes(out)


def pooled(blob, index, payloads, cursor):
    """(v2 bytes, new cursor) for a v1 model; new payloads are appended to `payloads`."""
    h = HEADER.unpack_from(blob)
    if not h[3]:
        return bytes(blob), cursor
    out = bytearray(blob)
    end = len(out)
    struct.pack_into('<H', out, 4, 2)
    for i in range(h[3]):
        e = list(TEX.unpack_from(out, h[13] + 48*i))
        payload = bytes(blob[e[6]:e[6] + e[7]])
        end = min(end, e[6])
        if payload not in index:
            index[payload] = cursor
            payloads.append(payload)
            cursor = align(cursor + len(payload))
        e[6] = index[payload]
        TEX.pack_into(out, h[13] + 48*i, *e)
    return bytes(out[:align(end)]), cursor


def write_atomic(path, data):
    tmp = path.with_name(path.name + '.tmp')
    tmp.write_bytes(data)
    os.replace(tmp, path)


def run(directory, unpool=False):
    directory = Path(directory)
    pool = read_pool(directory)
    files = sorted(directory.glob('*.ttm'))
    models = {p: standalone(p.read_bytes(), pool) for p in files}
    before = sum(p.stat().st_size for p in files) + (len(pool) if pool else 0)
    if unpool:
        for p, data in models.items():
            if p.read_bytes() != data:
                write_atomic(p, data)
        (directory / POOL_NAME).unlink(missing_ok=True)
        return before, sum(len(d) for d in models.values())
    index, payloads, cursor, out = {}, [], align(POOL.size), {}
    for p, data in models.items():
        out[p], cursor = pooled(data, index, payloads, cursor)
    blob = bytearray(cursor)
    POOL.pack_into(blob, 0, b'TTP1', 1, 0, len(payloads), cursor, 0, 0, 0, 0)
    for payload, at in sorted(((pl, off) for pl, off in index.items()), key=lambda x: x[1]):
        blob[at:at + len(payload)] = payload
    # Not atomic across files: an interrupted pass can leave models that point into a
    # different pool. Rerunning the pass cannot repair those; restore with a rebake.
    write_atomic(directory / POOL_NAME, bytes(blob))
    for p, data in out.items():
        if p.read_bytes() != data:
            write_atomic(p, data)
    for p, data in out.items():  # verify: every model round-trips to its v1 bytes
        if standalone(data, bytes(blob)) != models[p]:
            raise AssertionError(f'{p}: pool round trip mismatch')
    return before, sum(len(d) for d in out.values()) + len(blob)


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('directory')
    ap.add_argument('--unpool', action='store_true', help='restore standalone v1 models')
    args = ap.parse_args()
    before, after = run(args.directory, args.unpool)
    print(f'{args.directory}: {before} -> {after} bytes', file=sys.stderr)
