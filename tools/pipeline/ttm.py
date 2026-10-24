"""Writer for the TTM ("toontown model") runtime format + PICA200 texture encoding.

Format v1 (little endian, every section 128-byte aligned so the runtime can hand
section pointers straight to the GPU after copying into linear memory):

    Header (64 bytes)
      char[4] magic "TTM1"; u16 version; u16 flags
      u16 numTextures; u16 numBatches; u32 numVertices; u32 numIndices
      f32 posScale[3]; f32 posOffset[3]   -- world = q * posScale + posOffset
      u32 texTableOffset; u32 batchTableOffset; u32 vertexOffset; u32 indexOffset; u32 reserved
    Texture entry (48 bytes)
      u16 width; u16 height; u8 gpuFormat (GPU_TEXCOLOR); u8 mipLevels (incl. base)
      u8 wrapS; u8 wrapT (GPU_TEXTURE_WRAP_PARAM); u32 dataOffset; u32 dataSize; char[32] name
    Batch entry (24 bytes)
      u16 texture (0xFFFF = none); u8 blend (0 opaque, 1 alpha test, 2 alpha blend)
      u8 flags (bit0 two-sided); u8 tintSlot (0 = none); u8 layer (draw-in-front order)
      u8 texSlot (0 = own texture; 1 shirt, 2 sleeve, 3 bottom: the runtime binds the
      toon's DNA clothing texture instead, see bake_toon.py); u8 pad
      u32 firstVertex; u32 numVertices; u32 firstIndex; u32 numIndices
    Vertex (16 bytes): s16 pos[3], s16 pad, s16 uv[2] (x/1024), u8 rgba[4]
    Index: u16, relative to the batch's firstVertex

Must stay in sync with source/render/model.cpp.
"""
import struct

import numpy as np

import etc1

MAGIC = b'TTM1'
VERSION = 1
UV_SCALE = 1024.0
ALIGN = 128
MAX_BATCH_VERTS = 65535

# GPU_TEXCOLOR values from libctru gpu/enums.h
GPU_RGBA8, GPU_RGB8, GPU_RGBA5551, GPU_RGB565, GPU_RGBA4 = 0, 1, 2, 3, 4
GPU_LA8, GPU_HILO8, GPU_L8, GPU_A8, GPU_LA4 = 5, 6, 7, 8, 9
GPU_ETC1, GPU_ETC1A4 = etc1.GPU_ETC1, etc1.GPU_ETC1A4   # 0xC (4 bpp), 0xD (8 bpp)
ETC_FORMATS = (GPU_ETC1, GPU_ETC1A4)
# GPU_TEXTURE_WRAP_PARAM
GPU_CLAMP_TO_EDGE, GPU_REPEAT = 0, 2

BLEND_OPAQUE, BLEND_ALPHA_TEST, BLEND_ALPHA = 0, 1, 2

FORMAT_NAMES = {GPU_RGBA8: 'RGBA8', GPU_RGB565: 'RGB565', GPU_RGBA5551: 'RGBA5551',
                GPU_RGBA4: 'RGBA4', GPU_LA8: 'LA8', GPU_L8: 'L8', GPU_A8: 'A8', GPU_LA4: 'LA4',
                GPU_ETC1: 'ETC1', GPU_ETC1A4: 'ETC1A4'}
# Uncompressed formats only (ETC1 is 1/2 byte per pixel); see BITS_PER_PIXEL.
BYTES_PER_PIXEL = {GPU_RGBA8: 4, GPU_RGB565: 2, GPU_RGBA5551: 2, GPU_RGBA4: 2,
                   GPU_LA8: 2, GPU_L8: 1, GPU_A8: 1, GPU_LA4: 1}
BITS_PER_PIXEL = {**{f: 8 * b for f, b in BYTES_PER_PIXEL.items()}, GPU_ETC1: 4, GPU_ETC1A4: 8}


def level_size(w, h, fmt):
    """Bytes of one mip level (= citro3d C3D_TexCalcLevelSize)."""
    return BITS_PER_PIXEL[fmt] * w * h // 8


def _align(n, a=ALIGN):
    return (n + a - 1) // a * a


def _morton_table():
    # Index of pixel (x, y) inside an 8x8 PICA tile: bits interleaved x0 y0 x1 y1 x2 y2.
    t = np.zeros((8, 8), dtype=np.int64)
    for y in range(8):
        for x in range(8):
            i = 0
            for b in range(3):
                i |= ((x >> b) & 1) << (2 * b) | ((y >> b) & 1) << (2 * b + 1)
            t[y, x] = i
    return t


_MORTON = _morton_table()


