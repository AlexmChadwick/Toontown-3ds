#include "world/racing_autotest.h"
#include "core/log.h"
#include "net/net_platform.h"
#include <cmath>
#include <cstdio>
#include <cstring>
namespace tt::world {
void RacingAutotest::update(autotest::Runner &runner, net::Client &client, Racing &racing,
                            Place &place, Player &player, Input *input, bool transitionIdle) {
    if (!runner.active() || !client.inZone() || !transitionIdle)
        return;
    const auto *label = runner.label();
    if (std::strncmp(label, "speedway: ", 10))
        return;
    const uint32_t now = net::plat::nowMs();
    if (runner.stepBegan()) {
        bought_ = asked_ = boarded_ = sawRace_ = sawResult_ = false;
        nextRequest_ = 0;
    }
    if (!std::strcmp(label, "speedway: pads")) {
        const bool owned = client.clerkProfile().kart[0] >= 0;
        if (owned && place.zoneId() == 8000) {
            TT_LOG("racing autotest: owned kart %d tickets %lu ready for Practice",
                   client.clerkProfile().kart[0],
                   static_cast<unsigned long>(client.ownBook().tickets));
            runner.finishStep();
            return;
        }
        if (!owned && place.kind() == PlaceKind::Interior) {
            for (int i = 0; i < net::kMaxNpcs; ++i) {
                const auto &npc = client.npcSlot(i);
                if (!npc.active || npc.classId != dc::DistributedNPCKartClerk::kClassId)
                    continue;
                char key[48];
                std::snprintf(key, sizeof(key), "npc_origin_%u", npc.posIndex);
                const auto *anchor = place.point(key);
                if (!anchor)
                    continue;
                const float angle = anchor->h * .01745329252f;
                player.reset(anchor->x - std::sin(angle), anchor->y + std::cos(angle), anchor->z,
                             anchor->h + 180);
                const auto &session = client.clerkSession();
                if (!asked_ && now >= nextRequest_) {
                    asked_ = client.sendServiceClerkEnter(npc.doId);
                    nextRequest_ = now + 2000;
                }
                if (!bought_ && session.movie.avatar == client.avatarId() &&
                    session.movie.mode == 1) {
                    if (client.ownBook().tickets >= 100) {
                        bought_ = client.sendClerkKartBody(0);
                        TT_LOG("racing autotest: request stock starter kart cost 100; tickets "
                               "before %lu",
                               static_cast<unsigned long>(client.ownBook().tickets));
                    } else {
                        TT_LOG("racing autotest: blocked starter kart needs 100 tickets; have %lu",
                               static_cast<unsigned long>(client.ownBook().tickets));
                    }
                }
            }
            return;
        }
        if (owned && place.kind() == PlaceKind::Interior && !client.clerkSession().finishing)
            client.sendServiceClerkDone();
        if (now < nextRequest_)
            return;
        // The Speedway kart shop is DNA tb1. Use the actual baked door volume.
        const auto &collision = place.collision();
        for (int i = 0; i < collision.numTriggers(); ++i) {
            const auto &trigger = collision.trigger(i);
            if (!std::strstr(trigger.name, "door_trigger"))
                continue;
            if (!owned && std::strncmp(trigger.owner, "tb1:", 4))
                continue;
            player.reset((trigger.min[0] + trigger.max[0]) * .5f,
                         (trigger.min[1] + trigger.max[1]) * .5f, trigger.min[2],
                         player.headingDeg());
            nextRequest_ = now + 3000;
            return;
        }
    } else if (!std::strcmp(label, "speedway: race")) {
        if (racing.active()) {
            sawRace_ = true;
            if (client.racingRace() && client.racingRace()->finished)
                sawResult_ = true;
            racing.autopilot(input);
            return;
        }
        if (sawRace_ && place.zoneId() == 8000) {
            TT_LOG("racing autotest: %s; tickets %lu",
                   sawResult_ ? "authoritative result and return complete"
                              : "FAIL returned without a server result",
                   static_cast<unsigned long>(client.ownBook().tickets));
            runner.finishStep();
            return;
        }
        if (boarded_ && now >= nextRequest_ && !racing.driving())
            boarded_ = false;
        if (boarded_ || now < nextRequest_ || client.clerkProfile().kart[0] < 0)
            return;
        for (int i = 0; i < net::kMaxRacingBlocks; ++i) {
            const auto &block = client.racingBlockSlot(i);
            if (!block.active || block.viewing || block.occupant)
                continue;
            const net::RacingPad *pad = nullptr;
            for (int j = 0; j < net::kMaxRacingPads; ++j) {
                const auto &candidate = client.racingPadSlot(j);
                if (candidate.active && candidate.doId == block.padDoId &&
                    candidate.trackCount >= 2 && candidate.trackIds[1] == 0 &&
                    candidate.trackIds[0] == 0)
                    pad = &candidate;
            }
            if (!pad ||
                (std::strcmp(pad->state, "WaitEmpty") && std::strcmp(pad->state, "WaitCountdown")))
                continue;
            player.reset(block.x, block.y, block.z, block.h);
            input->down |= KEY_A;
            boarded_ = true;
            nextRequest_ = now + 3000;
            TT_LOG("racing autotest: board Practice block %lu pad %lu track %u",
                   static_cast<unsigned long>(block.doId), static_cast<unsigned long>(pad->doId),
                   pad->trackIds[0]);
            return;
        }
    } else if (!std::strcmp(label, "speedway: shop")) {
        TT_LOG("racing autotest: final kart %d tickets %lu zone %lu", client.clerkProfile().kart[0],
               static_cast<unsigned long>(client.ownBook().tickets),
               static_cast<unsigned long>(place.zoneId()));
        runner.finishStep();
    }
}
} // namespace tt::world
