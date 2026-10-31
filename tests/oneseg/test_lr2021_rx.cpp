#include "platform/linux/oneseg_lr2021_rx.h"
#include <cassert>
#include <vector>

using Rx = platform::linux_runtime::OneSegLr2021Rx;
struct Port : Rx::Port
{
    struct Call
    {
        uint16_t command;
        std::vector<uint8_t> data;
    };
    std::vector<Call> writes;
    bool owned = true;
    uint32_t clock = 16000000;
    unsigned reads = 0, fail_write = 0;
    uint8_t minor = 24;
    uint16_t errors = 0;
    int read_error = 0;
    bool exclusivelyOwned() const override { return owned; }
    uint32_t spiClockHz() const override { return clock; }
    int read(uint16_t command, uint8_t* data, size_t bytes) override
    {
        ++reads;
        assert(bytes == 2);
        if (read_error) return read_error;
        assert(command == 0x0101 || command == 0x0110);
        data[0] = command == 0x0101 ? 1 : static_cast<uint8_t>(errors >> 8);
        data[1] = command == 0x0101 ? minor : static_cast<uint8_t>(errors);
        return 0;
    }
    int write(uint16_t command, const uint8_t* data, size_t bytes) override
    {
        writes.push_back({command, bytes ? std::vector<uint8_t>(data, data + bytes) : std::vector<uint8_t>{}});
        return fail_write == writes.size() ? -77 : 0;
    }
};
int main()
{
    assert(Rx::frequencyHz(12) == 0 && Rx::frequencyHz(63) == 0);
    assert(Rx::frequencyHz(13) == 473142857 && Rx::frequencyHz(62) == 767142857);
    for (unsigned channel = 14; channel <= 62; ++channel)
        assert(Rx::frequencyHz(channel) - Rx::frequencyHz(channel - 1) == 6000000);
    static_assert(uint64_t(Rx::kPairRate) * Rx::kResampleInterpolation * 63 ==
                  uint64_t(64000000) * Rx::kResampleDecimation);
    for (unsigned failure = 1; failure <= 8; ++failure)
    {
        Port port;
        port.fail_write = failure;
        auto result = Rx::configure(port, 13);
        assert(!result && result.restore_required && result.native_error == -77);
        assert(result.error == Rx::Error::Command && port.writes.size() == failure);
    }
    Port port;
    auto result = Rx::configure(port, 13);
    assert(result && result.restore_required && port.reads == 2);
    const std::vector<uint16_t> expected{0x0128, 0x0111, 0x0207, 0x0123, 0x0200, 0x0201, 0x021a, 0x0240};
    assert(port.writes.size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i) assert(port.writes[i].command == expected[i]);
    const auto& bytes = port.writes[4].data;
    assert((uint32_t(bytes[0]) << 24 | uint32_t(bytes[1]) << 16 | uint32_t(bytes[2]) << 8 | bytes[3]) == 473142857);
    assert(port.writes[7].data.size() == 9 && port.writes[7].data[5] == Rx::kInputCode);
    Port busy;
    busy.owned = false;
    assert(Rx::configure(busy, 13).error == Rx::Error::NotExclusive && busy.reads == 0 && busy.writes.empty());
    Port slow;
    slow.clock = 1000000;
    assert(Rx::configure(slow, 13).error == Rx::Error::WireClock && slow.reads == 0);
    Port invalid;
    assert(Rx::configure(invalid, 0).error == Rx::Error::InvalidChannel && invalid.reads == 0);
    Port rom;
    rom.minor = 25;
    result = Rx::configure(rom, 13);
    assert(result.error == Rx::Error::Firmware && !result.restore_required && rom.writes.empty());
    Port failed;
    failed.read_error = -88;
    result = Rx::configure(failed, 13);
    assert(result.error == Rx::Error::Command && result.native_error == -88 && !result.restore_required);
    for (uint16_t error : {uint16_t(0x80), uint16_t(4), uint16_t(0x100)})
    {
        Port flagged;
        flagged.errors = error;
        result = Rx::configure(flagged, 13);
        assert(result.error == Rx::Error::ChipErrors && result.chip_errors == error && result.restore_required);
    }
}
