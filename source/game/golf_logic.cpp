#include "game/golf_logic.h"

#include <cmath>

namespace tt::golf {

void swing(Ball& ball, float power, float dirX, float dirY) {
    const float length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length < 0.001f) return;
    dirX /= length; dirY /= length;
    ball.vx = dirX * power * 3.0f;
    ball.vy = dirY * power * 3.0f;
    ball.vz = power * 0.45f;
    ball.resting = false;
}

void step(Ball& ball, float dt) {
    if (ball.resting) return;
    ball.vz -= 25.0f * dt;
    ball.x += ball.vx * dt;
    ball.y += ball.vy * dt;
    ball.z += ball.vz * dt;
    const float drag = std::pow(0.985f, dt * 90.0f);
    ball.vx *= drag; ball.vy *= drag;
    if (ball.z <= 0.25f) {
        ball.z = 0.25f;
        if (std::fabs(ball.vz) > 0.25f) ball.vz = -ball.vz * 0.25f;
        else { ball.vx *= 0.88f; ball.vy *= 0.88f; ball.vz = 0.0f; }
        if (std::fabs(ball.vx) + std::fabs(ball.vy) < 0.03f) {
            ball.vx = ball.vy = 0.0f; ball.resting = true;
        }
    }
}

bool inCup(const Ball& ball, float cupX, float cupY, float cupZ) {
    const float dx = ball.x - cupX, dy = ball.y - cupY, dz = ball.z - cupZ;
    return dx * dx + dy * dy + dz * dz <= 0.75f * 0.75f;
}

void recordSwing(Movie& movie, const Ball& start, float power, float dirX, float dirY,
                 float cupX, float cupY, float cupZ) {
    movie = Movie{};
    Ball ball = start;
    swing(ball, power, dirX, dirY);
    for (int frame = 0; frame < kMaxMovieFrames; ++frame) {
        movie.frames[movie.count++] = ball;
        if (inCup(ball, cupX, cupY, cupZ)) {
            movie.ballInFrame = static_cast<uint16_t>(frame);
            break;
        }
        step(ball, 1.0f / 90.0f);
        if (ball.resting) {
            movie.frames[movie.count++] = ball;
            break;
        }
    }
}

} // namespace tt::golf
