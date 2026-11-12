#include "game/ice_logic.h"

#include <algorithm>
#include <cmath>

namespace tt::icegame {

static constexpr int kPointsDeadCenter[5] = {5, 5, 5, 4, 3};
static constexpr int kBonusPointsForPlace[4] = {3, 2, 1, 0};

void Tire::launch(float force0to100, float headingDeg) {
    if (force0to100 < 0.0f) force0to100 = 0.0f;
    if (force0to100 > 100.0f) force0to100 = 100.0f;

    // Upstream: degs = heading + 90
    float rad = (headingDeg + 90.0f) * 0.0174532925f;
    float speed = force0to100 * 0.45f;
    vx = std::cos(rad) * speed;
    vy = std::sin(rad) * speed;
    moving = (speed > 0.1f);
}

void Tire::step(float dt, const Obstacle* obstacles, int numObstacles) {
    if (!moving) return;

    x += vx * dt;
    y += vy * dt;

    // Wall bounce
    if (x > kBoundX) {
        x = kBoundX;
        vx = -vx * 0.75f;
    } else if (x < -kBoundX) {
        x = -kBoundX;
        vx = -vx * 0.75f;
    }

    if (y > kBoundY) {
        y = kBoundY;
        vy = -vy * 0.75f;
    } else if (y < -kBoundY) {
        y = -kBoundY;
        vy = -vy * 0.75f;
    }

    // Obstacles: push the tire out of the tire stack / crate and reflect the normal velocity.
    for (int i = 0; i < numObstacles; ++i) {
        const Obstacle& o = obstacles[i];
        float cx = o.x, cy = o.y, reach = kTireRadius * 2.0f;
        if (o.cubic) {
            cx = std::clamp(x, o.x - kTireRadius, o.x + kTireRadius);
            cy = std::clamp(y, o.y - kTireRadius, o.y + kTireRadius);
            reach = kTireRadius;
        }
        float nx = x - cx, ny = y - cy;
        const float d = std::sqrt(nx * nx + ny * ny);
        if (d >= reach) continue;
        if (d > 1e-4f) { nx /= d; ny /= d; }
        else { nx = (std::fabs(x - o.x) > std::fabs(y - o.y)) ? (x > o.x ? 1.0f : -1.0f) : 0.0f;
               ny = nx == 0.0f ? (y > o.y ? 1.0f : -1.0f) : 0.0f; }
        x = cx + nx * reach;
        y = cy + ny * reach;
        const float vn = vx * nx + vy * ny;
        if (vn < 0.0f) {
            vx -= 1.75f * vn * nx;
            vy -= 1.75f * vn * ny;
        }
    }

    // Ice friction
    float friction = 0.985f;
    vx *= std::pow(friction, dt * 60.0f);
    vy *= std::pow(friction, dt * 60.0f);

    if (std::sqrt(vx * vx + vy * vy) < 0.2f) {
        vx = 0.0f;
        vy = 0.0f;
        moving = false;
    }
}

int calculatePoints(float dist, int numPlayers, int place) {
    if (numPlayers < 1) numPlayers = 1;
    if (numPlayers > 4) numPlayers = 4;
    int deadCenter = kPointsDeadCenter[numPlayers];
    float pts = (dist / kFarthestLength) * (float)(kPointsInCorner - deadCenter) + (float)deadCenter;
    int bonusIndex = place + (4 - numPlayers);
    if (bonusIndex < 0) bonusIndex = 0;
    if (bonusIndex > 3) bonusIndex = 3;
    int bonus = kBonusPointsForPlace[bonusIndex];
    int rounded = (int)(pts + 0.5f);
    return rounded + bonus;
}

void Simulation::init(int players) {
    numPlayers = (players >= 1 && players <= 4) ? players : 1;
    curMatch = 0;
    curRound = 0;
    isDone = false;
    numObstacles = 0;
    for (int i = 0; i < 4; ++i) {
        scores[i] = 0;
    }
    resetRound();
}

void Simulation::resetRound() {
    float startPositions[4][2] = {
        {-kBoundX, -kBoundY},
        {kBoundX, kBoundY},
        {-kBoundX, kBoundY},
        {kBoundX, -kBoundY}
    };
    for (int i = 0; i < 4; ++i) {
        tires[i].x = startPositions[i][0];
        tires[i].y = startPositions[i][1];
        tires[i].vx = 0.0f;
        tires[i].vy = 0.0f;
        tires[i].moving = false;
    }
}

void Simulation::setChoice(int playerIndex, float force, float headingDeg) {
    if (playerIndex >= 0 && playerIndex < 4) {
        tires[playerIndex].launch(force, headingDeg);
    }
}

void Simulation::stepPhysics(float dt) {
    for (int i = 0; i < 4; ++i) {
        tires[i].step(dt, obstacles, numObstacles);
    }
}

bool Simulation::allTiresStopped() const {
    for (int i = 0; i < numPlayers; ++i) {
        if (tires[i].moving) return false;
    }
    return true;
}

void Simulation::scoreMatch() {
    struct PlayerDist {
        int idx;
        float dist;
    };
    PlayerDist dists[4];
    for (int i = 0; i < numPlayers; ++i) {
        dists[i].idx = i;
        dists[i].dist = tires[i].distanceToCenter();
    }

    for (int i = 0; i < numPlayers; ++i) {
        for (int j = i + 1; j < numPlayers; ++j) {
            if (dists[j].dist < dists[i].dist) {
                PlayerDist tmp = dists[i];
                dists[i] = dists[j];
                dists[j] = tmp;
            }
        }
    }

    for (int sortIdx = 0; sortIdx < numPlayers; ++sortIdx) {
        int pIdx = dists[sortIdx].idx;
        int pts = calculatePoints(dists[sortIdx].dist, numPlayers, sortIdx);
        scores[pIdx] += pts;
    }

    curMatch++;
    curRound = 0;
    if (curMatch >= kNumMatches) {
        isDone = true;
    } else {
        resetRound();
    }
}

} // namespace tt::icegame
