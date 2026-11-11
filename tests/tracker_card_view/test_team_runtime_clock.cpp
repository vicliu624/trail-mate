#include "Arduino.h"
#include "platform/esp/arduino_common/team/runtime/team_runtime_arduino.h"
#include "sys/clock.h"
#include "ui/screens/team/team_page_read_model.h"
#include <cassert>
#include <cstdio>

int main()
{
    uint32_t (*millis_provider)() = []
    { return arduino_test_millis; };
    sys::set_millis_provider(millis_provider);
    sys::set_epoch_seconds_provider([]
                                    { return 0U; });
    team::infra::TeamRuntimeArduino runtime;
    assert(runtime.nowMillis() == 123000 && runtime.nowUnixSeconds() == 123);
    sys::set_epoch_seconds_provider([]
                                    { return 1794409000U; });
    const uint32_t received_s = runtime.nowUnixSeconds();
    const auto view = team::ui::TeamPageReadModel(1794409030U).buildLastSeen(received_s);
    assert(view.kind == team::ui::TeamRelativeTimeKind::MinutesAgo && view.value == 0);
    assert(runtime.nowMillis() == 123000);
    sys::set_epoch_seconds_provider([]
                                    { return 100U; });
    assert(runtime.nowUnixSeconds() == 123);
    sys::set_epoch_seconds_provider(nullptr);
    sys::set_millis_provider(nullptr);
    std::puts("Arduino Team clock: valid epoch produces visible recent activity; unset clock falls back to uptime.");
}