def tile(pixels):
    """(h, w, c) array (row 0 = top of image) -> PICA 8x8 Morton-tiled (h*w, c) array.

    The PICA treats the first row in memory as t=1 (same convention as tex3ds/citro2d,
    whose full-image subtexture is top=1.0), so with Panda UVs (v=1 at image top) the
    image is stored top row first, without flipping.
    """
    h, w, c = pixels.shape
    flipped = pixels
    out = np.empty((h * w, c), dtype=pixels.dtype)
    tiles_x = w // 8
    for ty in range(h // 8):
        for tx in range(w // 8):
            block = flipped[ty * 8:(ty + 1) * 8, tx * 8:(tx + 1) * 8].reshape(64, c)
            base = (ty * tiles_x + tx) * 64
            out[base + _MORTON.reshape(64)] = block
    return out


def encode(pixels, fmt):
    """pixels: (h, w, 4) uint8 RGBA, row 0 = top. Returns tiled bytes for one level."""
    if fmt in ETC_FORMATS:
        return etc1.encode_levels([pixels], fmt == GPU_ETC1A4)
    t = tile(pixels).astype(np.uint32)
    r, g, b, a = t[:, 0], t[:, 1], t[:, 2], t[:, 3]
    if fmt == GPU_RGBA8:
        # Stored as u32 0xRRGGBBAA little endian -> bytes A, B, G, R.
        return np.stack([a, b, g, r], axis=1).astype(np.uint8).tobytes()
    if fmt == GPU_RGB565:
        v = (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)
        return v.astype('<u2').tobytes()
    if fmt == GPU_RGBA5551:
        v = (r >> 3) << 11 | (g >> 3) << 6 | (b >> 3) << 1 | (a >> 7)
        return v.astype('<u2').tobytes()
    if fmt == GPU_RGBA4:
        v = (r >> 4) << 12 | (g >> 4) << 8 | (b >> 4) << 4 | (a >> 4)
        return v.astype('<u2').tobytes()
    if fmt == GPU_LA8:
        return (r << 8 | a).astype('<u2').tobytes()
    if fmt == GPU_LA4:
        return ((r >> 4) << 4 | (a >> 4)).astype(np.uint8).tobytes()
    if fmt == GPU_L8:
        return r.astype(np.uint8).tobytes()
    if fmt == GPU_A8:
        return a.astype(np.uint8).tobytes()
    raise ValueError(f'unsupported format {fmt}')


def mip_chain(pixels, min_size=8):
    levels = [pixels]
    while min(levels[-1].shape[:2]) > min_size:
        p = levels[-1].astype(np.uint16)
        p = (p[0::2, 0::2] + p[1::2, 0::2] + p[0::2, 1::2] + p[1::2, 1::2] + 2) // 4
        levels.append(p.astype(np.uint8))
    return levels


class Texture:
    def __init__(self, name, pixels, fmt, wrap_s=GPU_REPEAT, wrap_t=GPU_REPEAT, mipmaps=True):
        h, w = pixels.shape[:2]
        assert w >= 8 and h >= 8 and w & (w - 1) == 0 and h & (h - 1) == 0, (name, w, h)
        self.name, self.w, self.h, self.fmt = name, w, h, fmt
        self.wrap_s, self.wrap_t = wrap_s, wrap_t
        levels = mip_chain(pixels) if mipmaps else [pixels]
        self.mips = len(levels)
        if fmt in ETC_FORMATS:
            # One call for the whole chain (better vectorization); levels are
            # concatenated largest first, exactly like the uncompressed path.
            self.data = etc1.encode_levels(levels, fmt == GPU_ETC1A4)
        else:
            self.data = b''.join(encode(lv, fmt) for lv in levels)
        assert len(self.data) == sum(level_size(lv.shape[1], lv.shape[0], fmt) for lv in levels)


class Batch:
    def __init__(self, texture, blend, two_sided, tint, layer, verts, indices):
        self.texture, self.blend, self.two_sided = texture, blend, two_sided
        self.tint, self.layer = tint, layer
        self.verts = verts      # (n, 9) float: x y z u v r g b a (rgba 0..1)
        self.indices = indices  # (m,) int, relative to this batch


def _rebase_uvs(b):
    """Keep UVs inside the int16 range: unweld triangles and shift each by whole repeats.

    Only applied to batches that overflow (heavily tiled terrain); others keep sharing.
    """
    limit = 32767 / UV_SCALE
    if np.all(np.abs(b.verts[:, 3:5]) <= limit):
        return b
    tri = b.indices.reshape(-1, 3)
    verts = b.verts[tri.reshape(-1)].copy().reshape(-1, 3, 9)
    shift = np.floor(verts[:, :, 3:5].min(axis=1))
    verts[:, :, 3:5] -= shift[:, None, :]
    verts = verts.reshape(-1, 9)
    return Batch(b.texture, b.blend, b.two_sided, b.tint, b.layer, verts,
                 np.arange(len(verts), dtype=np.int64))


def write(path, textures, batches):
    split = []
    for b in map(_rebase_uvs, batches):
        for s in range(0, len(b.indices), MAX_BATCH_VERTS // 3 * 3) if len(b.verts) > MAX_BATCH_VERTS else [None]:
            if s is None:
                split.append(b)
            else:
                idx = b.indices[s:s + MAX_BATCH_VERTS // 3 * 3]
                split.append(Batch(b.texture, b.blend, b.two_sided, b.tint, b.layer,
                                   b.verts[idx], np.arange(len(idx), dtype=np.int64)))
    batches = split
    all_pos = np.concatenate([b.verts[:, :3] for b in batches])
    lo, hi = all_pos.min(axis=0), all_pos.max(axis=0)
    offset = (lo + hi) / 2
    scale = np.maximum((hi - lo) / 2 / 32767.0, 1e-6)

    vert_chunks, idx_chunks, batch_entries = [], [], []
    first_v = first_i = 0
    uv_clamped = 0
    for b in batches:
        n = len(b.verts)
        assert n <= MAX_BATCH_VERTS
        q = np.round((b.verts[:, :3] - offset) / scale).clip(-32767, 32767).astype('<i2')
        uv = np.round(b.verts[:, 3:5] * UV_SCALE)
        uv_clamped += int(np.count_nonzero((uv < -32768) | (uv > 32767)))
        uv = uv.clip(-32768, 32767).astype('<i2')
        rgba = np.round(b.verts[:, 5:9] * 255).clip(0, 255).astype(np.uint8)
        rec = np.zeros(n, dtype=[('p', '<i2', 4), ('uv', '<i2', 2), ('c', 'u1', 4)])
        rec['p'][:, :3] = q
        rec['uv'] = uv
        rec['c'] = rgba
        vert_chunks.append(rec.tobytes())
        idx_chunks.append(b.indices.astype('<u2').tobytes())
        batch_entries.append(struct.pack('<HBBBBBBIIII', 0xFFFF if b.texture is None else b.texture,
                                         b.blend, 1 if b.two_sided else 0, b.tint, b.layer,
                                         getattr(b, 'tex_slot', 0), 0,
                                         first_v, n, first_i, len(b.indices)))
        first_v += n
        first_i += len(b.indices)

    header_size = 64
    tex_table = header_size
    batch_table = _align(tex_table + 48 * len(textures), 4)
    vertex_off = _align(batch_table + 24 * len(batches))
    index_off = _align(vertex_off + 16 * first_v)
    cursor = _align(index_off + 2 * first_i)
    tex_entries, tex_blobs = [], []
    for t in textures:
        tex_entries.append(struct.pack('<HHBBBBII32s', t.w, t.h, t.fmt, t.mips, t.wrap_s, t.wrap_t,
                                       cursor, len(t.data), t.name.encode()[:31]))
        tex_blobs.append((cursor, t.data))
        cursor = _align(cursor + len(t.data))

    out = bytearray(cursor)
    header = struct.pack('<4sHHHHII3f3fIIIII', MAGIC, VERSION, 0, len(textures), len(batches),
                         first_v, first_i, *scale, *offset,
                         tex_table, batch_table, vertex_off, index_off, 0)
    assert len(header) == header_size
    out[0:header_size] = header
    for i, e in enumerate(tex_entries):
        out[tex_table + 48 * i: tex_table + 48 * (i + 1)] = e
    for i, e in enumerate(batch_entries):
        out[batch_table + 24 * i: batch_table + 24 * (i + 1)] = e
    vb = b''.join(vert_chunks)
    out[vertex_off:vertex_off + len(vb)] = vb
    ib = b''.join(idx_chunks)
    out[index_off:index_off + len(ib)] = ib
    for off, data in tex_blobs:
        out[off:off + len(data)] = data
    with open(path, 'wb') as f:
        f.write(out)

    tex_bytes = sum(len(t.data) for t in textures)
    return {
        'file_bytes': len(out), 'vertices': first_v, 'triangles': first_i // 3,
        'batches': len(batches), 'textures': len(textures), 'texture_bytes': tex_bytes,
        'geometry_bytes': 16 * first_v + 2 * first_i, 'uv_clamped': uv_clamped,
        'bounds': (lo.tolist(), hi.tolist()),
    }
