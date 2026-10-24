"""TSND ("toontown sound") container: writer + reader.

Format v1, little endian. Mirrors source/audio/audio.cpp (kTsnd* constants there);
change both together and bump VERSION.

    Header (64 bytes)
      0  char[4] magic "TSND"
      4  u16 version (1)
      6  u8  encoding    1 = PCM16, 2 = DSP-ADPCM (same values as NDSP_ENCODING_*)
      7  u8  channels    1 or 2 (channels are stored planar, one NDSP voice each)
      8  u32 sampleRate  Hz (fed to ndspChnSetRate)
      12 u32 numSamples  per channel
      16 u32 loopStart   sample index; multiple of 14 for ADPCM (frame aligned)
      20 u32 loopEnd     exclusive; == numSamples for a whole-file loop
      24 u32 flags       bit0 = loops
      28 u32 blockSamples   samples per channel per block (ADPCM: multiple of 14)
      32 u32 blockBytes     data bytes per channel per block
      36 u32 blockStride    bytes per block on disk = 32 + channels * blockBytes
      40 u32 numBlocks
      44 u32 numFrames      ADPCM frames per channel (ceil(numSamples / 14)); 0 for PCM16
      48 u32 dataOffset     file offset of block 0 (32-byte aligned)
      52 u32 dataSize       numBlocks * blockStride
      56 u8[8] reserved (0)
    Channel table (48 bytes per channel, at offset 64)
      s16 coefs[16]   DSP-ADPCM predictor pairs (c1,c2) x 8, Q11 (ndspChnSetAdpcmCoefs order)
      u16 loopPs; s16 loopHist0; s16 loopHist1; u16 pad
                      decoder state at loopStart (hist0 = last output sample)
      u8[8] reserved
      (all zero for PCM16)
    Block i (blockStride bytes, at dataOffset + i * blockStride)
      32-byte block header: per channel {u16 ps; s16 hist0; s16 hist1; u16 pad}
                      = ADPCM decoder state at sample i * blockSamples; rest zero
      channel 0 data (blockBytes), channel 1 data (blockBytes), ...
      The final block is zero padded to blockBytes.

DSP-ADPCM data (GameCube/3DS "DSPADPCM"): 8-byte frames, each a header byte
(high nibble = predictor index 0..7, low nibble = shift 0..12) followed by 14
signed 4-bit samples, high nibble first. Decoding:
    pred = c1[idx] * hist0 + c2[idx] * hist1
    out  = clamp16(((nib << shift) << 11) + 1024 + pred) >> 11)
A block boundary is always a frame boundary, so the runtime can hand any block
(or, at loopStart, any frame inside a block) straight to an ndspWaveBuf along
with the stored ndspAdpcmData {ps, hist0, hist1}.

PCM16 data is plain s16 samples; the block header is zero. Short SFX are a single
block (blockSamples == numSamples) so they can be loaded with one read.
"""
import struct

import numpy as np

MAGIC = b'TSND'
VERSION = 1
ENC_PCM16 = 1
ENC_ADPCM = 2
FLAG_LOOP = 1
HEADER_SIZE = 64
CHANNEL_ENTRY = 48
BLOCK_HEADER = 32
SAMPLES_PER_FRAME = 14
BYTES_PER_FRAME = 8

_HDR = struct.Struct('<4sHBBIIIIIIIIIIII8x')
assert _HDR.size == HEADER_SIZE


def _align(v, a):
    return (v + a - 1) // a * a


