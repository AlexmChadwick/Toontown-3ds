#include "game/cog_thief_logic.h"

#include <cmath>

namespace tt::cogthief {

int calcScore(float timeElapsed) {
    if (timeElapsed < 0.0f) timeElapsed = 0.0f;
    if (timeElapsed > kGameDuration) timeElapsed = kGameDuration;
    float range = static_cast<float>(kMaxScore - kMinScore);
    float score = range * (timeElapsed / kGameDuration) + static_cast<float>(kMinScore);
    return static_cast<int>(score + 0.5f);
}

int evaluateFinalScore(float timeElapsed, int barrelsStolen) {
    int score = calcScore(timeElapsed);
    if (barrelsStolen == 0) {
        score += 8; // solo perfect bonus
    }
    return score;
}

void Simulation::init(int cogCount, float speed) {
    numCogs = cogCount < kMaxCogs ? cogCount : kMaxCogs;
    cogSpeed = speed;
    for (int i = 0; i < kNumBarrels; i++) {
        barrels[i].pos = kBarrelStart[i];
        barrels[i].carriedByCog = -1;
        barrels[i].stolen = false;
    }
    for (int i = 0; i < numCogs; i++) {
        cogs[i].pos = kCogStart[i];
        cogs[i].goal = CogGoal::None;
        cogs[i].targetBarrel = -1;
        cogs[i].returnIndex = i % 6;
        cogs[i].carryingBarrel = -1;
        cogs[i].stunTimer = 0.0f;
    }
    pie.active = false;
}

void Simulation::step(float dt) {
    // Pie movement
    if (pie.active) {
        pie.pos.x += pie.vel.x * dt;
        pie.pos.y += pie.vel.y * dt;
        pie.pos.z += pie.vel.z * dt;
        pie.vel.z -= 25.0f * dt; // gravity

        // Check ground hit
        if (pie.pos.z <= 0.0f) {
            pie.active = false;
        }

        // Check cog hit
        if (pie.active) {
            for (int i = 0; i < numCogs; i++) {
                if (cogs[i].goal == CogGoal::Stunned) continue;
                float dx = cogs[i].pos.x - pie.pos.x;
                float dy = cogs[i].pos.y - pie.pos.y;
                float d = std::sqrt(dx * dx + dy * dy);
                if (d < 2.0f && std::abs(cogs[i].pos.z - pie.pos.z) < 3.0f) {
                    // Hit cog!
                    cogs[i].goal = CogGoal::Stunned;
                    cogs[i].stunTimer = 6.0f;
                    if (cogs[i].carryingBarrel >= 0) {
                        int bIdx = cogs[i].carryingBarrel;
                        barrels[bIdx].carriedByCog = -1;
                        barrels[bIdx].pos = cogs[i].pos;
                        cogs[i].carryingBarrel = -1;
                    }
                    pie.active = false;
                    break;
                }
            }
        }
    }

    // Cogs update
    for (int i = 0; i < numCogs; i++) {
        if (cogs[i].goal == CogGoal::Stunned) {
            cogs[i].stunTimer -= dt;
            if (cogs[i].stunTimer <= 0.0f) {
                cogs[i].pos = kCogStart[i];
                cogs[i].goal = CogGoal::None;
            }
            continue;
        }

        if (cogs[i].goal == CogGoal::None) {
            // Pick closest available barrel
            int bestBarrel = -1;
            float bestDist = 1e9f;
            for (int b = 0; b < kNumBarrels; b++) {
                if (barrels[b].stolen || barrels[b].carriedByCog >= 0) continue;
                float dx = barrels[b].pos.x - cogs[i].pos.x;
                float dy = barrels[b].pos.y - cogs[i].pos.y;
                float dist = std::sqrt(dx * dx + dy * dy);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestBarrel = b;
                }
            }
            if (bestBarrel >= 0) {
                cogs[i].goal = CogGoal::Barrel;
                cogs[i].targetBarrel = bestBarrel;
            }
        }

        if (cogs[i].goal == CogGoal::Barrel) {
            int b = cogs[i].targetBarrel;
            if (b < 0 || barrels[b].stolen || (barrels[b].carriedByCog >= 0 && barrels[b].carriedByCog != i)) {
                cogs[i].goal = CogGoal::None;
                cogs[i].targetBarrel = -1;
                continue;
            }

            float dx = barrels[b].pos.x - cogs[i].pos.x;
            float dy = barrels[b].pos.y - cogs[i].pos.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < 1.0f) {
                // Grab barrel!
                barrels[b].carriedByCog = i;
                cogs[i].carryingBarrel = b;
                cogs[i].goal = CogGoal::RunAway;
            } else {
                cogs[i].pos.x += (dx / dist) * cogSpeed * dt;
                cogs[i].pos.y += (dy / dist) * cogSpeed * dt;
            }
        } else if (cogs[i].goal == CogGoal::RunAway) {
            const Vec3& ret = kCogReturn[cogs[i].returnIndex];
            float dx = ret.x - cogs[i].pos.x;
            float dy = ret.y - cogs[i].pos.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < 1.5f) {
                // Stolen!
                int b = cogs[i].carryingBarrel;
                if (b >= 0) {
                    barrels[b].stolen = true;
                    barrels[b].carriedByCog = -1;
                    cogs[i].carryingBarrel = -1;
                }
                cogs[i].pos = kCogStart[i];
                cogs[i].goal = CogGoal::None;
            } else {
                cogs[i].pos.x += (dx / dist) * cogSpeed * dt;
                cogs[i].pos.y += (dy / dist) * cogSpeed * dt;
                if (cogs[i].carryingBarrel >= 0) {
                    barrels[cogs[i].carryingBarrel].pos = cogs[i].pos;
                }
            }
        }
    }
}

bool Simulation::throwPie(float toonX, float toonY, float toonHeading) {
    if (pie.active) return false;
    float rad = toonHeading * 3.14159265f / 180.0f;
    float dirX = -std::sin(rad);
    float dirY = std::cos(rad);
    pie.active = true;
    pie.pos = {toonX + dirX * 1.5f, toonY + dirY * 1.5f, 2.5f};
    pie.vel = {dirX * 35.0f, dirY * 35.0f, 6.0f};
    return true;
}

int Simulation::barrelsStolenCount() const {
    int count = 0;
    for (int i = 0; i < kNumBarrels; i++) {
        if (barrels[i].stolen) count++;
    }
    return count;
}

bool Simulation::allBarrelsStolen() const {
    return barrelsStolenCount() >= kNumBarrels;
}

} // namespace tt::cogthief
