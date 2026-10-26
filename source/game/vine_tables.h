// Generated from ref/open-toontown/toontown/minigame/VineGameGlobals.py CourseSections (10 sections of 5 vines).
// Each vine: {cable length, baseAngle in degrees, swing period in seconds, spider period (0 = none)}.
// DistributedVineGame.setupVineCourse builds the course from the server's vineSections.
#pragma once

namespace tt::game::vine {

struct CourseVine {
    float length;
    float baseAngle;
    float period;
    float spiderPeriod;
};

constexpr int kNumCourseSections = 10;
constexpr int kVinesPerSection = 5;
constexpr CourseVine kCourseSections[kNumCourseSections][kVinesPerSection] = {
    {
        {20.0f, 30.0f, 4.0f, 0.0f},
        {19.0f, 39.0f, 3.1f, 0.0f},
        {18.0f, 41.0f, 4.0f, 0.0f},
        {19.0f, 38.0f, 3.2f, 0.0f},
        {20.0f, 30.0f, 6.0f, 0.0f},
    },
    {
        {20.0f, 30.0f, 3.0f, 0.0f},
        {18.0f, 40.0f, 4.1f, 0.0f},
        {19.0f, 31.0f, 5.1f, 0.0f},
        {18.0f, 41.0f, 4.2f, 0.0f},
        {20.0f, 30.0f, 5.0f, 0.0f},
    },
    {
        {20.0f, 30.0f, 3.0f, 0.0f},
        {15.0f, 39.0f, 4.1f, 0.0f},
        {19.0f, 29.0f, 5.0f, 0.0f},
        {16.0f, 38.0f, 4.2f, 0.0f},
        {20.0f, 30.0f, 5.0f, 0.0f},
    },
    {
        {20.0f, 30.0f, 3.0f, 0.0f},
        {18.0f, 36.0f, 4.1f, 0.0f},
        {19.0f, 30.0f, 5.0f, 9.0f},
        {18.0f, 38.0f, 4.2f, 0.0f},
        {20.0f, 30.0f, 5.0f, 0.0f},
    },
    {
        {20.0f, 30.0f, 3.0f, 0.0f},
        {18.0f, 15.0f, 4.1f, 0.0f},
        {19.0f, 30.0f, 5.0f, 11.0f},
        {18.0f, 16.0f, 4.2f, 0.0f},
        {20.0f, 30.0f, 5.0f, 0.0f},
    },
    {
        {20.0f, 30.0f, 3.0f, 0.0f},
        {18.0f, 11.0f, 4.1f, 0.0f},
        {15.0f, 12.0f, 5.0f, 0.0f},
        {18.0f, 16.0f, 4.2f, 0.0f},
        {20.0f, 30.0f, 5.0f, 0.0f},
    },
    {
        {20.0f, 30.0f, 3.0f, 0.0f},
        {15.0f, 39.0f, 4.1f, 13.0f},
        {19.0f, 29.0f, 5.0f, 0.0f},
        {16.0f, 38.0f, 4.2f, 0.0f},
        {20.0f, 30.0f, 5.0f, 0.0f},
    },
    {
        {20.0f, 30.0f, 3.0f, 0.0f},
        {18.0f, 26.0f, 4.1f, 9.0f},
        {19.0f, 30.0f, 5.0f, 0.0f},
        {18.0f, 28.0f, 4.2f, 12.0f},
        {20.0f, 30.0f, 5.0f, 0.0f},
    },
    {
        {20.0f, 30.0f, 3.0f, 0.0f},
        {15.0f, 26.0f, 4.1f, 9.0f},
        {19.0f, 30.0f, 5.0f, 0.0f},
        {15.0f, 28.0f, 4.2f, 12.0f},
        {20.0f, 30.0f, 5.0f, 0.0f},
    },
    {
        {15.0f, 50.0f, 4.0f, 0.0f},
        {15.0f, 40.0f, 4.1f, 0.0f},
        {19.0f, 40.0f, 5.0f, 0.0f},
        {19.0f, 28.0f, 4.2f, 0.0f},
        {20.0f, 30.0f, 5.0f, 0.0f},
    },
};

} // namespace tt::game::vine
