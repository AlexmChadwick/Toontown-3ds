#pragma once
namespace tt::ui {
struct RacingView {
    float curve[64][2] = {};
    float racers[4][2] = {};
    char names[4][32] = {};
    int count = 0;
    float minX = 0, minY = 0, spanX = 1, spanY = 1;
};
} // namespace tt::ui
