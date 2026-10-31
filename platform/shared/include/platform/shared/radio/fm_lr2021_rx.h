#pragma once
#include <cstddef>
#include <cstdint>

namespace platform::shared::radio
{
// Experimental, receive-only upper-FM-band setup for ROM 1.24. This does not
// qualify this board's RF passband, measured sample rate or station reception.
// It never changes packet-transmit frequency validation or board GPIO policy.
class FmLr2021Rx
{
  public:
    static constexpr uint32_t kPairRate = 250000;
    static constexpr uint32_t kMinimumWireClock = kPairRate * 2 * 8;
    static constexpr uint8_t kRoute = 1, kInputCode = 17; // Raw source 0, signed IQ8.
    static constexpr uint16_t kImageCalibrationError = 0x0080;
    class Port
    {
      public:
        virtual ~Port() = default;
        // The owner must hold the physical radio/SPI lock and stop packet users.
        virtual bool exclusivelyOwned() const = 0;
        virtual uint32_t spiClockHz() const = 0;
        virtual int write(uint16_t opcode, const uint8_t*, size_t) = 0;
        virtual int read(uint16_t opcode, uint8_t*, size_t) = 0;
    };
    enum class Error : uint8_t
    {
        None,
        Frequency,
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
        bool restore_required = false, calibration_warning = false;
        explicit operator bool() const { return error == Error::None; }
    };
    static bool frequencySupported(uint32_t hz) { return hz >= 100000000 && hz <= 108000000; }
    // Leaves standby configured for the checked raw stream engine. The optional
    // known IMG_CAL exception is explicit and preserved in the returned result;
    // no command failure or any other chip error is tolerated.
    static Result configure(Port& port, uint32_t hz, bool allow_image_calibration_warning = false)
    {
        Result result;
        if (!frequencySupported(hz))
        {
            result.error = Error::Frequency;
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
        const uint8_t standby = 0, fsk = 2, gain = 0;
        const uint16_t cell = static_cast<uint16_t>(hz / 4000000u);
        const uint8_t calibration[] = {static_cast<uint8_t>(cell >> 8), static_cast<uint8_t>(cell), 0, 0, 0, 0};
        const uint8_t frequency[] = {static_cast<uint8_t>(hz >> 24), static_cast<uint8_t>(hz >> 16),
                                     static_cast<uint8_t>(hz >> 8), static_cast<uint8_t>(hz)};
        const uint8_t path[] = {0, 0};
        // FSK setup stages the wide converter input; its packet bitrate and
        // deviation are not interpreted as an FM IQ sample rate.
        const uint8_t modulation[] = {0, 0, 0x27, 0x10, 0, kInputCode, 0, 0x13, 0x88};
        struct Command
        {
            uint16_t opcode;
            const uint8_t* data;
            size_t bytes;
        };
        const Command commands[] = {{0x0128, &standby, 1}, {0x0111, nullptr, 0}, {0x0207, &fsk, 1}, {0x0123, calibration, sizeof(calibration)}, {0x0200, frequency, sizeof(frequency)}, {0x0201, path, sizeof(path)}, {0x021a, &gain, 1}, {0x0240, modulation, sizeof(modulation)}};
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
        result.chip_errors = (uint16_t(errors[0]) << 8) | errors[1];
        result.calibration_warning = (result.chip_errors & kImageCalibrationError) != 0;
        const uint16_t allowed = allow_image_calibration_warning ? kImageCalibrationError : 0;
        if (result.chip_errors & ~allowed) result.error = Error::ChipErrors;
        return result;
    }
};
} // namespace platform::shared::radio
