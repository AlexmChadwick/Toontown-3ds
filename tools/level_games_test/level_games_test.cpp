#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "world/level_games.h"

int main() {
    using namespace tt::world::games_logic;

    // 1. Laser Fields
    {
        // 8x8 grid on 16.0 x 16.0 scale
        int8_t tx = -1, ty = -1;
        bool in = calcLaserTile(1.0f, 1.0f, 16.0f, 16.0f, 8, 8, &tx, &ty);
        assert(in && tx == 0 && ty == 0);

        in = calcLaserTile(5.0f, 7.0f, 16.0f, 16.0f, 8, 8, &tx, &ty);
        assert(in && tx == 2 && ty == 3);

        in = calcLaserTile(15.9f, 15.9f, 16.0f, 16.0f, 8, 8, &tx, &ty);
        assert(in && tx == 7 && ty == 7);

        // Outside grid
        in = calcLaserTile(-0.5f, 5.0f, 16.0f, 16.0f, 8, 8, &tx, &ty);
        assert(!in && tx == -1);

        in = calcLaserTile(16.5f, 5.0f, 16.0f, 16.0f, 8, 8, &tx, &ty);
        assert(!in && tx == -1);

        // Hazard symbol checks
        assert(!isLaserHazard(0));  // blank
        assert(!isLaserHazard(1));  // number 1
        assert(!isLaserHazard(9));  // number 9
        assert(isLaserHazard(12));  // hidden bomb
        assert(isLaserHazard(13));  // bomb
        std::printf("pass: laser field tile indexing and hazard detection\n");
    }

    // 2. Crates
    {
        // Normal side detection (0: +X, 1: -X, 2: +Y, 3: -Y)
        assert(calcCratePushSide(1.0f, 0.0f) == 0);
        assert(calcCratePushSide(-1.0f, 0.0f) == 1);
        assert(calcCratePushSide(0.0f, 1.0f) == 2);
        assert(calcCratePushSide(0.0f, -1.0f) == 3);

        // Slightly off-axis contact
        assert(calcCratePushSide(0.95f, 0.1f) == 0);
        assert(calcCratePushSide(-0.95f, -0.05f) == 1);
        assert(calcCratePushSide(0.1f, 0.95f) == 2);
        assert(calcCratePushSide(-0.1f, -0.95f) == 3);

        // Zero / degenerate normal
        assert(calcCratePushSide(0.0f, 0.0f) == -1);

        // Movement lerp over 1.5s
        const float start[3] = {10.0f, 20.0f, 0.0f};
        const float end[3] = {13.0f, 20.0f, 0.0f};
        float cur[3] = {};

        calcCrateLerp(start, end, 0.0f, 1.5f, cur);
        assert(std::fabs(cur[0] - 10.0f) < 1e-5f);

        calcCrateLerp(start, end, 0.75f, 1.5f, cur);
        assert(std::fabs(cur[0] - 11.5f) < 1e-5f);

        calcCrateLerp(start, end, 1.5f, 1.5f, cur);
        assert(std::fabs(cur[0] - 13.0f) < 1e-5f);

        calcCrateLerp(start, end, 3.0f, 1.5f, cur);
        assert(std::fabs(cur[0] - 13.0f) < 1e-5f);
        std::printf("pass: crate push side classification and linear interpolation\n");
    }

    // 3. Mazes
    {
        int rotA[4] = {};
        int rotB[4] = {};
        shuffleMazeRotations(12345u, rotA);
        shuffleMazeRotations(12345u, rotB);

        // Determinism: same seed gives identical rotations
        for (int i = 0; i < 4; ++i) {
            assert(rotA[i] == rotB[i]);
            assert(rotA[i] == 0 || rotA[i] == 90 || rotA[i] == 180 || rotA[i] == 270);
        }

        // Different seed
        int rotC[4] = {};
        shuffleMazeRotations(99999u, rotC);
        bool anyDiff = false;
        for (int i = 0; i < 4; ++i) {
            if (rotA[i] != rotC[i]) anyDiff = true;
        }
        assert(anyDiff);
        std::printf("pass: maze section rotation RNG determinism\n");
    }

    // 4. Mole Fields
    {
        MolePopup sched[64];
        const int count = calcMoleSchedule(42u, 60.0f, 16, sched, 64);
        assert(count > 10);

        // Verify monotonic start times and valid holes
        int normalCount = 0;
        int bombCount = 0;
        for (int i = 0; i < count; ++i) {
            assert(sched[i].hole < 16);
            assert(sched[i].upTime >= 0.5f);
            assert(sched[i].stayTime >= 3.0f);
            assert(sched[i].downTime >= 0.5f);
            if (i > 0) assert(sched[i].start > sched[i - 1].start);
            if (sched[i].type == 0) ++normalCount;
            else if (sched[i].type == 1) ++bombCount;
        }
        // Ratio should have both normals and bombs
        assert(normalCount > bombCount);
        assert(bombCount > 0);

        // Elevation timing
        // Popup at start = 10.0, up = 1.0, stay = 4.0, down = 1.0
        assert(calcMoleElevation(5.0f, 10.0f, 1.0f, 4.0f, 1.0f) == 0.0f);
        assert(std::fabs(calcMoleElevation(10.5f, 10.0f, 1.0f, 4.0f, 1.0f) - 0.5f) < 1e-4f);
        assert(calcMoleElevation(12.0f, 10.0f, 1.0f, 4.0f, 1.0f) == 1.0f);
        assert(std::fabs(calcMoleElevation(15.5f, 10.0f, 1.0f, 4.0f, 1.0f) - 0.5f) < 1e-4f);
        assert(calcMoleElevation(16.5f, 10.0f, 1.0f, 4.0f, 1.0f) == 0.0f);
        std::printf("pass: mole schedule decay algorithm and height animation\n");
    }

    // 5. Golf Greens
    {
        float p0[3], p1[3], p2[3], p3[3];
        calcGolfSpot(0, p0);
        calcGolfSpot(1, p1);
        calcGolfSpot(2, p2);
        calcGolfSpot(3, p3);

        assert(p0[0] == 3.0f && p0[1] == 13.0f && p0[2] == 0.0f);
        assert(p1[0] == 6.0f && p1[1] == 13.0f && p1[2] == 0.0f);
        assert(p2[0] == -3.0f && p2[1] == 13.0f && p2[2] == 0.0f);
        assert(p3[0] == -6.0f && p3[1] == 13.0f && p3[2] == 0.0f);
        std::printf("pass: golf tee-off spots\n");
    }

    std::printf("\nALL LEVEL GAMES HOST TESTS PASSED!\n");
    return 0;
}
