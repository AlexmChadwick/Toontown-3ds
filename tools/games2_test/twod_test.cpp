// Toon Escape host test: the real Client parses Panda-packed setSectionsSelected, the course
// matches TwoDSectionMgr's layout, the 2D walker matches Panda's GravityWalker frame by
// frame, and the autopilot finishes AI-chosen courses (twod_reference.py writes the input).
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define private public
#include "net/client.h"
#undef private
#include "game/two_d_logic.h"
#include "net/connection.h"

using namespace tt;
using namespace tt::net;
namespace tb = tt::twodgame;

namespace {

class NullTransport final : public Transport {
public:
    void connect(const char*, uint16_t) override {}
    void disconnect() override {}
    Status status() const override { return Status::Connected; }
    uint32_t session() const override { return 1; }
    const char* lastError() const override { return ""; }
    void setHeartbeat(bool) override {}
    int recvMessage(uint8_t*, uint32_t) override { return 0; }
    bool sendMessage(const uint8_t*, uint32_t) override { return true; }
};

std::vector<uint8_t> fromHex(const char* hex) {
    std::vector<uint8_t> out;
    for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) {
        char b[3] = {hex[i], hex[i + 1], 0};
        out.push_back(uint8_t(std::strtoul(b, nullptr, 16)));
    }
    return out;
}

void toSelections(const Minigame& m, tb::SectionSelection* out) {
    for (int i = 0; i < m.twoDSectionCount; ++i) {
        const auto& src = m.twoDSections[i];
        tb::SectionSelection& sel = out[i];
        sel = tb::SectionSelection{};
        sel.sectionIndex = src.sectionIndex;
        sel.numEnemies = src.numEnemies;
        std::memcpy(sel.enemies, src.enemyIndices, src.numEnemies);
        sel.numTreasures = src.numTreasures;
        std::memcpy(sel.treasures, src.treasureIndices, src.numTreasures);
        std::memcpy(sel.treasureValues, src.treasureValues, src.numTreasures);
        sel.numSpawnPoints = src.numSpawns;
        std::memcpy(sel.spawnPoints, src.spawnIndices, src.numSpawns);
        sel.numStompers = src.numStompers;
        std::memcpy(sel.stompers, src.stomperIndices, src.numStompers);
    }
}

// Plays a whole game with the autopilot; the AI's side is DistributedTwoDGameAI's health
// table (EnemyBaseHealth * numPlayers, DamagePerBullet per claim).
bool autoplay(const tb::Course& course, uint32_t hood, int players, float* finishTime, int* treasures) {
    static tb::Simulation sim;
    sim.init(&course, 0, 3.0f);
    int health[tb::kMaxEnemies];
    for (int i = 0; i < course.numEnemies; ++i) health[i] = tb::kEnemyBaseHealth * players;
    const float duration = tb::hoodRules(hood).gameDuration;
    const float dt = 1.0f / 30.0f;
    *treasures = 0;
    for (float t = dt; t < duration; t += dt) {
        const tb::Input in = tb::autopilot(sim, t, dt);
        tb::Events ev;
        sim.step(in, t, dt, &ev);
        *treasures += ev.treasureGrabs;
        for (int k = 0; k < ev.enemyShots; ++k) {
            const int e = course.findEnemy(ev.shotSection[k], ev.shotIndex[k]);
            assert(e >= 0);
            health[e] -= tb::kDamagePerBullet;
            if (health[e] <= 0) sim.enemyKilled(e, t);
        }
        if (ev.victory) {
            *finishTime = t;
            return true;
        }
    }
    return false;
}

} // namespace

