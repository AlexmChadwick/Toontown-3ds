#pragma once
// DistributedElevator / DistributedCogKart client text shared by every elevator owner.
#include <cstdio>

#include "net/client.h"
#include "net/dc_generated.h"

namespace tt::world {

// rejectBoard -> localAvatar.elevatorNotifier.showMe (TTLocalizer); null for reasons stock shows
// nothing for (no seat, boarding party, ...).
inline const char* elevatorRejectText(const net::CogElevator& e, char* buf, size_t size) {
    switch (e.rejectReason) {
    case 1: return "You need to wait for the next elevator."; // REJECT_SHUFFLE: ElevatorHoppedOff
    case 2: // REJECT_MINLAFF: ElevatorMinLaff / KartMinLaff
        std::snprintf(buf, size,
                      e.classId == dc::DistributedCogKart::kClassId ? "You need %u laff points to ride this kart"
                                                                    : "You need %u laff points to ride this elevator.",
                      unsigned(e.minLaff));
        return buf;
    case 4: return "You cannot board this elevator until you have earned a promotion."; // BossElevatorRejectMessage
    case 6: return "This elevator is not yet available."; // NotYetAvailable
    default: return nullptr;
    }
}

// ElevatorData[type]['countdown'] (DistributedElevatorInt: INTERIOR_ELEVATOR_COUNTDOWN_TIME is
// the interior's own); the boss lobbies' elevators wait 30 s, every other exterior one 15 s.
inline float elevatorCountdownSec(const net::CogElevator& e) {
    const uint16_t c = e.classId;
    return c == dc::DistributedVPElevator::kClassId || c == dc::DistributedCFOElevator::kClassId ||
                   c == dc::DistributedCJElevator::kClassId || c == dc::DistributedBBElevator::kClassId
               ? 30.0f
               : 15.0f;
}

} // namespace tt::world
