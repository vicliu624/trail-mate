#pragma once

// In Arduino environments we use <Arduino.h> for basic types and helpers.
// In non-Arduino builds, fall back to standard headers.
#if __has_include(<Arduino.h>)
#include <Arduino.h>
#else
#include <stdbool.h>
#include <stdint.h>
#endif

// Abstract base class: provides a unified interface for different hardware boards.
// Only keeps the minimal set actually used by the application layer to avoid tight coupling.
class BoardBase
{
  public:
    virtual ~BoardBase() = default;

    // Lifecycle / power management
    virtual uint32_t begin(uint32_t disable_hw_init = 0) = 0;
    virtual void wakeUp() = 0;
    virtual void handlePowerButton() = 0;
    virtual void softwareShutdown() = 0;

    // Turn off peripheral power when screen sleeps, and restore on wake; default no-op.
    virtual void enterScreenSleep() {}
    virtual void exitScreenSleep() {}

    // Low-battery tiers: 0=Normal, 1=Low (<=20%), 2=Critical (<=10%); used for brightness/GPS policies.
    virtual void setPowerTier(int tier)
    {
        (void)tier;
    }
    virtual int getPowerTier() const
    {
        return 0;
    }

    // Display / brightness
    virtual void setBrightness(uint8_t level) = 0;
    virtual uint8_t getBrightness() = 0;

    // Keyboard backlight (may be a stub on boards without keyboard)
    virtual bool hasKeyboard() = 0;
    virtual void keyboardSetBrightness(uint8_t level) = 0;
    virtual uint8_t keyboardGetBrightness() = 0;

    // Sensors and power state
    virtual bool isRTCReady() const = 0;
    virtual bool isCharging() = 0;
    virtual int getBatteryLevel() = 0;

    // Storage / peripheral state
    virtual bool isSDReady() const = 0;
    virtual bool isCardReady() = 0;
    virtual bool isGPSReady() const = 0;
    virtual bool hasGPSHardware() const
    {
        return false;
    }
    virtual bool hasSstvAudioInput() const
    {
        return false;
    }

    // Haptic feedback
    virtual void vibrator() = 0;
    virtual void stopVibrator() = 0;

    // Incoming-message tone (default no-op; boards can override if needed)
    virtual void playMessageTone() {}

    // System notification volume (0-100, default 45)
    virtual void setMessageToneVolume(uint8_t volume_percent)
    {
        (void)volume_percent;
    }
    virtual uint8_t getMessageToneVolume() const
    {
        return 45;
    }
};

#ifndef DEVICE_MAX_BRIGHTNESS_LEVEL
#define DEVICE_MAX_BRIGHTNESS_LEVEL 16
#endif
#ifndef DEVICE_MIN_BRIGHTNESS_LEVEL
#define DEVICE_MIN_BRIGHTNESS_LEVEL 0
#endif

extern BoardBase& board;