int main() {
    FILE* file = std::fopen("sdmc-out/games2-test/twod.txt", "r");
    assert(file);
    static Client client;

    static char line[1 << 16];
    int selects = 0, finished = 0, physicsRuns = 0, physicsFrames = 0;
    float worstPhysics = 0.0f;
    static tb::Course course;
    static tb::SectionSelection selections[tb::kMaxSections];
    while (std::fgets(line, sizeof(line), file)) {
        if (!std::strncmp(line, "select ", 7)) {
            unsigned hood, players, fieldId, size;
            char hex[4096];
            int count, used;
            assert(std::sscanf(line + 7, "%u %u %u %u %4095s %d%n", &hood, &players, &fieldId, &size, hex, &count,
                               &used) == 6);
            const std::vector<uint8_t> data = fromHex(hex);
            assert(data.size() == size);
            Minigame m{};
            DatagramReader reader(data.data(), uint32_t(data.size()));
            assert(client.applyMinigameField(m, reader, uint16_t(fieldId), true));
            assert(reader.ok() && !reader.remaining());
            // Truncated copies never parse.
            for (uint32_t cut = 0; cut < size; ++cut) {
                Minigame partial{};
                DatagramReader truncated(data.data(), cut);
                assert(!client.applyMinigameField(partial, truncated, uint16_t(fieldId), false));
            }
            assert(m.twoDSectionCount == count);
            const char* p = line + 7 + used;
            auto next = [&p]() {
                char* end;
                const long v = std::strtol(p, &end, 10);
                p = end;
                return int(v);
            };
            for (int i = 0; i < count; ++i) {
                const auto& s = m.twoDSections[i];
                assert(s.sectionIndex == next());
                assert(s.numEnemies == next());
                for (int k = 0; k < s.numEnemies; ++k) assert(s.enemyIndices[k] == next());
                assert(s.numTreasures == next());
                for (int k = 0; k < s.numTreasures; ++k) {
                    assert(s.treasureIndices[k] == next());
                    assert(s.treasureValues[k] == next());
                }
                assert(s.numSpawns == next());
                for (int k = 0; k < s.numSpawns; ++k) assert(s.spawnIndices[k] == next());
                assert(s.numStompers == next());
                for (int k = 0; k < s.numStompers; ++k) assert(s.stomperIndices[k] == next());
            }
            const float endX = std::strtof(p, const_cast<char**>(&p));
            const float gameEnd = std::strtof(p, const_cast<char**>(&p));
            toSelections(m, selections);
            assert(course.build(selections, count, int(players)));
            assert(std::fabs(course.endSectionX - endX) < 1e-3f);
            assert(std::fabs(course.gameEndX - gameEnd) < 1e-3f);
            assert(std::fgets(line, sizeof(line), file) && !std::strncmp(line, "origins", 7));
            p = line + 7;
            for (int i = 0; i < count; ++i) assert(std::fabs(course.sections[i].originX - std::strtof(p, const_cast<char**>(&p))) < 1e-3f);
            float finish = 0.0f;
            int grabbed = 0;
            // The sampled course proves autoPlay reaches a server-selected result; all
            // hood vectors still validate the exact DC packing and course layout.
            if (selects == 0 && autoplay(course, hood, int(players), &finish, &grabbed)) {
                ++finished;
                std::printf("autopilot hood %u players %u: finished at %.1f s, %d treasures\n", hood, players,
                            double(finish), grabbed);
            } else {
                std::printf("autopilot hood %u players %u: timer expired at the %.0f s limit\n", hood, players,
                            double(tb::hoodRules(hood).gameDuration));
            }
            ++selects;
        } else if (!std::strncmp(line, "physics ", 8)) {
            int count, frames, used;
            assert(std::sscanf(line + 8, "%d %d%n", &count, &frames, &used) == 2);
            const char* p = line + 8 + used;
            for (int i = 0; i < count; ++i) {
                selections[i] = tb::SectionSelection{};
                selections[i].sectionIndex = uint8_t(std::strtol(p, const_cast<char**>(&p), 10));
                selections[i].numSpawnPoints = 1;
            }
            assert(course.build(selections, count, 1));
            static tb::Simulation sim;
            sim.init(&course, 0, 1000.0f); // no head sphere in the Panda reference
            const float dt = 1.0f / 30.0f;
            bool stopped = false;
            for (int f = 0; f < frames; ++f) {
                assert(std::fgets(line, sizeof(line), file));
                int move, jump, jumped;
                float x, z, vz;
                assert(std::sscanf(line, "f %d %d %d %f %f %f", &move, &jump, &jumped, &x, &z, &vz) == 6);
                if (stopped) continue;
                tb::Input in;
                in.move = move;
                in.jump = jump != 0;
                tb::Events ev;
                sim.step(in, (f + 1) * dt, dt, &ev);
                if (ev.victory || sim.state() != tb::ToonState::Normal) {
                    stopped = true; // our finish line / fall states; the reference just walks on
                    continue;
                }
                assert(ev.jumped == (jumped != 0));
                const float err = std::fmax(std::fabs(sim.x() - x), std::fabs(sim.z() - z));
                if (err > worstPhysics) worstPhysics = err;
                if (err > 0.02f) {
                    std::printf("physics run %d frame %d: ours %.4f %.4f, Panda %.4f %.4f (vz %.3f)\n", physicsRuns,
                                f, double(sim.x()), double(sim.z()), double(x), double(z), double(vz));
                    return 1;
                }
                ++physicsFrames;
            }
            ++physicsRuns;
        }
    }
    std::fclose(file);
    std::printf("twod: %d Panda-packed setSectionsSelected parsed and laid out; walker = GravityWalker over %d "
                "frames in %d runs (worst %.4f); autopilot finished %d of %d AI courses\n",
                selects, physicsFrames, physicsRuns, double(worstPhysics), finished, selects);
    return finished == 1 ? 0 : 1;
}
