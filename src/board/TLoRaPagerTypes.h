#pragma once

#include <Arduino.h>

/* Hardware presence mask */
#define HW_RADIO_ONLINE (_BV(0))
#define HW_TOUCH_ONLINE (_BV(1))
#define HW_DRV_ONLINE (_BV(2))
#define HW_PMU_ONLINE (_BV(3))
#define HW_RTC_ONLINE (_BV(4))
#define HW_PSRAM_ONLINE (_BV(5))
#define HW_GPS_ONLINE (_BV(6))
#define HW_SD_ONLINE (_BV(7))
#define HW_NFC_ONLINE (_BV(8))
#define HW_BHI260AP_ONLINE (_BV(9))
#define HW_KEYBOARD_ONLINE (_BV(10))
#define HW_GAUGE_ONLINE (_BV(11))
#define HW_EXPAND_ONLINE (_BV(12))
#define HW_CODEC_ONLINE (_BV(13))

/* Selectively disable some initialisation */
#define NO_HW_RTC (_BV(0))
#define NO_HW_I2C_SCAN (_BV(1))
#define NO_SCAN_I2C_DEV (_BV(2))
#define NO_HW_TOUCH (_BV(3))
#define NO_HW_SENSOR (_BV(4))
#define NO_HW_NFC (_BV(5))
#define NO_HW_DRV (_BV(6))
#define NO_HW_GPS (_BV(7))
#define NO_HW_SD (_BV(8))
#define NO_HW_MIC (_BV(9))
#define NO_INIT_DELAY (_BV(10))
#define NO_HW_LORA (_BV(11))
#define NO_HW_KEYBOARD (_BV(12))
#define NO_INIT_FATFS (_BV(13))
#define NO_HW_CODEC (_BV(17))

/* Hardware interrupt mask */
#define HW_IRQ_TOUCHPAD (_BV(0))
#define HW_IRQ_RTC (_BV(1))
#define HW_IRQ_POWER (_BV(2))
#define HW_IRQ_SENSOR (_BV(3))
#define HW_IRQ_EXPAND (_BV(4))

typedef enum PowerCtrlChannel
{
    // None - for components that don't require power control
    POWER_NONE,
    // Display and touch power supply
    POWER_DISPLAY,
    // Display backlight power supply
    POWER_DISPLAY_BACKLIGHT,
    // LoRa power supply
    POWER_RADIO,
    // Touch feedback driver power supply
    POWER_HAPTIC_DRIVER,
    // Global Positioning GPS power supply
    POWER_GPS,
    // NFC power supply
    POWER_NFC,
    // SD Card power supply
    POWER_SD_CARD,
    // Audio Power Amplifier Power Supply
    POWER_SPEAK,
    // Sensor power supply
    POWER_SENSOR,
    // Keyboard power supply
    POWER_KEYBOARD,
    // Extern gpio
    POWER_EXT_GPIO,
    // Codec
    POWER_CODEC,
    // RTC
    POWER_RTC,
} PowerCtrlChannel_t;

typedef bool (*lock_callback_t)(void);
