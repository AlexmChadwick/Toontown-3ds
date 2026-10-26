#include <cassert>
#include <cstring>

#include "game/photo_logic.h"

int main() {
    tt::photogame::Simulation first;
    tt::photogame::Simulation second;
    first.init(1, 2000, 0x12345678);
    second.init(1, 2000, 0x12345678);
    for (int i = 0; i < tt::photogame::kOnscreenAssignments; ++i) {
        assert(first.assignments[i].subjectIndex == second.assignments[i].subjectIndex);
        assert(!std::strcmp(first.assignments[i].poseName, second.assignments[i].poseName));
    }
    tt::photogame::SubjectPose pose;
    first.getSubjectPose(first.assignments[0].subjectIndex, 0.0f, pose);
    assert(pose.poseName[0]);
    // A shot must consume exactly one frame of film, and an AI score only improves an assignment.
    const int film = first.filmUsed[0];
    first.evaluateShot(0, 0.0f, 50.0f, 27.0f, -87.8752f, -0.378549f, 0.0f);
    assert(first.filmUsed[0] == film + 1);
    assert(first.submitAIScore(42, 0, 0, 3.0f));
    assert(!first.submitAIScore(42, 0, 0, 2.0f));
    return 0;
}
