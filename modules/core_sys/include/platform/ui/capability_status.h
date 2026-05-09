#pragma once

// ---------------------------------------------------------------------------
// CapabilityState / CapabilityStatus
//
// Honest capability signalling shared between ESP and Linux lines.
// Every runtime contract in platform::ui::* can expose a CapabilityStatus
// so the UI can distinguish "unsupported", "simulated", "real", "degraded",
// and "error" without platform-specific knowledge.
// ---------------------------------------------------------------------------

namespace platform::ui
{

enum class CapabilityState
{
    /// No code path exists for this capability on the current target.
    Unsupported,

    /// A synthetic / env-driven implementation exists for development and
    /// simulator use.  Must NOT be presented as real hardware by default.
    Simulated,

    /// Real hardware or OS path is available and behaving normally.
    Available,

    /// Real hardware is present but running in a reduced mode
    /// (e.g. GPS with no fix, LoRa with poor SNR, battery unknown).
    Degraded,

    /// The capability was available but has encountered a runtime error.
    Error,
};

struct CapabilityStatus
{
    CapabilityState state{CapabilityState::Unsupported};
    const char* message{nullptr}; // human-readable, may be nullptr
};

/// Helper: human-readable label for a CapabilityState.
inline const char* capability_state_label(CapabilityState state)
{
    switch (state)
    {
    case CapabilityState::Unsupported:
        return "Unsupported";
    case CapabilityState::Simulated:
        return "Simulated";
    case CapabilityState::Available:
        return "Available";
    case CapabilityState::Degraded:
        return "Degraded";
    case CapabilityState::Error:
        return "Error";
    }
    return "Unknown";
}

} // namespace platform::ui
