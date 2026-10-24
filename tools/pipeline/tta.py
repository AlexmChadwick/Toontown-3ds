"""Writer for the TTA ("toontown animation") vertex-animation format.

One .tta holds every frame of one animation for one .ttm part. Vertex order is the
.ttm's vertex order. The runtime morphs between two frames in the vertex shader
(shaders/morph.v.pica).

Format v1 (little endian, sections 128-byte aligned):

    Header (64 bytes)
      char[4] magic "TTA1"; u16 version; u16 flags (bit0: has attach transforms)
      u32 numVertices; u16 numFrames; u16 reserved; f32 fps
      f32 posScale[3]; f32 posOffset[3]   -- pos = q * posScale + posOffset
      u32 framesOffset; u32 frameStride; u32 attachOffset; u32[2] reserved
    Frames: numFrames x numVertices x s16[4] (x, y, z, 0)
    Attach (optional): numFrames x f32[12], a 3x4 row-major matrix giving the child
      part's root in this part's space (legs -> torso at joint_hips, torso -> head).

Must stay in sync with source/render/anim.cpp.
"""
import struct

import numpy as np

MAGIC = b'TTA1'
VERSION = 1
ALIGN = 128


def _align(n):
    return (n + ALIGN - 1) // ALIGN * ALIGN


def write(path, frames, fps, attach=None):
    """frames: (F, V, 3) float positions. attach: optional (F, 3, 4) float."""
    frames = np.asarray(frames, dtype=np.float64)
    nf, nv, _ = frames.shape
    lo, hi = frames.reshape(-1, 3).min(axis=0), frames.reshape(-1, 3).max(axis=0)
    offset = (lo + hi) / 2
    scale = np.maximum((hi - lo) / 2 / 32767.0, 1e-6)
    q = np.zeros((nf, nv, 4), dtype='<i2')
    q[:, :, :3] = np.round((frames - offset) / scale).clip(-32767, 32767)

    frames_off = _align(64)
    stride = nv * 8
    attach_off = _align(frames_off + nf * stride) if attach is not None else 0
    size = (attach_off + nf * 48) if attach is not None else frames_off + nf * stride
    out = bytearray(_align(size))
    header = struct.pack('<4sHHIHHf3f3fIIIII', MAGIC, VERSION, 1 if attach is not None else 0, nv, nf, 0,
                         float(fps), *scale, *offset, frames_off, stride, attach_off, 0, 0)
    assert len(header) == 64
    out[0:64] = header
    qb = q.tobytes()
    out[frames_off:frames_off + len(qb)] = qb
    if attach is not None:
        ab = np.asarray(attach, dtype='<f4').reshape(nf, 12).tobytes()
        out[attach_off:attach_off + len(ab)] = ab
    with open(path, 'wb') as f:
        f.write(out)
    return {'frames': nf, 'vertices': nv, 'bytes': len(out)}
