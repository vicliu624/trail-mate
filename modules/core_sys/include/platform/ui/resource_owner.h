/**
 * @file resource_owner.h
 * @brief Shared resource-ownership vocabulary for Linux and ESP-IDF lines.
 *
 * Both platforms need to reason about which subsystem currently owns a
 * shared hardware or software resource (radio, display-backlight, power
 * rail, I2C bus).  These types provide a common language.
 *
 * Concrete enforcement lives in platform implementations
 * (platform/esp/idf_common/radio_owner.h, platform/linux/...).
 */

#pragma once

#include <cstdint>

namespace platform::ui
{

/// Categories of shared resources that require explicit ownership.
enum class ResourceKind : std::uint8_t
{
    Radio,     // LoRa / SX126x / SX1280 transceiver
    Display,   // LVGL display and backlight control
    PowerRail, // External 5 V / switched power rail (Tab5 Ext5V, etc.)
    I2cBus,    // Shared I2C bus (touch, expander, RTC, sensors)
    SpiBus,    // Shared SPI bus (display, SD, radio)
    Gps,       // GNSS receiver (serial or I2C)
    Audio,     // Audio codec / I2S
};

/// Lightweight ownership token – used for logging and diagnostics.
/// Not a lock; the platform implementation enforces the actual exclusion.
struct OwnerToken
{
    ResourceKind resource{ResourceKind::Radio};
    const char* label{nullptr}; // e.g. "Mesh", "Walkie", "GPS"
    const char* source_file{nullptr};
    int source_line{0};
};

} // namespace platform::ui
