#include "linux_radiolib_hal.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/spi/spidev.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>

namespace platform::linux_runtime
{
LinuxRadioLibHal::LinuxRadioLibHal(PacketRadioHardware hardware)
    : RadioLibHal(0, 1, 0, 1, 1, 2), hardware_(std::move(hardware)) {}

LinuxRadioLibHal::~LinuxRadioLibHal() { term(); }

void LinuxRadioLibHal::fail(const char* operation)
{
    if (error_.empty()) error_ = std::string(operation) + ": " + std::strerror(errno);
}

void LinuxRadioLibHal::init()
{
    if (spi_ >= 0) return;
    error_.clear();
    busy_since_ = {};
    chip_ = open(hardware_.bus.gpiochip.c_str(), O_RDWR | O_CLOEXEC);
    if (chip_ < 0)
    {
        fail("open gpiochip");
        return;
    }
    spi_ = open(hardware_.bus.spi_device.c_str(), O_RDWR | O_CLOEXEC);
    if (spi_ < 0)
    {
        fail("open spidev");
        term();
        return;
    }
    if (flock(spi_, LOCK_EX | LOCK_NB) != 0)
    {
        fail("lock spidev");
        term();
        return;
    }
    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    auto speed = hardware_.bus.spi_speed_hz;
    if (ioctl(spi_, SPI_IOC_WR_MODE, &mode) || ioctl(spi_, SPI_IOC_WR_BITS_PER_WORD, &bits) ||
        ioctl(spi_, SPI_IOC_WR_MAX_SPEED_HZ, &speed))
    {
        fail("configure SPI");
        term();
        return;
    }
    if (hardware_.bus.power_gpio >= 0)
    {
        pinMode(hardware_.bus.power_gpio, GpioModeOutput);
        digitalWrite(hardware_.bus.power_gpio, GpioLevelHigh);
        delay(10);
    }
    pinMode(hardware_.bus.reset_gpio, GpioModeOutput);
    pinMode(hardware_.bus.busy_gpio, GpioModeInput);
    if (!healthy()) term();
}

void LinuxRadioLibHal::term()
{
    iq_mode_ = false;
    if (pins_.count(hardware_.bus.reset_gpio)) digitalWrite(hardware_.bus.reset_gpio, GpioLevelHigh);
    if (hardware_.bus.power_gpio >= 0 && pins_.count(hardware_.bus.power_gpio))
        digitalWrite(hardware_.bus.power_gpio, GpioLevelLow);
    for (const auto& pin : pins_) close(pin.second);
    pins_.clear();
    if (spi_ >= 0)
    {
        close(spi_);
        spi_ = -1;
    }
    if (chip_ >= 0)
    {
        close(chip_);
        chip_ = -1;
    }
}

void LinuxRadioLibHal::pinMode(uint32_t pin, uint32_t mode)
{
    if (pin == RADIOLIB_NC || pin == kKernelChipSelect || pins_.count(pin)) return;
    if (chip_ < 0)
    {
        errno = EBADF;
        fail("GPIO before initialization");
        return;
    }
    gpio_v2_line_request request{};
    request.offsets[0] = pin;
    request.num_lines = 1;
    request.config.flags = mode == GpioModeOutput ? GPIO_V2_LINE_FLAG_OUTPUT : GPIO_V2_LINE_FLAG_INPUT;
    if (mode == GpioModeOutput)
    {
        request.config.num_attrs = 1;
        request.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
        request.config.attrs[0].attr.values = 1;
        request.config.attrs[0].mask = 1;
    }
    std::strncpy(request.consumer, "trailmate-radio", sizeof(request.consumer) - 1);
    if (ioctl(chip_, GPIO_V2_GET_LINE_IOCTL, &request) != 0)
    {
        fail("request GPIO");
        return;
    }
    pins_[pin] = request.fd;
}

void LinuxRadioLibHal::digitalWrite(uint32_t pin, uint32_t value)
{
    if (pin == RADIOLIB_NC || pin == kKernelChipSelect) return;
    const auto found = pins_.find(pin);
    if (found == pins_.end())
    {
        errno = EBADF;
        fail("write unopened GPIO");
        return;
    }
    gpio_v2_line_values values{};
    values.mask = 1;
    values.bits = value ? 1 : 0;
    if (ioctl(found->second, GPIO_V2_LINE_SET_VALUES_IOCTL, &values) != 0) fail("write GPIO");
}

uint32_t LinuxRadioLibHal::digitalRead(uint32_t pin)
{
    if (pin == RADIOLIB_NC) return 0;
    // Some upstream calibration loops have no timeout. Latch a transport error
    // and let the loop unwind; the owning driver rejects the operation.
    if (!healthy()) return 0;
    const auto found = pins_.find(pin);
    if (found == pins_.end())
    {
        errno = EBADF;
        fail("read unopened GPIO");
        return 0;
    }
    gpio_v2_line_values values{};
    values.mask = 1;
    if (ioctl(found->second, GPIO_V2_LINE_GET_VALUES_IOCTL, &values) != 0)
    {
        fail("read GPIO");
        return 0;
    }
    if (pin == static_cast<uint32_t>(hardware_.bus.busy_gpio))
    {
        if (!(values.bits & 1)) busy_since_ = {};
        else if (busy_since_ == std::chrono::steady_clock::time_point{})
            busy_since_ = std::chrono::steady_clock::now();
        else if (std::chrono::steady_clock::now() - busy_since_ > std::chrono::milliseconds(2500))
        {
            errno = ETIMEDOUT;
            fail("radio BUSY stuck high");
            return 0;
        }
    }
    return values.bits & 1;
}

bool LinuxRadioLibHal::waitReady()
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
    while (healthy() && digitalRead(hardware_.bus.busy_gpio))
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            errno = ETIMEDOUT;
            fail("radio BUSY");
            return false;
        }
        delayMicroseconds(20);
    }
    return healthy();
}

