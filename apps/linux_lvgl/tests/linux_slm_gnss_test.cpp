#include "platform/linux/gnss_source.h"
#include <cmath>
#include <stdexcept>
#include <string>
using platform::linux_runtime::NordicSlmGnss;
void require(bool value)
{
    if (!value) throw std::runtime_error("SLM contract failed");
}
void exchange(NordicSlmGnss& session, const char* command, uint32_t now, const char* response = "OK")
{
    const auto text = session.pending(now);
    require(text == command);
    session.wrote(1, now);
    require(session.pending(now) == std::string_view(command).substr(1));
    session.wrote(text.size() - 1, now);
    require(session.pending(now).empty());
    ::gps::GpsState fix;
    require(!session.line(command, now + 1, fix));
    require(!session.line(response, now + 1, fix));
}
int main()
{
    NordicSlmGnss session;
    session.start(true, 100);
    exchange(session, "AT\r\n", 100);
    exchange(session, "AT#XGPS=0\r\n", 200, "ERROR");
    exchange(session, "AT+CFUN=0\r\n", 300);
    exchange(session, "AT%XSYSTEMMODE=0,0,1,0\r\n", 400);
    exchange(session, "AT+CFUN=31\r\n", 500);
    exchange(session, "AT#XGPS=1,0,1\r\n", 600);
    require(session.ready() && session.pending(10000).empty());
    ::gps::GpsState fix;
    require(session.line("#XGPS: -35.457576,139.625090,121.473785,22.199919,0.442868,69.0,\"2021-06-02 06:25:48\"", 700, fix));
    require(fix.valid && fix.has_speed && fix.has_alt && fix.has_course && fix.satellites == 0);
    require(std::abs(fix.lat + 35.457576) < 1e-8 && std::abs(fix.speed_mps - 0.442868) < 1e-8);
    require(!session.line("#XGPS: 1,1", 800, fix));
    require(!session.line("#XGPS: nan,0,0,1,0,0,\"2021-06-02 06:25:48\"", 800, fix));
    require(!session.line("#XGPS: 91,0,0,1,0,0,\"2021-06-02 06:25:48\"", 800, fix));
    require(!session.line("#XGPS: 0,0,0,1,0,0,\"2021-02-29 06:25:48\"", 800, fix));
    require(!session.line("#XGPS: 0,0,0,1,0,0,\"2021-06-02 24:25:48\"", 800, fix));
    require(!session.line("#XGPS: 1,0", 800, fix) && !session.ready() && session.failed());
    session.start(false, 1000);
    exchange(session, "AT\r\n", 1000);
    exchange(session, "AT#XGPS=0\r\n", 1100);
    exchange(session, "AT#XGPS=1,0,1\r\n", 1200);
    require(session.ready());
    session.start(false, 2000);
    require(session.pending(5000).empty() && session.failed());
    session.start(false, 0xfffffff0U);
    require(!session.pending(100).empty());
    require(session.pending(4000).empty() && session.failed());
    session.start(false, 100);
    session.wrote(session.pending(100).size(), 100);
    session.line("OK", 3101, fix);
    require(session.failed());
    using namespace platform::linux_runtime;
    GnssSourceOptions options;
    require(valid_gnss_source_options(options));
    options.protocol = "nordic_slm";
    require(!valid_gnss_source_options(options));
    options.device = "/dev/test-modem";
    options.baud = 115200;
    require(configure_gnss_source(options));
    require(!configure_gnss_source(options) && !gnss_source_ready());
}
