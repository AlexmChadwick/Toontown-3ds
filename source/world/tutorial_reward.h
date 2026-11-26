// Movie.playTutorialReward (DistributedBattleTutorial.playReward): the Toontorial's Flunky battle
// ends without the victory dance. Two TTDialogs credit the first Throw and Squirt points over the
// RewardPanel, the ToonTask frame shows the first task done, then the camera moves to the street,
// a Tutorial Tom built by the movie teleports in beside the Toon, congratulates it, points it to
// Toon HQ and teleports out; handleRewardDone sends rewardDone and the Toon turns to heading 270.
// Plain state machine; main.cpp stages it (camera, Toon pose, the street Tom, the panel dialog).
#pragma once

#include <cstdint>

#include "world/toon_anim.h"

namespace tt::world {

class TutorialReward {
public:
    // Client-side id of the movie's own Tutorial Tom (no server object): above every doId.
    static constexpr uint32_t kTomId = 0xFFFFFF01u;
    // TTLocalizer.NPCToonNames[20000]; dnaList ('dll', 'ms', 'm', 'm', 7, 0, 7, 7, 2, 6, 2, 6, 2, 16)
    // as a ToonDNA net string.
    static const char* tomName() { return "Tutorial Tom"; }
    static const uint8_t* tomDna();
    // render positions of the street scene (playTutorialReward_3's track1)
    static constexpr float kCamPos[3] = {34.0f, 19.88f, 3.48f}, kCamH = -90.0f, kCamP = -2.36f;
    static constexpr float kToonPos[4] = {40.31f, 22.0f, -0.47f, 150.0f};
    static constexpr float kTomPos[4] = {40.29f, 17.9f, -0.47f, 11.31f};

    void start();
    void stop() { step_ = Step::Off; }
    bool active() const { return step_ != Step::Off && step_ != Step::Done; }
    // The movie is over: send rewardDone (handleRewardDone), then stop().
    bool done() const { return step_ == Step::Done; }
    void update(float dt);
    // The player's acknowledgement: a dialog's OK, or Tom's balloon page button.
    void advance();

    // MovieTutorialReward1 / 2 over the RewardPanel (the bottom screen), else null.
    const char* dialog() const;
    // The street scene: the camera and the Toon at the movie's render positions.
    bool staged() const { return step_ >= Step::TomIn && step_ <= Step::TomOut; }
    bool tomShown() const { return staged() && !(step_ == Step::TomOut && t_ > kTeleportOutSec); }
    ToonAnim tomAnim(float* rate) const;
    // What Tom says (MovieTutorialReward3, 4 wait for the page button; 5 times out), else null.
    const char* tomLine() const;

private:
    enum class Step : uint8_t { Off, Dialog1, Dialog2, Quest, TomIn, Say3, Say4, Say5, TomOut, Done };
    static constexpr float kQuestSec = 3.0f;       // Wait 1, the quest frame's progress, Wait 1
    static constexpr float kTeleportInSec = 1.517f; // TeleportIn, then neutral
    static constexpr float kSay5Sec = 1.5f;        // Wait 0.5, MovieTutorialReward5, Wait 1.0
    static constexpr float kTeleportOutSec = 1.9f; // TeleportOut: the 'teleport' clip (hole) ...
    static constexpr float kAfterOutSec = 1.0f;    // ... then Wait 1.0
    void go(Step s) {
        step_ = s;
        t_ = 0.0f;
    }
    Step step_ = Step::Off;
    float t_ = 0.0f;
};

} // namespace tt::world
