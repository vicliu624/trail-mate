#pragma once

#include "agenda/ports/agenda_clock.h"

namespace platform::esp::arduino_common
{
class AgendaClock final : public agenda::IAgendaClock
{
  public:
    agenda::ClockSample sample() const override;
};
} // namespace platform::esp::arduino_common
