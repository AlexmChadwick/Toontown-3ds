#include "audio/audio.h"

#include <3ds.h>
#include <cstdio>
#include <cstring>

#include "core/log.h"

namespace tt::audio {

namespace {

// ---- .tsnd format (mirror of tools/pipeline/audio_tsnd.py, keep in sync) ----
constexpr char kTsndMagic[4] = {'T', 'S', 'N', 'D'};
constexpr uint16_t kTsndVersion = 1;
constexpr uint8_t kEncPcm16 = 1;
constexpr uint8_t kEncAdpcm = 2;
constexpr uint32_t kFlagLoop = 1;
constexpr uint32_t kBlockHeaderBytes = 32;
constexpr uint32_t kAdpcmSamplesPerFrame = 14;
constexpr uint32_t kAdpcmBytesPerFrame = 8;

struct TsndHeader {
    char magic[4];
    uint16_t version;
    uint8_t encoding;
    uint8_t channels;
    uint32_t sampleRate;
    uint32_t numSamples;
    uint32_t loopStart;
    uint32_t loopEnd;
    uint32_t flags;
    uint32_t blockSamples;
    uint32_t blockBytes;
    uint32_t blockStride;
    uint32_t numBlocks;
    uint32_t numFrames;
    uint32_t dataOffset;
    uint32_t dataSize;
    uint8_t reserved[8];
};
static_assert(sizeof(TsndHeader) == 64, "TSND header layout");

struct AdpcmCtx {
    uint16_t ps;
    int16_t hist0;
    int16_t hist1;
    uint16_t pad;
};
static_assert(sizeof(AdpcmCtx) == 8, "TSND ctx layout");

struct TsndChannel {
    int16_t coefs[16];
    AdpcmCtx loopCtx;
    uint8_t reserved[8];
};
static_assert(sizeof(TsndChannel) == 48, "TSND channel layout");

// Reads and validates header + channel table. Leaves the file position undefined.
bool readHeader(FILE* f, const char* path, TsndHeader& h, TsndChannel ch[2]) {
    if (fread(&h, sizeof(h), 1, f) != 1 || memcmp(h.magic, kTsndMagic, 4) != 0) {
        TT_LOG("audio: %s: not a .tsnd file", path);
        return false;
    }
    if (h.version != kTsndVersion) {
        TT_LOG("audio: %s: tsnd v%u, need v%u (re-run bake_audio.py)", path, h.version,
               kTsndVersion);
        return false;
    }
    bool ok = (h.encoding == kEncPcm16 || h.encoding == kEncAdpcm) &&
              (h.channels == 1 || h.channels == 2) && h.sampleRate >= 1000 &&
              h.sampleRate <= 48000 && h.numSamples > 0 && h.blockSamples > 0 &&
              h.numBlocks > 0 && h.blockStride == kBlockHeaderBytes + h.channels * h.blockBytes &&
              h.loopStart < h.loopEnd && h.loopEnd <= h.numSamples;
    if (ok && h.encoding == kEncAdpcm)
        ok = h.blockSamples % kAdpcmSamplesPerFrame == 0 &&
             h.loopStart % kAdpcmSamplesPerFrame == 0 &&
             h.blockBytes >= h.blockSamples / kAdpcmSamplesPerFrame * kAdpcmBytesPerFrame;
    if (ok && h.encoding == kEncPcm16) ok = h.blockBytes >= h.blockSamples * 2;
    if (!ok) {
        TT_LOG("audio: %s: bad tsnd header", path);
        return false;
    }
    if (fread(ch, sizeof(TsndChannel), h.channels, f) != h.channels) {
        TT_LOG("audio: %s: truncated", path);
        return false;
    }
    return true;
}

// Byte offset of a sample inside one channel's block data (frame aligned for ADPCM).
inline uint32_t sampleByteOffset(uint8_t enc, uint32_t s) {
    return enc == kEncAdpcm ? s / kAdpcmSamplesPerFrame * kAdpcmBytesPerFrame : s * 2;
}

inline u16 ndspFormat(uint8_t enc) {
    return enc == kEncAdpcm ? NDSP_FORMAT_MONO_ADPCM : NDSP_FORMAT_MONO_PCM16;
}

inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void setMix(int chn, float left, float right) {
    float mix[12] = {};
    mix[0] = left;
    mix[1] = right;
    ndspChnSetMix(chn, mix);
}

bool gOk = false;
size_t gLinearUsed = 0;

// ---------------------------------------------------------------- music ----

constexpr int kMaxSlots = 8;
constexpr int kStreamPrio = 0x30;       // AGENTS.md threads: 0x30-0x31, core 0
constexpr size_t kStreamStack = 16 * 1024;
constexpr s64 kStreamPollNs = 20 * 1000 * 1000; // 20 ms; one ADPCM block is ~450 ms

enum class Cmd : int { None, Start, Stop, Quit };

struct Music {
    // Owned by main while the stream thread is idle, by the stream thread while
    // `streaming` is true. Handover happens through cmd + wake/ack events.
    FILE* file = nullptr;
    TsndHeader hdr{};
    TsndChannel ch[2]{};
    bool loop = false;
    int numSlots = 0;
    uint8_t* ring = nullptr; // linear, kMusicRingBytes
    ndspWaveBuf wb[kMaxSlots][2]{};
    AdpcmCtx ctx[kMaxSlots][2]{};
    int nextSlot = 0;
    uint32_t cursor = 0;
    long filePos = -1;
    bool eof = false;