def write(path, *, encoding, sample_rate, num_samples, channel_data, loop=False,
          loop_start=0, loop_end=None, block_samples=None, coefs=None,
          block_ctx=None, loop_ctx=None):
    """Write a .tsnd file.

    channel_data: list (one per channel) of bytes. ADPCM: frame bytes; PCM16: s16 LE.
    coefs: list per channel of 16 ints (ADPCM only).
    block_ctx: list per channel of list per block of (ps, hist0, hist1) (ADPCM only).
    loop_ctx: list per channel of (ps, hist0, hist1) at loop_start (ADPCM only).
    Returns total bytes written.
    """
    channels = len(channel_data)
    assert channels in (1, 2)
    if loop_end is None:
        loop_end = num_samples
    if block_samples is None:
        block_samples = num_samples
    if encoding == ENC_ADPCM:
        assert block_samples % SAMPLES_PER_FRAME == 0
        assert loop_start % SAMPLES_PER_FRAME == 0
        block_bytes = block_samples // SAMPLES_PER_FRAME * BYTES_PER_FRAME
        num_frames = (num_samples + SAMPLES_PER_FRAME - 1) // SAMPLES_PER_FRAME
    else:
        block_bytes = block_samples * 2
        num_frames = 0
    block_bytes = _align(block_bytes, 4)
    num_blocks = max(1, (num_samples + block_samples - 1) // block_samples)
    stride = BLOCK_HEADER + channels * block_bytes
    data_offset = _align(HEADER_SIZE + CHANNEL_ENTRY * channels, 32)

    out = bytearray(_HDR.pack(MAGIC, VERSION, encoding, channels, sample_rate,
                              num_samples, loop_start, loop_end,
                              FLAG_LOOP if loop else 0, block_samples, block_bytes,
                              stride, num_blocks, num_frames, data_offset,
                              num_blocks * stride))
    for c in range(channels):
        if encoding == ENC_ADPCM:
            out += struct.pack('<16h', *coefs[c])
            ps, h0, h1 = loop_ctx[c]
            out += struct.pack('<Hhhh8x', ps, h0, h1, 0)
        else:
            out += bytes(CHANNEL_ENTRY)
    out += bytes(data_offset - len(out))

    for b in range(num_blocks):
        hdr = bytearray(BLOCK_HEADER)
        if encoding == ENC_ADPCM:
            for c in range(channels):
                struct.pack_into('<Hhhh', hdr, c * 8, *block_ctx[c][b], 0)
        out += hdr
        for c in range(channels):
            chunk = channel_data[c][b * block_bytes:(b + 1) * block_bytes]
            out += chunk + bytes(block_bytes - len(chunk))
    with open(path, 'wb') as f:
        f.write(out)
    return len(out)


def read(path):
    """Parse a .tsnd file. Returns a dict with header fields plus per-channel
    'data' (bytes, concatenated across blocks, trimmed), 'coefs', 'loop_ctx',
    'block_ctx'."""
    with open(path, 'rb') as f:
        buf = f.read()
    (magic, version, enc, ch, rate, ns, ls, le, flags, bs, bb, stride, nb, nf,
     doff, dsize) = _HDR.unpack_from(buf, 0)
    if magic != MAGIC or version != VERSION:
        raise ValueError(f'{path}: not a TSND v{VERSION} file')
    if dsize != nb * stride or doff + dsize > len(buf):
        raise ValueError(f'{path}: truncated')
    info = dict(encoding=enc, channels=ch, sample_rate=rate, num_samples=ns,
                loop_start=ls, loop_end=le, loop=bool(flags & FLAG_LOOP),
                block_samples=bs, block_bytes=bb, block_stride=stride,
                num_blocks=nb, num_frames=nf, data_offset=doff, size=len(buf))
    info['coefs'] = []
    info['loop_ctx'] = []
    for c in range(ch):
        o = HEADER_SIZE + c * CHANNEL_ENTRY
        info['coefs'].append(list(struct.unpack_from('<16h', buf, o)))
        info['loop_ctx'].append(struct.unpack_from('<Hhh', buf, o + 32))
    data = [bytearray() for _ in range(ch)]
    info['block_ctx'] = [[] for _ in range(ch)]
    for b in range(nb):
        base = doff + b * stride
        for c in range(ch):
            info['block_ctx'][c].append(struct.unpack_from('<Hhh', buf, base + c * 8))
            s = base + BLOCK_HEADER + c * bb
            data[c] += buf[s:s + bb]
    if enc == ENC_ADPCM:
        keep = nf * BYTES_PER_FRAME
    else:
        keep = ns * 2
    info['data'] = [bytes(d[:keep]) for d in data]
    return info


def pcm16_bytes(samples):
    return np.ascontiguousarray(samples, dtype='<i2').tobytes()
