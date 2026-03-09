#pragma once

#include "team/ports/i_team_runtime.h"

namespace team::infra
{

class TeamRuntimeArduino : public team::ITeamRuntime
{
  public:
    uint32_t nowMillis() override;
    uint32_t nowUnixSeconds() override;
    void fillRandomBytes(uint8_t* out, size_t len) override;
};

} // namespace team::infra