    // Thread state.
    Thread thread = nullptr;
    LightEvent wake;
    LightEvent ack;
    volatile Cmd cmd = Cmd::None;
    volatile bool streaming = false; // written by stream thread
    volatile bool ioError = false;
    volatile uint32_t underruns = 0;

    // Main-thread volume state.
    float volume = 0.8f;
    float fade = 1.0f;
    float fadePerMs = 0.0f; // > 0 while fading out
    float fadeInPerMs = 0.0f; // > 0 while fading in
    u64 lastTick = 0;
    float appliedGain = -1.0f;
};

Music gMusic;

void applyMusicGain() {
    float g = gMusic.volume * gMusic.fade;
    if (g == gMusic.appliedGain) return;
    gMusic.appliedGain = g;
    if (gMusic.hdr.channels == 2) {
        setMix(kMusicChannelL, g, 0.0f);
        setMix(kMusicChannelR, 0.0f, g);
    } else {
        setMix(kMusicChannelL, g, g);
    }
}

// Stream thread: is every wavebuf of the slot finished (or never queued)?
bool slotFree(int s) {
    for (int c = 0; c < gMusic.hdr.channels; ++c) {
        u8 st = gMusic.wb[s][c].status;
        if (st != NDSP_WBUF_FREE && st != NDSP_WBUF_DONE) return false;
    }
    return true;
}

bool allSlotsFree() {
    for (int s = 0; s < gMusic.numSlots; ++s)
        if (!slotFree(s)) return false;
    return true;
}

// Stream thread: read the next segment into slot s and queue it. Returns false on IO error.
bool fillSlot(int s) {
    Music& m = gMusic;
    const TsndHeader& h = m.hdr;
    const uint32_t end = m.loop ? h.loopEnd : h.numSamples;
    const uint32_t block = m.cursor / h.blockSamples;
    const uint32_t inBlock = m.cursor - block * h.blockSamples;
    uint32_t segEnd = (block + 1) * h.blockSamples;
    if (segEnd > end) segEnd = end;

    uint8_t* slot = m.ring + size_t(s) * h.blockStride;
    long off = long(h.dataOffset) + long(block) * long(h.blockStride);
    if (off != m.filePos && fseek(m.file, off, SEEK_SET) != 0) return false;
    if (fread(slot, h.blockStride, 1, m.file) != 1) {
        m.filePos = -1;
        return false;
    }
    m.filePos = off + long(h.blockStride);
    DSP_FlushDataCache(slot, h.blockStride);

    const AdpcmCtx* blockCtx = reinterpret_cast<const AdpcmCtx*>(slot);
    for (int c = 0; c < h.channels; ++c) {
        ndspWaveBuf& wb = m.wb[s][c];
        memset(&wb, 0, sizeof(wb));
        wb.data_vaddr = slot + kBlockHeaderBytes + c * h.blockBytes +
                        sampleByteOffset(h.encoding, inBlock);
        wb.nsamples = segEnd - m.cursor;
        wb.looping = false;
        if (h.encoding == kEncAdpcm) {
            // Block starts carry their own decoder state; a mid-block start can only
            // be the loop point, whose state is in the channel table.
            m.ctx[s][c] = inBlock == 0 ? blockCtx[c] : m.ch[c].loopCtx;
            wb.adpcm_data = reinterpret_cast<ndspAdpcmData*>(&m.ctx[s][c]);
        }
        ndspChnWaveBufAdd(c == 0 ? kMusicChannelL : kMusicChannelR, &wb);
    }

    m.cursor = segEnd;
    if (m.cursor >= end) {
        if (m.loop)
            m.cursor = h.loopStart;
        else
            m.eof = true;
    }
    return true;
}

void setupMusicChannels() {
    const TsndHeader& h = gMusic.hdr;
    for (int c = 0; c < h.channels; ++c) {
        int chn = c == 0 ? kMusicChannelL : kMusicChannelR;
        ndspChnReset(chn);
        ndspChnSetInterp(chn, NDSP_INTERP_POLYPHASE);
        ndspChnSetRate(chn, float(h.sampleRate));
        ndspChnSetFormat(chn, ndspFormat(h.encoding));
        if (h.encoding == kEncAdpcm)
            ndspChnSetAdpcmCoefs(chn, reinterpret_cast<u16*>(gMusic.ch[c].coefs));
        ndspChnSetPaused(chn, true);
    }
    gMusic.appliedGain = -1.0f;
    applyMusicGain();
}

void stopMusicChannels() {
    ndspChnWaveBufClear(kMusicChannelL);
    ndspChnWaveBufClear(kMusicChannelR);
    ndspChnReset(kMusicChannelL);
    ndspChnReset(kMusicChannelR);
}

void streamThread(void*) {
    Music& m = gMusic;
    for (;;) {
        if (m.streaming)
            LightEvent_WaitTimeout(&m.wake, kStreamPollNs);
        else
            LightEvent_Wait(&m.wake);

        Cmd cmd = m.cmd;
        if (cmd != Cmd::None) {
            m.cmd = Cmd::None;
            if (cmd == Cmd::Quit || cmd == Cmd::Stop) {
                if (m.streaming) stopMusicChannels();
                m.streaming = false;
                LightEvent_Signal(&m.ack);
                if (cmd == Cmd::Quit) break;
                continue;
            }
            if (cmd == Cmd::Start) {
                // Prefill every slot while paused, then start L and R together.
                bool ok = true;
                for (int s = 0; s < m.numSlots && ok && !m.eof; ++s) {
                    ok = fillSlot(s);
                    m.nextSlot = (s + 1) % m.numSlots;
                }
                if (ok) {
                    for (int c = 0; c < m.hdr.channels; ++c)
                        ndspChnSetPaused(c == 0 ? kMusicChannelL : kMusicChannelR, false);
                    m.streaming = true;
                } else {
                    stopMusicChannels();
                    m.ioError = true;
                }
                LightEvent_Signal(&m.ack);
                continue;
            }
        }

        if (!m.streaming) continue;

        // Refill finished slots, oldest first (they complete in queue order).
        while (!m.eof && slotFree(m.nextSlot)) {
            if (allSlotsFree()) ++m.underruns; // everything drained: audible gap
            if (!fillSlot(m.nextSlot)) {
                stopMusicChannels();
                m.streaming = false;
                m.ioError = true;
                break;
            }
            m.nextSlot = (m.nextSlot + 1) % m.numSlots;
        }
        if (m.streaming && m.eof && allSlotsFree()) {
            stopMusicChannels();
            m.streaming = false;
        }
    }
}

// Main thread: send a command and wait until the stream thread has handled it.
void sendCmd(Cmd c) {
    if (!gMusic.thread) return;
    LightEvent_Clear(&gMusic.ack);
    gMusic.cmd = c;
    LightEvent_Signal(&gMusic.wake);
    LightEvent_Wait(&gMusic.ack);
}

void stopMusicNow() {
    if (!gOk) return;
    sendCmd(Cmd::Stop);
    if (gMusic.file) fclose(gMusic.file);
    gMusic.file = nullptr;
    gMusic.fadePerMs = 0.0f;
    gMusic.fadeInPerMs = 0.0f;
    gMusic.eof = false;
}

// ------------------------------------------------------------------ sfx ----

struct Sfx {
    void* data = nullptr; // linear
    uint32_t bytes = 0;
    uint32_t numSamples = 0;
    uint32_t sampleRate = 0;
    uint8_t encoding = 0;
    int16_t coefs[16] = {};
    AdpcmCtx ctx{};
};

struct Voice {
    ndspWaveBuf wb[2]{}; // alternated so a stolen voice never rewrites a live struct
    ndspAdpcmData ctx[2]{};
    int cur = 0;
    SfxId sfx = kInvalidSfx;
    bool loop = false;
    uint16_t gen = 0;
    uint32_t startSeq = 0;
};

Sfx gSfx[kMaxSfx];
Voice gVoices[kNumSfxVoices];
uint32_t gVoiceSeq = 0;

inline int voiceChannel(int v) { return kSfxFirstChannel + v; }

bool voiceBusy(int v) {
    const Voice& vo = gVoices[v];
    if (vo.sfx == kInvalidSfx) return false;
    u8 st = vo.wb[vo.cur].status;
    return st == NDSP_WBUF_QUEUED || st == NDSP_WBUF_PLAYING;
}

void stopVoiceIndex(int v) {
    ndspChnWaveBufClear(voiceChannel(v));
    gVoices[v].sfx = kInvalidSfx;
}

int voiceFromHandle(VoiceHandle h) {
    if (h == kNoVoice) return -1;
    int v = int(h & 0xFF) - 1;
    if (v < 0 || v >= kNumSfxVoices) return -1;
    if (gVoices[v].gen != uint16_t(h >> 8) || !voiceBusy(v)) return -1;
    return v;
}

void voiceMix(int v, float volume, float pan) {
    float g = clamp01(volume);
    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;
    // Centre = full level in both speakers (like upstream's non-positional SFX).
    float l = g * (pan > 0.0f ? 1.0f - pan : 1.0f);
    float r = g * (pan < 0.0f ? 1.0f + pan : 1.0f);
    setMix(voiceChannel(v), l, r);
}

} // namespace

// ------------------------------------------------------------- public API --

bool init() {
    if (gOk) return true;
    Result rc = ndspInit();
    if (R_FAILED(rc)) {
        TT_LOG("audio: ndspInit failed 0x%08lX (missing sdmc:/3ds/dspfirm.cdc?) - silent mode",
               (unsigned long)rc);
        return false;
    }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspSetMasterVol(1.0f);

    gMusic.ring = static_cast<uint8_t*>(linearAlloc(kMusicRingBytes));
    if (!gMusic.ring) {
        TT_LOG("audio: no linear memory for music ring - silent mode");
        ndspExit();
        return false;
    }
    gLinearUsed = kMusicRingBytes;
    LightEvent_Init(&gMusic.wake, RESET_ONESHOT);
    LightEvent_Init(&gMusic.ack, RESET_ONESHOT);
    gMusic.cmd = Cmd::None;
    gMusic.streaming = false;
    gMusic.thread = threadCreate(streamThread, nullptr, kStreamStack, kStreamPrio, 0, false);
    if (!gMusic.thread) {
        TT_LOG("audio: stream thread failed - music disabled");
    }
    for (Voice& v : gVoices) v = Voice{};
    gOk = true;
    TT_LOG("audio: ndsp ok, music ring %u KB", unsigned(kMusicRingBytes / 1024));
    return true;
}

void shutdown() {
    if (!gOk) return;
    stopMusicNow();
    if (gMusic.thread) {
        sendCmd(Cmd::Quit);
        threadJoin(gMusic.thread, U64_MAX);
        threadFree(gMusic.thread);
        gMusic.thread = nullptr;
    }
    for (int v = 0; v < kNumSfxVoices; ++v) stopVoiceIndex(v);
    for (SfxId i = 0; i < kMaxSfx; ++i) freeSfx(i);
    ndspExit();
    linearFree(gMusic.ring);
    gMusic.ring = nullptr;
    gLinearUsed = 0;
    gOk = false;
}

bool available() { return gOk; }

bool playMusic(const char* path, bool loop, int fadeInMs) {
    if (!gOk || !gMusic.thread) return false;
    stopMusicNow();

    FILE* f = fopen(path, "rb");
    if (!f) {
        TT_LOG("audio: cannot open %s", path);
        return false;
    }
    setvbuf(f, nullptr, _IONBF, 0); // fread straight into linear memory, no stdio buffer
    Music& m = gMusic;
    if (!readHeader(f, path, m.hdr, m.ch)) {
        fclose(f);
        return false;
    }
    int slots = int(kMusicRingBytes / m.hdr.blockStride);
    if (slots > kMaxSlots) slots = kMaxSlots;
    if (slots < 3) {
        TT_LOG("audio: %s: block %lu B too big for the %u KB ring", path,
               (unsigned long)m.hdr.blockStride, unsigned(kMusicRingBytes / 1024));
        fclose(f);
        return false;
    }
    m.file = f;
    m.filePos = -1;
    m.numSlots = slots;
    m.loop = loop && (m.hdr.flags & kFlagLoop);
    m.cursor = 0;
    m.eof = false;
    m.nextSlot = 0;
    m.ioError = false;
    memset(m.wb, 0, sizeof(m.wb));
    m.fade = fadeInMs > 0 ? 0.0f : 1.0f;
    m.fadePerMs = 0.0f;
    m.fadeInPerMs = fadeInMs > 0 ? 1.0f / float(fadeInMs) : 0.0f;
    m.lastTick = osGetTime();
    setupMusicChannels();
    sendCmd(Cmd::Start);
    if (!m.streaming) {
        TT_LOG("audio: %s: read error", path);
        fclose(m.file);
        m.file = nullptr;
        m.ioError = false;
        return false;
    }
    TT_LOG("audio: music %s (%lu Hz, %u ch, %d slots)", path, (unsigned long)m.hdr.sampleRate,
           unsigned(m.hdr.channels), slots);
    return true;
}

void stopMusic(int fadeMs) {
    if (!gOk || !gMusic.file) return;
    if (fadeMs <= 0) {
        stopMusicNow();
        return;
    }
    gMusic.fadePerMs = 1.0f / float(fadeMs);
    gMusic.fadeInPerMs = 0.0f;
    gMusic.lastTick = osGetTime();
}

bool musicPlaying() { return gOk && gMusic.streaming; }

void setMusicVolume(float v) {
    gMusic.volume = clamp01(v);
    if (gOk && gMusic.file) applyMusicGain();
}

SfxId loadSfx(const char* path) {
    if (!gOk) return kInvalidSfx;
    SfxId id = kInvalidSfx;
    for (SfxId i = 0; i < kMaxSfx; ++i)
        if (!gSfx[i].data) {
            id = i;
            break;
        }
    if (id == kInvalidSfx) {
        TT_LOG("audio: sfx table full (%d)", kMaxSfx);
        return kInvalidSfx;
    }
    FILE* f = fopen(path, "rb");
    if (!f) {
        TT_LOG("audio: cannot open %s", path);
        return kInvalidSfx;
    }
    setvbuf(f, nullptr, _IONBF, 0);
    TsndHeader h;
    TsndChannel ch[2];
    if (!readHeader(f, path, h, ch)) {
        fclose(f);
        return kInvalidSfx;
    }
    if (h.channels != 1 || h.numBlocks != 1) {
        TT_LOG("audio: %s: SFX must be mono, single block", path);
        fclose(f);
        return kInvalidSfx;
    }
    uint32_t bytes = h.blockBytes;
    void* data = linearAlloc(bytes);
    if (!data) {
        TT_LOG("audio: %s: out of linear memory (%lu B)", path, (unsigned long)bytes);
        fclose(f);
        return kInvalidSfx;
    }
    AdpcmCtx ctx{};
    bool ok = fseek(f, long(h.dataOffset), SEEK_SET) == 0 && fread(&ctx, sizeof(ctx), 1, f) == 1 &&
              fseek(f, long(h.dataOffset + kBlockHeaderBytes), SEEK_SET) == 0 &&
              fread(data, bytes, 1, f) == 1;
    fclose(f);
    if (!ok) {
        TT_LOG("audio: %s: truncated", path);
        linearFree(data);
        return kInvalidSfx;
    }
    DSP_FlushDataCache(data, bytes);
    Sfx& s = gSfx[id];
    s.data = data;
    s.bytes = bytes;
    s.numSamples = h.numSamples;
    s.sampleRate = h.sampleRate;
    s.encoding = h.encoding;
    memcpy(s.coefs, ch[0].coefs, sizeof(s.coefs));
    s.ctx = ctx;
    gLinearUsed += bytes;
    return id;
}

void freeSfx(SfxId id) {
    if (id < 0 || id >= kMaxSfx || !gSfx[id].data) return;
    for (int v = 0; v < kNumSfxVoices; ++v)
        if (gVoices[v].sfx == id) stopVoiceIndex(v);
    linearFree(gSfx[id].data);
    gLinearUsed -= gSfx[id].bytes;
    gSfx[id] = Sfx{};
}

VoiceHandle playSfx(SfxId id, float volume, float pan, bool loop) {
    if (!gOk || id < 0 || id >= kMaxSfx || !gSfx[id].data) return kNoVoice;
    const Sfx& s = gSfx[id];

    // Free voice, else steal the oldest one-shot, else the oldest loop.
    int pick = -1;
    for (int v = 0; v < kNumSfxVoices && pick < 0; ++v)
        if (!voiceBusy(v)) pick = v;
    for (int pass = 0; pass < 2 && pick < 0; ++pass) {
        uint32_t oldest = 0;
        for (int v = 0; v < kNumSfxVoices; ++v) {
            if (pass == 0 && gVoices[v].loop) continue;
            uint32_t age = gVoiceSeq - gVoices[v].startSeq;
            if (pick < 0 || age > oldest) {
                pick = v;
                oldest = age;
            }
        }
    }
    if (pick < 0) return kNoVoice; // unreachable with kNumSfxVoices > 0

    Voice& vo = gVoices[pick];
    int chn = voiceChannel(pick);
    if (vo.sfx != kInvalidSfx) ndspChnWaveBufClear(chn);
    ndspChnReset(chn);
    ndspChnSetInterp(chn, NDSP_INTERP_LINEAR);
    ndspChnSetRate(chn, float(s.sampleRate));
    ndspChnSetFormat(chn, ndspFormat(s.encoding));
    if (s.encoding == kEncAdpcm)
        ndspChnSetAdpcmCoefs(chn, reinterpret_cast<u16*>(gSfx[id].coefs));
    voiceMix(pick, volume, pan);

    vo.cur ^= 1;
    ndspWaveBuf& wb = vo.wb[vo.cur];
    memset(&wb, 0, sizeof(wb));
    wb.data_vaddr = s.data;
    wb.nsamples = s.numSamples;
    wb.looping = loop;
    if (s.encoding == kEncAdpcm) {
        vo.ctx[vo.cur].index = s.ctx.ps;
        vo.ctx[vo.cur].history0 = s.ctx.hist0;
        vo.ctx[vo.cur].history1 = s.ctx.hist1;
        wb.adpcm_data = &vo.ctx[vo.cur];
    }
    vo.sfx = id;
    vo.loop = loop;
    vo.gen = uint16_t(vo.gen + 1);
    if (vo.gen == 0) vo.gen = 1;
    vo.startSeq = ++gVoiceSeq;
    ndspChnWaveBufAdd(chn, &wb);
    return (VoiceHandle(vo.gen) << 8) | VoiceHandle(pick + 1);
}

void stopVoice(VoiceHandle h) {
    if (!gOk) return;
    int v = voiceFromHandle(h);
    if (v >= 0) stopVoiceIndex(v);
}

bool voicePlaying(VoiceHandle h) { return gOk && voiceFromHandle(h) >= 0; }

void setVoiceVolume(VoiceHandle h, float volume, float pan) {
    if (!gOk) return;
    int v = voiceFromHandle(h);
    if (v >= 0) voiceMix(v, volume, pan);
}

int freeVoices() {
    if (!gOk) return 0;
    int n = 0;
    for (int v = 0; v < kNumSfxVoices; ++v)
        if (!voiceBusy(v)) ++n;
    return n;
}

void setMasterVolume(float v) {
    if (gOk) ndspSetMasterVol(clamp01(v));
}

void update() {
    if (!gOk) return;
    Music& m = gMusic;
    if (m.ioError) {
        m.ioError = false;
        TT_LOG("audio: music read error, stream stopped");
    }
    if (m.file && m.fadeInPerMs > 0.0f) {
        u64 now = osGetTime();
        float dt = float(now - m.lastTick);
        m.lastTick = now;
        m.fade += dt * m.fadeInPerMs;
        if (m.fade >= 1.0f) {
            m.fade = 1.0f;
            m.fadeInPerMs = 0.0f;
        }
        applyMusicGain();
    }
    if (m.file && m.fadePerMs > 0.0f) {
        u64 now = osGetTime();
        float dt = float(now - m.lastTick);
        m.lastTick = now;
        m.fade -= dt * m.fadePerMs;
        if (m.fade <= 0.0f) {
            m.fade = 0.0f;
            stopMusicNow();
            return;
        }
        applyMusicGain();
    }
    // A finished non-looping track: release the file on the main thread.
    if (m.file && !m.streaming) {
        stopMusicNow();
    }
}

size_t linearBytesUsed() { return gLinearUsed; }

uint32_t musicUnderruns() { return gMusic.underruns; }

} // namespace tt::audio
