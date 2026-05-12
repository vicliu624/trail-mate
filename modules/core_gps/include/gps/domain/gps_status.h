#pragma once

#include "gps/domain/gnss_satellite.h"
#include "gps/domain/gps_diagnostics.h"

namespace gps
{

struct GpsStatusSnapshot
{
    bool supported = false;
    bool enabled = false;
    bool powered = false;
    bool ready = false;
    bool has_fix = false;
    uint8_t satellites = 0;
    GnssStatus gnss{};
    GpsDiagnosticCode diagnostic = GpsDiagnosticCode::Disabled;
};

inline GpsStatusSnapshot gpsStatusFromDiagnostics(const GpsDiagnosticsSnapshot& diagnostics,
                                                  const GnssStatus& gnss = GnssStatus{})
{
    GpsStatusSnapshot status{};
    status.supported = diagnostics.supported;
    status.enabled = diagnostics.enabled;
    status.powered = diagnostics.powered;
    status.ready = diagnostics.ready;
    status.has_fix = diagnostics.has_fix;
    status.satellites = diagnostics.satellites;
    status.gnss = gnss;
    status.diagnostic = diagnostics.code;
    return status;
}

} // namespace gps
