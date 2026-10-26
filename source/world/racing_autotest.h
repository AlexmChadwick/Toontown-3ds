#pragma once
#include "core/autotest.h"
#include "world/place.h"
#include "world/racing.h"
namespace tt::world {
// Diagnostic input only. All ownership, fees, results and tickets come from the AI.
class RacingAutotest {
  public:
    void update(autotest::Runner &runner, net::Client &client, Racing &racing, Place &place,
                Player &player, Input *input, bool transitionIdle);

  private:
    bool bought_ = false, asked_ = false, boarded_ = false, sawRace_ = false, sawResult_ = false;
    uint32_t nextRequest_ = 0;
};
} // namespace tt::world
