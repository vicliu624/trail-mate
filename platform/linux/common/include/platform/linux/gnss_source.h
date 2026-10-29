#pragma once
#include "gps/domain/gps_state.h"
#include "platform/linux/gpio_power.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace platform::linux_runtime
{
struct GnssSourceOptions
{
    std::string protocol = "nmea";
    std::string device;
    unsigned baud = 0;
    bool modem_lines = false;
    bool gnss_only_setup = false;
    GpioPowerOptions power;
};
bool valid_gnss_source_options(const GnssSourceOptions& options);
// Resolve an exact path or a filename pattern in one absolute directory.
// Patterns require exactly one character device. Call only from serial I/O.
std::string resolve_gnss_serial_device(const std::string& selector, int& error);
// Composition must configure before runtime observation; later changes fail.
bool configure_gnss_source(const GnssSourceOptions& options, std::shared_ptr<GpioPowerLine> power_line = {});
std::unique_ptr<AsyncGpioPower> make_gnss_power_control();
GnssSourceOptions gnss_source_options();
bool gnss_source_ready();
void publish_gnss_source_ready(bool ready);

// Nonblocking SLM command transaction. The caller owns the serial descriptor,
// writes pending bytes, and advances only the number actually accepted by write.
class NordicSlmGnss
{
  public:
    void start(bool gnss_only_setup, uint32_t now);
    void reset();
    std::string_view pending(uint32_t now);
    void wrote(std::size_t count, uint32_t now);
    bool line(std::string_view text, uint32_t now, ::gps::GpsState& fix);
    bool ready() const { return ready_; }
    bool failed() const { return failed_; }

  private:
    std::string_view command() const;
    unsigned step_ = 0;
    std::size_t offset_ = 0;
    uint32_t deadline_ = 0;
    bool setup_ = false, waiting_ = false, ready_ = false, failed_ = false;
};
} // namespace platform::linux_runtime
