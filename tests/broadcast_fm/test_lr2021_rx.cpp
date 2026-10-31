#include "platform/shared/radio/fm_lr2021_rx.h"
#include <array>
#include <cassert>
#include <cstring>
#include <vector>
using Rx = platform::shared::radio::FmLr2021Rx;
struct Fake final : Rx::Port
{
    struct Command
    {
        uint16_t opcode;
        std::vector<uint8_t> bytes;
    };
    std::vector<Command> commands;
    bool exclusive = true;
    uint32_t clock = 16000000;
    uint8_t major = 1, minor = 24;
    uint16_t errors = 0;
    unsigned calls = 0, fail_at = 0;
    bool exclusivelyOwned() const override { return exclusive; }
    uint32_t spiClockHz() const override { return clock; }
    int write(uint16_t opcode, const uint8_t* data, size_t bytes) override
    {
        commands.push_back({opcode, bytes ? std::vector<uint8_t>(data, data + bytes) : std::vector<uint8_t>{}});
        return ++calls == fail_at ? -77 : 0;
    }
    int read(uint16_t opcode, uint8_t* data, size_t bytes) override
    {
        assert(bytes == 2);
        if (++calls == fail_at) return -77;
        if (opcode == 0x0101)
        {
            data[0] = major;
            data[1] = minor;
        }
        else
        {
            assert(opcode == 0x0110);
            data[0] = errors >> 8;
            data[1] = errors;
        }
        return 0;
    }
};
int main()
{
    for (const uint32_t hz : {87500000u, 99999999u, 108000001u, 915000000u})
    {
        Fake f;
        const auto result = Rx::configure(f, hz);
        assert(result.error == Rx::Error::Frequency && !f.calls && !result.restore_required);
    }
    for (unsigned condition = 0; condition < 3; ++condition)
    {
        Fake f;
        if (condition == 0) f.exclusive = false;
        if (condition == 1) f.clock = Rx::kMinimumWireClock - 1;
        if (condition == 2) f.minor = 23;
        const auto result = Rx::configure(f, 100800000);
        assert(!result && !result.restore_required && f.commands.empty());
    }
    Fake f;
    const auto result = Rx::configure(f, 100800000);
    assert(result && result.restore_required && !result.chip_errors && f.commands.size() == 8);
    assert(f.commands[4].opcode == 0x0200);
    const auto& frequency = f.commands[4].bytes;
    const uint32_t encoded = (uint32_t(frequency[0]) << 24) | (uint32_t(frequency[1]) << 16) |
                             (uint32_t(frequency[2]) << 8) | frequency[3];
    assert(encoded == 100800000 && f.commands[7].bytes[5] == 17);
    for (const auto& command : f.commands) assert(command.opcode != 0x020b && command.opcode != 0x020c);
    for (unsigned at = 1; at <= 10; ++at)
    {
        Fake failure;
        failure.fail_at = at;
        const auto failed = Rx::configure(failure, 100800000, true);
        assert(!failed && failed.native_error == -77 && failed.error == Rx::Error::Command);
        assert(failed.restore_required == (at != 1));
    }
    Fake warning;
    warning.errors = Rx::kImageCalibrationError;
    assert(Rx::configure(warning, 100800000).error == Rx::Error::ChipErrors);
    const auto accepted_warning = Rx::configure(warning, 100800000, true);
    assert(accepted_warning && accepted_warning.calibration_warning && accepted_warning.chip_errors == 0x0080);
    warning.errors = 0x0081;
    assert(Rx::configure(warning, 100800000, true).error == Rx::Error::ChipErrors);
    for (uint32_t hz : {100000000u, 108000000u})
    {
        Fake boundary;
        assert(Rx::configure(boundary, hz));
    }
}
