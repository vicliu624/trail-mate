/**
 * @file radio_owner.h
 * @brief Radio (LoRa SX126x) ownership model for ESP-IDF targets.
 *
 * Problem on Tab5 / T-Display P4:
 * The Sx126xRadio mutex protects individual SPI transactions, but does not
 * prevent higher-level mode conflicts: the mesh runtime, walkie-talkie,
 * SSTV decoder, energy sweep, and diagnostics can all contend for exclusive
 * radio access.  A raw mutex per-call does not answer "who owns receive mode
 * right now, and who is allowed to reconfigure frequency/modulation."
 *
 * This header defines a lightweight RadioOwner / RadioSession contract.
 * It does NOT enforce ownership — that is the caller's responsibility.
 * It provides the vocabulary and a skeletal session class so that each
 * feature runtime can declare intent before touching the radio.
 *
 * Phase 1 (current): Documentation + owner self-identification in logs.
 * Phase 2 (after UI stability): RadioSession enforces exclusive ownership.
 */

#pragma once

#include <cstdint>

namespace platform::esp::idf_common
{

/// Who currently claims the radio.
enum class RadioOwner : std::uint8_t
{
    None = 0,
    Mesh,        // Meshtastic / MeshCore / RNode / LXMF adapter
    Walkie,      // Walkie-talkie RX/TX
    Sstv,        // SSTV reception
    Diagnostics, // Energy sweep / spectrum scan
    Gps,         // GNSS-assisted time / position over radio
    Boot,        // Startup probe (should be removed in stable builds)
};

/// Lightweight session handle.  Create on the stack; release in dtor.
class RadioSession
{
  public:
    RadioSession(RadioOwner owner, const char* source_file, int source_line);
    ~RadioSession();

    RadioSession(const RadioSession&) = delete;
    RadioSession& operator=(const RadioSession&) = delete;

    /// Log the owner context.  Call before any radio operation.
    /// Returns false if another owner is already active (future enforcement).
    bool enter();

    /// Release the session.  Safe to call multiple times.
    void leave();

  private:
    RadioOwner owner_;
    const char* source_file_;
    int source_line_;
    bool active_{false};
};

} // namespace platform::esp::idf_common

// Convenience macro for stack-based session logging.
// Usage:  RADIO_SESSION(RadioOwner::Mesh);
// Expands to a local variable that logs enter/leave around the scope.
#define RADIO_SESSION(owner) \
    ::platform::esp::idf_common::RadioSession _radio_sess(owner, __FILE__, __LINE__)
