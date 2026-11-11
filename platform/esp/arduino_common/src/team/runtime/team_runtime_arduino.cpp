#include "platform/esp/arduino_common/team/runtime/team_runtime_arduino.h"
#include "sys/clock.h"

#include <Arduino.h>

namespace team::infra
{

uint32_t TeamRuntimeArduino::nowMillis()
{
    return millis();
}

uint32_t TeamRuntimeArduino::nowUnixSeconds()
{
    const uint32_t epoch_s = sys::epoch_seconds_now();
    return epoch_s >= 1577836800U ? epoch_s : sys::uptime_seconds_now();
}

void TeamRuntimeArduino::fillRandomBytes(uint8_t* out, size_t len)
{
    if (!out)
    {
        return;
    }

    for (size_t index = 0; index < len; ++index)
    {
        out[index] = static_cast<uint8_t>(random(0, 256));
    }
}

} // namespace team::infra
