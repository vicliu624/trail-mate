#pragma once

#include <cstdlib>
#include <cstring>

namespace platform::linux_runtime
{

// ---------------------------------------------------------------------------
// LinuxRuntimeMode
//
// Controls the composition behaviour of the Linux app facade:
//   - SimulatorDemo:  desktop simulator with synthetic peers, loopback mesh,
//                     dummy crypto, auto-replies.  Enabled by default when
//                     TRAIL_MATE_SIM_DEMO is not explicitly set to "0".
//   - DeviceLocal:    real device shell without demo data.  No fake peers,
//                     no dummy crypto.  Mesh transport is loopback or absent.
//   - DeviceRealMesh: real device with actual LoRa / BLE transport (future).
// ---------------------------------------------------------------------------

enum class LinuxRuntimeMode
{
    SimulatorDemo,
    DeviceLocal,
    DeviceRealMesh,
};

/// Resolve the intended runtime mode from environment.
///
/// If TRAIL_MATE_RUNTIME_MODE is set to "demo", "local", or "mesh", that
/// value takes precedence.
///
/// Otherwise defaults to DeviceLocal (safe for real devices).
/// The simulator shell should either set TRAIL_MATE_RUNTIME_MODE=demo or
/// call resolve_runtime_mode() and override the result at its entrypoint.
inline LinuxRuntimeMode resolve_runtime_mode()
{
    const char* mode_env = std::getenv("TRAIL_MATE_RUNTIME_MODE");
    if (mode_env && mode_env[0] != '\0')
    {
        if (std::strcmp(mode_env, "demo") == 0) return LinuxRuntimeMode::SimulatorDemo;
        if (std::strcmp(mode_env, "local") == 0) return LinuxRuntimeMode::DeviceLocal;
        if (std::strcmp(mode_env, "mesh") == 0) return LinuxRuntimeMode::DeviceRealMesh;
    }

    // Safe default: no demo data, no dummy crypto, no synthetic peers.
    return LinuxRuntimeMode::DeviceLocal;
}

/// Explicit demo-enable for development without the full simulator.
/// Respects TRAIL_MATE_DEMO_WORLD env var.
inline bool demo_world_enabled(LinuxRuntimeMode mode)
{
    if (mode == LinuxRuntimeMode::SimulatorDemo)
    {
        return true;
    }

    // Allow DeviceLocal + TRAIL_MATE_DEMO_WORLD=1 for dev testing.
    const char* demo_env = std::getenv("TRAIL_MATE_DEMO_WORLD");
    if (demo_env && demo_env[0] != '\0')
    {
        return std::strcmp(demo_env, "1") == 0 ||
               std::strcmp(demo_env, "true") == 0 ||
               std::strcmp(demo_env, "TRUE") == 0;
    }

    return false;
}

} // namespace platform::linux_runtime
