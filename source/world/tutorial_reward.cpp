#include "world/tutorial_reward.h"

#include "core/log.h"

namespace tt::world {

const uint8_t* TutorialReward::tomDna() {
    // 't', head dll, torso ms, legs m, male, shirt 2/6, sleeve 2/6, shorts 2/16, arm 7, glove 0, leg 7, head 7
    static const uint8_t kDna[15] = {'t', 3, 1, 1, 1, 2, 6, 2, 6, 2, 16, 7, 0, 7, 7};
    return kDna;
}

void TutorialReward::start() {
    go(Step::Dialog1);
    TT_LOG("tutorial: reward movie start");
}

void TutorialReward::update(float dt) {
    if (!active()) return;
    t_ += dt;
    switch (step_) {
    case Step::Quest:
        if (t_ >= kQuestSec) go(Step::TomIn);
        break;
    case Step::TomIn:
        if (t_ >= kTeleportInSec) go(Step::Say3);
        break;
    case Step::Say5:
        if (t_ >= kSay5Sec) go(Step::TomOut);
        break;
    case Step::TomOut:
        if (t_ >= kTeleportOutSec + kAfterOutSec) {
            go(Step::Done);
            TT_LOG("tutorial: reward movie done");
        }
        break;
    default:
        break;
    }
}

void TutorialReward::advance() {
    switch (step_) {
    case Step::Dialog1: go(Step::Dialog2); break;
    case Step::Dialog2: go(Step::Quest); break;
    case Step::Say3: go(Step::Say4); break;
    case Step::Say4: go(Step::Say5); break;
    default: break;
    }
}

const char* TutorialReward::dialog() const {
    if (step_ == Step::Dialog1) return "You received 1 Throw point! When you get 10, you will get a new gag!";
    if (step_ == Step::Dialog2) return "You received 1 Squirt point! When you get 10, you will get a new gag!";
    return nullptr;
}

ToonAnim TutorialReward::tomAnim(float* rate) const {
    if (rate) *rate = 1.0f;
    if (step_ == Step::TomIn) {
        // TeleportIn rises out of the hole: the 'teleport' clip backwards
        if (rate) *rate = -1.0f;
        return kAnimTeleport;
    }
    if (step_ == Step::TomOut) return kAnimTeleport;
    return kAnimNeutral;
}

const char* TutorialReward::tomLine() const {
    switch (step_) {
    case Step::Say3: return "Good job! You completed your first ToonTask!";
    case Step::Say4: return "Go to Toon Headquarters for your reward!";
    case Step::Say5: return t_ >= 0.5f ? "Have fun!" : nullptr;
    default: return nullptr;
    }
}

} // namespace tt::world
