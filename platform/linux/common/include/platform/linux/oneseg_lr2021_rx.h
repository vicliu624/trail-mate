#pragma once

#include <cstddef>
#include <cstdint>

namespace platform::linux_runtime
{
// CM0-only receive setup for the first fixed-profile 1seg integration.
// Command semantics follow the pinned MIT lr2021-sdr RX research. No upstream
// implementation is copied, and no TX, MMIO or GPIO policy is added here.
class OneSegLr2021Rx
{
  public:
    static constexpr uint32_t kPairRate = 576000;
    static constexpr uint8_t kRoute = 1; // Source 0, signed IQ8, not source 1/6.
    static constexpr uint8_t kInputCode = 17;
    // This is an absolute wire-rate lower bound, excluding command overhead.
    // Meeting it is necessary, never proof of sustained reception.
    static constexpr uint32_t kMinimumWireClock = kPairRate * 2 * 8;
    // Native Mode3 1seg decoder input: 576000 * 1000/567 = 64 MHz/63.
    static constexpr uint32_t kResampleInterpolation = 1000;
    static constexpr uint32_t kResampleDecimation = 567;

    class Port
    {
      public:
        virtual ~Port() = default;
        // Must attest the real coordinator lease, stopped packet workers, and
        // a transport mutex held throughout setup/stream/recovery.
        virtual bool exclusivelyOwned() const = 0;
        virtual uint32_t spiClockHz() const = 0;
        // Return zero only after checking BUSY, transport and command status.
        virtual int write(uint16_t command, const uint8_t* data, size_t bytes) = 0;
        virtual int read(uint16_t command, uint8_t* data, size_t bytes) = 0;
    };
    enum class Error
    {
        None,
        InvalidChannel,
        NotExclusive,
        WireClock,
        Firmware,
        Command,
        ChipErrors
    };
    struct Result
    {
        Error error = Error::None;
        int native_error = 0;
        uint16_t chip_errors = 0;
        // True once a state-changing command was attempted, including on error.
        // Caller must restore the saved packet PHY before releasing ownership.
        bool restore_required = false;
        explicit operator bool() const { return error == Error::None; }
    };

    static uint32_t frequencyHz(unsigned channel)
    {
        return channel >= 13 && channel <= 62 ? 473142857u + (channel - 13) * 6000000u : 0;
    }

    // Leaves the chip in standby with the raw-route input configured. The
    // checked stream engine subsequently sets routing/FIFO and starts RX.
    // TCXO, board RF-switch policy and saved packet settings belong to caller.
    static Result configure(Port& port, unsigned channel)
    {
        Result result;
        const uint32_t hz = frequencyHz(channel);
        if (!hz)
        {
            result.error = Error::InvalidChannel;
            return result;
        }
        if (!port.exclusivelyOwned())
        {
            result.error = Error::NotExclusive;
            return result;
        }
        if (port.spiClockHz() < kMinimumWireClock)
        {
            result.error = Error::WireClock;
            return result;
        }
        uint8_t version[2]{};
        result.native_error = port.read(0x0101, version, sizeof(version));
        if (result.native_error)
        {
            result.error = Error::Command;
            return result;
        }
        if (version[0] != 1 || version[1] != 24)
        {
            result.error = Error::Firmware;
            return result;
        }
        result.restore_required = true;
        const uint8_t standby = 0, fsk = 2, agc = 0;
        const uint16_t cell = static_cast<uint16_t>(hz / 4000000u);
        const uint8_t calibration[] = {static_cast<uint8_t>(cell >> 8), static_cast<uint8_t>(cell), 0, 0, 0, 0};
        const uint8_t frequency[] = {static_cast<uint8_t>(hz >> 24), static_cast<uint8_t>(hz >> 16),
                                     static_cast<uint8_t>(hz >> 8), static_cast<uint8_t>(hz)};
        const uint8_t path[] = {0, 0}; // LF path, no RX boost.
        const uint8_t modulation[] = {0, 0, 0x27, 0x10, 0, kInputCode, 0, 0x13, 0x88};
        struct Command
        {
            uint16_t opcode;
            const uint8_t* data;
            size_t bytes;
        };
        const Command commands[] = {{0x0128, &standby, 1}, {0x0111, nullptr, 0}, {0x0207, &fsk, 1}, {0x0123, calibration, sizeof(calibration)}, {0x0200, frequency, sizeof(frequency)}, {0x0201, path, sizeof(path)}, {0x021a, &agc, 1}, {0x0240, modulation, sizeof(modulation)}};
        for (const auto& command : commands)
        {
            result.native_error = port.write(command.opcode, command.data, command.bytes);
            if (result.native_error)
            {
                result.error = Error::Command;
                return result;
            }
        }
        uint8_t errors[2]{};
        result.native_error = port.read(0x0110, errors, sizeof(errors));
        if (result.native_error)
        {
            result.error = Error::Command;
            return result;
        }
        result.chip_errors = static_cast<uint16_t>(static_cast<uint16_t>(errors[0]) << 8 | errors[1]);
        // Unlike exploratory tools, do not ignore image-calibration warnings.
        if (result.chip_errors) result.error = Error::ChipErrors;
        return result;
    }
};
} // namespace platform::linux_runtime
