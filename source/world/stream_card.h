#pragma once
namespace tt::world {
enum class StreamCardPhase { Idle, Cover, Streaming, Ready, Retire };
// Called once at the existing FrameBegin fence. Arrival work gets one final
// covered frame; card GPU resources can retire only at the following fence.
inline bool retireStreamCard(StreamCardPhase& phase) {
    if (phase == StreamCardPhase::Ready) {
        phase = StreamCardPhase::Retire;
        return false;
    }
    if (phase != StreamCardPhase::Retire) return false;
    phase = StreamCardPhase::Idle;
    return true;
}
}
