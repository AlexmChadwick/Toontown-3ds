#pragma once

namespace tt::golf {

struct Ball {
    float x = 0, y = 0, z = 0.25f;
    float vx = 0, vy = 0, vz = 0;
    bool resting = true;
};

// Deterministic 90 Hz approximation of the stock PhysicsWorldBase ball flight.
void swing(Ball& ball, float power, float dirX, float dirY);
void step(Ball& ball, float dt);
bool inCup(const Ball& ball, float cupX, float cupY, float cupZ);

} // namespace tt::golf
