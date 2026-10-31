#include "platform/linux/lr2021_rf_switch.h"
#include <array>
#include <cassert>
#include <vector>

using namespace platform::linux_runtime;
struct Call
{
    uint16_t opcode;
    std::array<uint8_t, 2> data;
};
int main()
{
    Lr2021RfSwitchProfile profile = Lr2021RfSwitchProfile::Cm0_16E8;
    assert(parse_lr2021_rf_switch_profile("none", profile) && profile == Lr2021RfSwitchProfile::None);
    assert(parse_lr2021_rf_switch_profile("cm0_16e8", profile) && profile == Lr2021RfSwitchProfile::Cm0_16E8);
    for (const auto* bad : {"", "p4", "16e8", "cm0_16e8-extra", "CM0_16E8"})
        assert(!parse_lr2021_rf_switch_profile(bad, profile) && profile == Lr2021RfSwitchProfile::Cm0_16E8);
    const std::vector<Call> expected{{0x0112, {7, 0x20}}, {0x0112, {6, 0x20}}, {0x0113, {7, 0x10}}, {0x0113, {6, 0x18}}};
    for (unsigned fail = 0; fail <= 4; ++fail)
    {
        std::vector<Call> calls;
        const int code = apply_lr2021_rf_switch(profile, [&](uint16_t op, const uint8_t* data, size_t bytes)
                                                {
            assert(bytes == 2);
            calls.push_back({op, {data[0], data[1]}});
            return calls.size() == fail ? -77 : 0; });
        assert(code == (fail ? -77 : 0));
        assert(calls.size() == (fail ? fail : 4));
        for (size_t n = 0; n < calls.size(); ++n)
            assert(calls[n].opcode == expected[n].opcode && calls[n].data == expected[n].data);
    }
    unsigned calls = 0;
    auto writer = [&](uint16_t, const uint8_t*, size_t)
    { ++calls; return 0; };
    assert(apply_lr2021_rf_switch(Lr2021RfSwitchProfile::None, writer) == 0 && calls == 0);
    assert(apply_lr2021_rf_switch(static_cast<Lr2021RfSwitchProfile>(255), writer) == -1 && calls == 0);
    assert((expected[2].data[1] & 0x7) == 0 && (expected[3].data[1] & 0x7) == 0);
}
