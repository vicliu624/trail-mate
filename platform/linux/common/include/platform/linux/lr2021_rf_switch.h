#pragma once

#include <cstdint>
#include <string_view>

namespace platform::linux_runtime
{
enum class Lr2021RfSwitchProfile : uint8_t
{
    None,
    Cm0_16E8
};

inline bool parse_lr2021_rf_switch_profile(std::string_view value, Lr2021RfSwitchProfile& output)
{
    if (value == "none") output = Lr2021RfSwitchProfile::None;
    else if (value == "cm0_16e8") output = Lr2021RfSwitchProfile::Cm0_16E8;
    else return false;
    return true;
}

// Independent command encoding from the official CM0 16E8 board facts:
// DIO7 high for HF TX; DIO6 high for HF RX/TX. Both low for LF RX/TX/standby.
// Writer must check transport and command status. No transmission is started.
template <typename Writer>
int apply_lr2021_rf_switch(Lr2021RfSwitchProfile profile, Writer&& write)
{
    if (profile == Lr2021RfSwitchProfile::None) return 0;
    if (profile != Lr2021RfSwitchProfile::Cm0_16E8) return -1;
    struct Command
    {
        uint16_t opcode;
        uint8_t data[2];
    };
    const Command commands[] = {{0x0112, {7, 0x20}}, {0x0112, {6, 0x20}}, {0x0113, {7, 0x10}}, {0x0113, {6, 0x18}}};
    for (const auto& command : commands)
    {
        const int code = write(command.opcode, command.data, sizeof(command.data));
        if (code) return code;
    }
    return 0;
}
} // namespace platform::linux_runtime
