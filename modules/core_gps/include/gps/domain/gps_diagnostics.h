#pragma once

#include <cstdint>

namespace gps
{

enum class GpsDiagnosticCode : uint8_t
{
    OK = 0,
    Disabled,
    NotEnabled,
    PowerOff,
    TransportNotReady,
    NoTraffic,
    TrafficStalled,
    NoFix,
    NotConfigured,
};

struct GpsDiagnosticsSnapshot
{
    bool supported = true;
    bool enabled = false;
    bool powered = false;
    bool ready = false;
    bool has_fix = false;
    uint8_t satellites = 0;
    uint8_t sats_in_view = 0;
    uint8_t sats_in_use = 0;
    uint32_t chars_total = 0;
    uint32_t chars_recent = 0;
    uint32_t last_rx_age_ms = 0xFFFFFFFFUL;
    uint32_t poll_interval_ms = 0;
    uint32_t collection_interval_ms = 0;
    GpsDiagnosticCode code = GpsDiagnosticCode::OK;
};

inline const char* gpsDiagnosticCodeName(GpsDiagnosticCode code)
{
    switch (code)
    {
    case GpsDiagnosticCode::OK:
        return "GPSD_OK";
    case GpsDiagnosticCode::Disabled:
        return "GPSD_DISABLED";
    case GpsDiagnosticCode::NotEnabled:
        return "GPSD_NOT_ENABLED";
    case GpsDiagnosticCode::PowerOff:
        return "GPSD_POWER_OFF";
    case GpsDiagnosticCode::TransportNotReady:
        return "GPSD_TRANSPORT_NOT_READY";
    case GpsDiagnosticCode::NoTraffic:
        return "GPSD_NO_UART_TRAFFIC";
    case GpsDiagnosticCode::TrafficStalled:
        return "GPSD_UART_TRAFFIC_STALLED";
    case GpsDiagnosticCode::NoFix:
        return "GPSD_NO_FIX";
    case GpsDiagnosticCode::NotConfigured:
        return "GPSD_NOT_CONFIGURED";
    }
    return "GPSD_UNKNOWN";
}

} // namespace gps
