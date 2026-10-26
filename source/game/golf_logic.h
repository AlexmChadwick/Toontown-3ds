#pragma once

#include <cstdint>

namespace tt::golf {

struct Ball {
    float x = 0, y = 0, z = 0.25f;
    float vx = 0, vy = 0, vz = 0;
    bool resting = true;
};

enum class Surface : uint8_t { Grass = 0, Hard = 2, Cup = 3, Slick = 4 };
struct Triangle { float p[9]; Surface surface; };

// GPH1 is produced by tools/pipeline/bake_golf_physics.py.  It contains only
// collision triangles: unlike TTM it has no textures or render data.
class Physics {
public:
    Physics() = default;
    ~Physics();
    Physics(const Physics&) = delete;
    bool load(const char* path);
    void clear();
    bool addTriangle(const Triangle& triangle); // host tests and generated fixtures
    void simulate(Ball& ball, uint16_t* touchedCup = nullptr);
    uint32_t triangleCount() const { return count_; }

private:
    Triangle* triangles_ = nullptr;
    uint32_t count_ = 0, capacity_ = 0;
};

constexpr int kMaxMovieFrames = 360;
struct Movie {
    Ball frames[kMaxMovieFrames] = {};
    uint16_t count = 0;
    uint16_t ballInFrame = 0;
};

// Deterministic 90 Hz approximation of the stock PhysicsWorldBase ball flight.
void swing(Ball& ball, float power, float dirX, float dirY);
void step(Ball& ball, float dt);
bool inCup(const Ball& ball, float cupX, float cupY, float cupZ);
void recordSwing(Movie& movie, Physics& physics, const Ball& start, float power, float dirX, float dirY,
                 float cupX, float cupY, float cupZ);

} // namespace tt::golf
