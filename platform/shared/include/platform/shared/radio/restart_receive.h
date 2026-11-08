#pragma once

namespace platform::shared::radio
{
// Continuous RX remains active after RX_DONE. Radios such as LR2021 reject
// receive-path configuration until standby, even after their FIFO was drained.
// The caller owns the radio lock and transport-specific failure handling.
template <typename Radio, typename Check>
bool restartReceive(Radio& radio, Check check, const char* operation)
{
    return check(radio.standby(), "standby before receive") &&
           check(radio.startReceive(), operation);
}
} // namespace platform::shared::radio
