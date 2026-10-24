// Allocation-free playback math, shared by the device and host conformance test.
#pragma once
#include <cmath>
#include <cstdint>
namespace tt::world {
enum class ClipMode : uint8_t { Loop, Once, Hold };
struct ClipSample { uint32_t a=0,b=0; float blend=0; };
inline ClipSample clipSample(float time, float fps, uint32_t frames, bool loop, float period=0) {
    if (frames<=1 || !std::isfinite(time) || !std::isfinite(fps) || fps<=0) return {};
    double f=static_cast<double>(time)*fps;
    if (loop && period>0 && std::isfinite(period)) {
        float t=std::fmod(time,period); if(t<0) t+=period;
        f=t*fps;
        const float last=(frames-1)/fps;
        if(frames>1 && t>=last && period>last) f=frames-1+(t-last)/(period-last);
    } else if (loop) { f=std::fmod(f,static_cast<double>(frames)); if (f<0) f+=frames; }
    else { if (f<0) f=0; if (f>frames-1) f=static_cast<float>(frames-1); }
    // A mismatched external period must never address beyond the file's frames.
    if (!std::isfinite(f)) return {};
    if (loop) f=std::fmod(f,static_cast<double>(frames));
    const auto a=static_cast<uint32_t>(f);
    return {a,loop?(a+1)%frames:(a+1<frames?a+1:a),static_cast<float>(f-a)};
}
struct ToonPlayback {
    ClipMode mode=ClipMode::Loop;
    float elapsed=0, rate=1, start=0, end=-1;
    uint32_t token=0;
    bool complete=false;
    void begin(ClipMode m,float first,float last,float speed=1,float seek=0) {
        mode=m; start=std::isfinite(first)?first:0; end=std::isfinite(last)?last:-1; rate=std::isfinite(speed)?speed:0;
        elapsed=std::isfinite(seek)&&seek>0?seek:0; complete=false; ++token;
    }
    void update(float dt) { if (std::isfinite(dt)&&dt>0) elapsed+=dt; }
    float sample(float duration) const {
        const float last=end>=0?end:duration;
        const float lo=start<last?start:last, hi=start<last?last:start;
        if (mode==ClipMode::Hold) return start;
        const bool reverse=rate<0 || last<start;
        float value=(reverse?hi:lo)+elapsed*std::fabs(rate)*(reverse?-1:1);
        if (mode==ClipMode::Loop) {
            const float span=hi-lo;
            if (span<=0) return lo;
            value=std::fmod(value-lo,span); if (value<0) value+=span;
            return value+lo;
        }
        return value<lo?lo:value>hi?hi:value;
    }
    bool finished(float duration) const {
        if (mode!=ClipMode::Once) return false;
        const float last=end>=0?end:duration;
        return std::fabs(rate)>0 && elapsed*std::fabs(rate)>=std::fabs(last-start);
    }
};
// Same 16-bit centisecond nearest-epoch rule as Astron movement timestamps.
inline float clipElapsed(uint32_t nowMs,uint32_t sampleMs) {
    const int32_t delta=static_cast<int32_t>(nowMs-sampleMs);
    return delta>0?delta/1000.0f:0.0f;
}
} // namespace tt::world
