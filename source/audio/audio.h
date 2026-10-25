// Audio: NDSP output, one streamed music track, a small pool of SFX voices.
//
// Data: .tsnd files baked by tools/pipeline/bake_audio.py (format spec in
// tools/pipeline/audio_tsnd.py). Music is DSP-ADPCM (decoded by the DSP itself,
// zero CPU), SFX are PCM16 mono.
//
// NDSP channel map: 0-1 music (L, R), 2-9 SFX voices. 10-23 are free.
//
// Memory (linear heap, counted against the 2 MB audio sub-budget in AGENTS.md):
//   music ring   kMusicRingBytes (128 KB) allocated once in init(); with the
//                baked 32 kHz stereo ADPCM track that is 7 blocks x 14336
//                samples = ~3.1 s of buffered audio (~16 KB per block).
//   SFX          each loaded sound fully resident (PCM16: 2 bytes/sample).
//
// Threads: one streaming thread (prio 0x30, core 0, 16 KB stack) owns the music
// wavebuf queue and does all music file reads. It never allocates; the file is
// opened/closed and the ring allocated on the main thread. The NDSP frame
// callback is not used (no file IO in the DSP callback).
//
// If ndspInit() fails (real hardware without sdmc:/3ds/dspfirm.cdc, which is
// dumped by the "DSP1" homebrew), init() logs it and every call below becomes
// a cheap no-op: the game runs silently, it never crashes.
//
// HOME menu / sleep: libctru's NDSP installs its own APT hook and pauses/resumes
// the DSP (and our queued wavebufs) by itself. While suspended no buffers finish,
// so the stream thread simply idles. Nothing to do in main.cpp.
//
// All functions are main-thread only.
#pragma once

#include <cstddef>
#include <cstdint>

namespace tt::audio {

using SfxId = int;
constexpr SfxId kInvalidSfx = -1;
using VoiceHandle = uint32_t; // 0 = none
constexpr VoiceHandle kNoVoice = 0;

constexpr int kMusicChannelL = 0;
constexpr int kMusicChannelR = 1;
constexpr int kSfxFirstChannel = 2;
constexpr int kNumSfxVoices = 8;
// Resident SFX: ambient 4, main 7, transitions 4, facility props 15 + room games 13, battle 6,
// boat 4, fishing, interior life (about 57 at most); each slot only holds a pointer until loaded.
constexpr int kMaxSfx = 64;
constexpr size_t kMusicRingBytes = 128 * 1024; // AGENTS.md: music ring <= 256 KB

// Returns false when audio is unavailable (the game continues silently).
bool init();
void shutdown();
bool available();

// Music. path e.g. "romfs:/baked/audio/ttc_nbrhood.tsnd". loop=true uses the
// file's loop points (whole track for the baked music). Replaces any current
// track immediately. Returns false on a missing/invalid file (logged).
// fadeInMs > 0 ramps the volume up from silence over that time (zone-change crossfade:
// the old track is faded out with stopMusic(fadeMs) first, there is one music stream).
bool playMusic(const char* path, bool loop = true, int fadeInMs = 0);
// fadeMs = 0 stops now; otherwise fades out linearly, then stops.
void stopMusic(int fadeMs = 0);
bool musicPlaying();
void setMusicVolume(float v); // 0..1, default 0.8 (upstream base.playMusic volume)

// SFX. Loads the whole sound into linear memory. Returns kInvalidSfx on error.
SfxId loadSfx(const char* path);
void freeSfx(SfxId id); // stops any voice playing it
// volume 0..1, pan -1 (left) .. +1 (right). loop=true repeats until stopVoice()
// (used for the upstream footstep loops). With all voices busy the oldest
// non-looping voice is stolen (or the oldest voice if all loop).
VoiceHandle playSfx(SfxId id, float volume = 1.0f, float pan = 0.0f, bool loop = false);
void stopVoice(VoiceHandle v);
bool voicePlaying(VoiceHandle v);
void setVoiceVolume(VoiceHandle v, float volume, float pan = 0.0f);

// SFX voices currently idle (0..kNumSfxVoices). Ambient one-shots use it to stay out of the way
// of UI/footstep voices: playSfx() steals the oldest voice when none is free.
int freeVoices();

void setMasterVolume(float v); // 0..1 (ndspSetMasterVol)

// Once per frame on the main thread: advances fades, reports stream errors.
void update();

// For the Debug tab.
size_t linearBytesUsed();
uint32_t musicUnderruns(); // times the stream ran dry and had to restart

} // namespace tt::audio
