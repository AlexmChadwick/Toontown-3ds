// Bust-a-Cog: the country club golf green's puzzle board (DistributedGolfGreenGame's client game
// and GameSprite3D). The AI sends each board (startBoard: balls on a 9 x 15 hex grid and the
// attack pattern of colours to shoot); the client plays it and answers requestBoard(success).
// The board lies flat on the green (frame P 90, 10 ahead and 2 up), seen from __setCamera's
// fixed camera. Owned by LevelGames (world/level_games.*).
#pragma once

#include <cstdint>

#ifdef __3DS__
#include "audio/audio.h"
#include "render/model.h"
#include "render/renderer.h"
#include "world/asset_batch.h"
#include <citro3d.h>
#endif

namespace tt::world {

#ifdef __3DS__

class BustACog {
public:
    static constexpr int kDimX = 9, kDimZ = 15;
    static constexpr int kMaxSprites = 160;
    static constexpr int kMaxEffects = 48;
    static constexpr int kModelCount = 10; // GameSprite3D's ball models (bac_ball_*)

    void load(BasicAssetBatch<32>& batch);
    void unload();

    // __startGame: the board, shooter and holes show and the camera moves over the board.
    void begin();
    // __leaveGame (Exit for Now, or signalDone): everything goes; the caller sends leaveGame.
    void end();
    bool active() const { return active_; }

    // startBoard: balls (posX, posZ, typeIndex) and the attack pattern.
    void startBoard(const uint8_t (*balls)[3], int numBalls, const uint8_t* pattern, int patternLen);
    void boardCleared() { if (running_) doFail(); } // another Toon cleared our board
    bool running() const { return running_; }
    void helpOthers() { if (running_) giftId_ = 7; } // a bomb ball for the next shot

    // One frame: aim is -1..1 (left / right), fire = the shot button. Returns true once per
    // finished board with the requestBoard argument in *success.
    bool update(float dt, float aim, bool fire, uint8_t* success);
    // The ten-second count to the next row (timerLabel), 0 while no board runs.
    int countdown() const { return running_ || countDownRunning_ ? countDown_ : 0; }

    // `entity` is the green's world transform (LevelDrawData matrix).
    void draw(render::Renderer& renderer, const C3D_Mtx& entity) const;
    // __setCamera: the camera at (0, 12, 27) under the green, H 180, P -70, vertical fov 46.8.
    static void camera(const C3D_Mtx& entity, float eye[3], float target[3]);
    static constexpr float kCameraFovY = 46.8265f;

private:
    struct Sprite {
        bool used = false;
        bool inFrame = false;  // a child of the frame (the loaded shot), else of the sprite node
        float x = 0, z = 0, prevX = 0, prevZ = 0;
        float velX = 0, velZ = 0;
        bool hasAcc = false;
        float accX = 0, accZ = 0;
        bool isActive = false, canCollide = true, foundation = false, breakable = true;
        int8_t colorType = 0;
        int8_t model = 0;      // bac_ball_* index
        float color[4] = {1, 1, 1, 1};
        bool multiColor = false;
        uint8_t mcList[4] = {}, mcLen = 0, mcIndex = 0, mcNext = 1;
        float mcLevel = 0, mcStep = 0;
        int8_t giftId = -1;
        int8_t gridX = -1, gridZ = -1;
        // warningBump / shake: nodeObj's offset along the board, piecewise linear from bumpStart.
        float bumpStart = -1;
        uint8_t bumpKeys = 0;
        float bumpT[5] = {}, bumpV[5] = {};
    };
    struct Effect {
        bool used = false;
        bool wild = false;      // wildEffect: grows to 1.5 and fades out in place
        float t = 0, dur = 1;
        float pos[3] = {};      // deathEffect: world start; wild: sprite-node x, z
        float vel[3] = {};
        float tilt = 0;         // nodeObj P
        int8_t model = 0;
        float from[4] = {}, to[4] = {};
    };

    void setBallType(Sprite& s, int type, bool solidOverride);
    int addSprite(bool found, int color);
    void killSprite(int idx);
    void clearGrid();
    void placeIntoGrid(int idx, int x, int z);
    void stickInGrid(int idx, bool force);
    bool findGrid(float x, float z, bool force, int* cx, int* cz) const;
    bool hasNeighbor(int x, int z) const;
    int colorTypeAt(int x, int z) const;
    int breakableAt(int x, int z) const;
    bool matchWild(int x, int z, int color) const;
    void fillMatchList(int x, int z);
    void addNeighbors(int x, int z, bool breakableOnly);
    void clearMatchList(int typeClear);
    void clearFloaters();
    void explodeBombs();
    void checkForTooLow();
    void doFail();
    void finishBoard(bool success);
    void collide(int a, int b);
    void colorGrid();
    void bump(Sprite& s, bool warning);
    float bumpAt(const Sprite& s) const;
    void runColor(Sprite& s);
    void deathEffect(int idx);
    void wildEffect(float x, float z);
    void play(int sfx) const;
    void spriteNodeMtx(const C3D_Mtx& frame, C3D_Mtx* out) const;
    void drawBall(render::Renderer& renderer, const C3D_Mtx& parent, float x, float z, int model,
                  const float color[4], float bumpZ, float scale = 1.0f) const;

    bool active_ = false, running_ = false, finished_ = false, tooLow_ = false;
    float now_ = 0;
    Sprite sprites_[kMaxSprites];
    int order_[kMaxSprites] = {}; // self.sprites, in insertion order
    int numOrder_ = 0;
    int8_t grid_[kDimX][kDimZ];
    float gridColor_[kDimX][kDimZ];  // the hole's colour scale (alpha 0 = outColor)
    Effect effects_[kMaxEffects];
    int control_ = -1;
    Sprite standby_;
    int standbyHold_ = -1;  // holdType
    uint8_t pattern_[16] = {};
    int patternLen_ = 0, attackCounter_ = 0;
    int notch_ = 0;
    float notchShown_ = 0, notchFrom_ = 0, rollT_ = -1; // lerpSpritePos over 0.5 s
    int countDown_ = 10;
    float countClock_ = 0;
    bool countDownRunning_ = false;
    int giftId_ = -1;
    int ballLoaded_ = 0;
    float aimRadian_ = -1.5707963f;
    bool hasChanged_ = false, colorGridFlag_ = false;
    int matchList_[kDimX * kDimZ][2] = {};
    int numMatch_ = 0;
    int closestX_ = 0, closestZ_ = 0;
    float failAt_ = -1;     // doFail's failBoard 1 s later
    bool pendingRequest_ = false;
    uint8_t pendingSuccess_ = 0;
    uint32_t rng_ = 0x2545F491u;
    float random();

    render::Model balls_[kModelCount];
    render::Model hole_, background_, shooter_, shooterMoving_;
    enum Sfx { kFire, kLand, kBurst, kBomb, kLose, kWin, kMove, kSfxCount };
    audio::SfxId sfx_[kSfxCount] = {};
};

#endif // __3DS__

} // namespace tt::world