void LinuxRadioLibHal::spiTransfer(uint8_t* output, size_t size, uint8_t* input)
{
    if (input) std::memset(input, 0, size);
    if (!size || size > 4096 || !output || !input)
    {
        errno = EINVAL;
        fail("invalid SPI frame");
        return;
    }
    if (!healthy() || !waitReady()) return;
    spi_ioc_transfer transfer{};
    transfer.tx_buf = reinterpret_cast<uintptr_t>(output);
    transfer.rx_buf = reinterpret_cast<uintptr_t>(input);
    transfer.len = static_cast<uint32_t>(size);
    transfer.speed_hz = spiClockHz();
    transfer.bits_per_word = 8;
    if (ioctl(spi_, SPI_IOC_MESSAGE(1), &transfer) != static_cast<int>(size))
    {
        fail("SPI transfer");
        return;
    }
    // BUSY may assert after NSS rises. Do not require observing its high edge;
    // short pulses may already be over when userspace is scheduled again.
    delayMicroseconds(iq_mode_ ? 1 : 100);
}

bool LinuxRadioLibHal::setIqTransferMode(bool enabled)
{
    if (spi_ < 0) return !enabled;
    if (enabled && !healthy()) return false;
    uint32_t speed = enabled ? 16000000u : hardware_.bus.spi_speed_hz;
    // Keep the requested mode for cleanup even if ioctl reports an error.
    // The checked stream always resets/reopens on setup or bus-restore failure.
    iq_mode_ = enabled;
    if (ioctl(spi_, SPI_IOC_WR_MAX_SPEED_HZ, &speed) != 0)
    {
        fail(enabled ? "IQ SPI clock" : "restore packet SPI clock");
        return false;
    }
    uint32_t actual = 0;
    if (ioctl(spi_, SPI_IOC_RD_MAX_SPEED_HZ, &actual) != 0 || actual != speed)
    {
        errno = EIO;
        fail("SPI clock readback");
        return false;
    }
    return true;
}

void LinuxRadioLibHal::delay(RadioLibTime_t value) { std::this_thread::sleep_for(std::chrono::milliseconds(value)); }
void LinuxRadioLibHal::delayMicroseconds(RadioLibTime_t value)
{
    // RadioLib also requests 1 us between command/status phases. A userspace
    // sleep can overshoot that by a scheduler quantum. Bound short IQ waits
    // with the steady clock; keep packet timing and all longer waits unchanged.
    if (iq_mode_ && value <= 100)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(value);
        while (std::chrono::steady_clock::now() < deadline)
        {
        }
    }
    else std::this_thread::sleep_for(std::chrono::microseconds(value));
}
RadioLibTime_t LinuxRadioLibHal::millis()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_).count();
}
RadioLibTime_t LinuxRadioLibHal::micros()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started_).count();
}
void LinuxRadioLibHal::yield() { delayMicroseconds(20); }
void LinuxRadioLibHal::spiBegin() { init(); }
void LinuxRadioLibHal::spiEnd() { term(); }
void LinuxRadioLibHal::spiBeginTransaction() {}
void LinuxRadioLibHal::spiEndTransaction() {}
// The packet backend polls chip IRQ flags; it never promises GPIO callbacks.
void LinuxRadioLibHal::attachInterrupt(uint32_t, void (*)(void), uint32_t) {}
void LinuxRadioLibHal::detachInterrupt(uint32_t) {}
long LinuxRadioLibHal::pulseIn(uint32_t, uint32_t, RadioLibTime_t) { return 0; }
} // namespace platform::linux_runtime
