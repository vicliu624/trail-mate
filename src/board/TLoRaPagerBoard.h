/**
 * @file TLoRaPagerBoard.h
 * @brief T-LoRa-Pager board hardware abstraction layer
 * 
 * This class provides a unified interface to all hardware components on the
 * LilyGo T-LoRa-Pager board, including display, GPS, LoRa, NFC, sensors, etc.
 */

#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <AW9364LedDriver.hpp>

// Power management and battery
#define XPOWERS_CHIP_BQ25896
#include <XPowersLib.h>
#include <GaugeBQ27220.hpp>

// Sensors and peripherals
#include <SensorPCF85063.hpp>
#include <SensorDRV2605.hpp>
#include <SensorBHI260AP.hpp>
#include <ExtensionIOXL9555.hpp>
#include <RadioLib.h>

// Audio codec
#include "audio/codec/esp_codec.h"

// NFC
#ifdef USING_ST25R3916
#include "board/nfc_include.h"
#endif

// Keyboard
#ifdef USING_INPUT_DEV_KEYBOARD
#include "LilyGoKeyboard.h"
#endif

#include "board/TLoRaPagerTypes.h"
#include "display/BrightnessController.h"
#include "display/DisplayInterface.h"
#include "gps/GPS.h"
#include "input/rotary/Rotary.h"
#include "pins_arduino.h"

#define newModule()   new Module(LORA_CS, LORA_IRQ, LORA_RST, LORA_BUSY)

/**
 * @class TLoRaPagerBoard
 * @brief Main board class for T-LoRa-Pager hardware
 * 
 * This class manages all hardware components on the T-LoRa-Pager board.
 * It provides initialization, control, and status query functions for each component.
 */
class TLoRaPagerBoard : public LilyGo_Display,
                        public LilyGoDispArduinoSPI,
                        public BrightnessController<TLoRaPagerBoard, 0, 16, 50>
{
public:
    /**
     * @brief Get the singleton instance of TLoRaPagerBoard
     * @return Pointer to the singleton instance
     */
    static TLoRaPagerBoard *getInstance();

    /**
     * @brief Initialize the board and all hardware components
     * @param disable_hw_init Bitmask to disable specific hardware initialization
     *                        Use NO_HW_* flags from TLoRaPagerTypes.h
     * @return Bitmask indicating which hardware components are online (HW_* flags)
     */
    uint32_t begin(uint32_t disable_hw_init = 0);
    
    /**
     * @brief Main loop function - call this periodically in your main loop
     * 
     * This function processes background tasks such as NFC worker.
     */
    void loop();

    // Hardware initialization functions
    bool initPMU();
    bool initGPS();
    bool initLoRa();
    bool initNFC();
    bool initKeyboard();
    bool initDrv();
    bool initSensor();
    bool initRTC();
    bool installSD();
    void uninstallSD();
    bool isCardReady();

    // Power control
    void powerControl(PowerCtrlChannel_t ch, bool enable);

    // Display
    void setBrightness(uint8_t level);
    uint8_t getBrightness();
    void setRotation(uint8_t rotation) override;
    uint8_t getRotation() override;
    uint16_t width() override;
    uint16_t height() override;
    void pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color) override;

    // Rotary encoder
    bool hasEncoder() override;
    RotaryMsg_t getRotary() override;
    void feedback(void *args = NULL) override;

    // Touch (not available on T-LoRa-Pager)
    bool hasTouch() override { return false; }

    // Haptic feedback
    /**
     * @brief Trigger haptic feedback (vibration)
     */
    void vibrator();
    
    /**
     * @brief Set haptic effect waveform
     * @param effects Effect number (0-127, see DRV2605 documentation)
     */
    void setHapticEffects(uint8_t effects);
    
    /**
     * @brief Get current haptic effect
     * @return Effect number (0-127)
     */
    uint8_t getHapticEffects();

    // Keyboard
    bool hasKeyboard() override;
    int getKey(char *c);
    int getKeyChar(char *c) override;

    // NFC functions
#ifdef USING_ST25R3916
    /**
     * @brief Start NFC discovery mode
     * @param techs2Find Technologies to find (e.g., RFAL_NFC_POLL_TECH_A)
     * @param totalDuration Total discovery duration in ms
     * @return true if successful, false otherwise
     */
    bool startNFCDiscovery(uint8_t techs2Find = RFAL_NFC_POLL_TECH_A, uint16_t totalDuration = 1000);
    
    /**
     * @brief Stop NFC discovery mode
     */
    void stopNFCDiscovery();
    
    /**
     * @brief Check if NFC is ready
     * @return true if NFC is initialized and online
     */
    bool isNFCReady() const { return (devices_probe & HW_NFC_ONLINE) != 0; }
#endif

    // Hardware status queries
    /**
     * @brief Get hardware detection status bitmask
     * @return Bitmask with HW_* flags indicating which devices are online
     */
    uint32_t getDevicesProbe() const { return devices_probe; }
    
    /**
     * @brief Check if a specific hardware component is online
     * @param flag Hardware flag (e.g., HW_GPS_ONLINE, HW_LORA_ONLINE)
     * @return true if the hardware is online, false otherwise
     */
    bool isHardwareOnline(uint32_t flag) const { return (devices_probe & flag) != 0; }
    
    /**
     * @brief Check if GPS is initialized and online
     */
    bool isGPSReady() const { return isHardwareOnline(HW_GPS_ONLINE); }
    
    /**
     * @brief Check if LoRa is initialized and online
     */
    bool isLoRaReady() const { return isHardwareOnline(HW_RADIO_ONLINE); }
    
    /**
     * @brief Check if SD card is ready
     */
    bool isSDReady() const { return isHardwareOnline(HW_SD_ONLINE); }
    
    /**
     * @brief Check if RTC is initialized and online
     */
    bool isRTCReady() const { return isHardwareOnline(HW_RTC_ONLINE); }
    
    /**
     * @brief Check if sensor is initialized and online
     */
    bool isSensorReady() const { return isHardwareOnline(HW_BHI260AP_ONLINE); }
    
    /**
     * @brief Check if haptic driver is initialized and online
     */
    bool isHapticReady() const { return isHardwareOnline(HW_DRV_ONLINE); }
    
    /**
     * @brief Check if PMU is initialized and online
     */
    bool isPMUReady() const { return isHardwareOnline(HW_PMU_ONLINE); }
    
    /**
     * @brief Check if battery gauge is initialized and online
     */
    bool isGaugeReady() const { return isHardwareOnline(HW_GAUGE_ONLINE); }

    // Public hardware instances
    GPS gps;
    SensorBHI260AP sensor;
    SensorPCF85063 rtc;
    SensorDRV2605 drv;
    GaugeBQ27220 gauge;
    AW9364LedDriver backlight;
    PowersBQ25896 pmu;
    Rotary rotary = Rotary(ROTARY_A, ROTARY_B);
    ExtensionIOXL9555 io;

#ifdef USING_INPUT_DEV_KEYBOARD
    LilyGoKeyboard kb;
#endif

#ifdef USING_ST25R3916
    RfalNfcClass *nfc;
#endif

#ifdef USING_AUDIO_CODEC
    EspCodec codec;
#endif

#if defined(ARDUINO_LILYGO_LORA_SX1262)
    SX1262 radio = newModule();
#elif defined(ARDUINO_LILYGO_LORA_SX1280)
    SX1280 radio = newModule();
#elif defined(ARDUINO_LILYGO_LORA_CC1101)
    CC1101 radio = newModule();
#elif defined(ARDUINO_LILYGO_LORA_LR1121)
    LR1121 radio = newModule();
#elif defined(ARDUINO_LILYGO_LORA_SI4432)
    Si4432 radio = newModule();
#endif

private:
    // Singleton pattern - prevent copy and assignment
    TLoRaPagerBoard();
    ~TLoRaPagerBoard();
    TLoRaPagerBoard(const TLoRaPagerBoard &) = delete;
    TLoRaPagerBoard &operator=(const TLoRaPagerBoard &) = delete;

    /**
     * @brief Initialize shared SPI bus CS pins
     */
    void initShareSPIPins();
    
    /**
     * @brief Rotary encoder task (FreeRTOS task)
     * @param p Task parameter (unused)
     */
    static void rotaryTask(void *p);

    uint32_t devices_probe = 0;      ///< Hardware detection status bitmask
    uint8_t _haptic_effects = 15;    ///< Current haptic effect (default: 15 = strong click)
};

extern TLoRaPagerBoard &instance;

#define DEVICE_MAX_BRIGHTNESS_LEVEL 16
#define DEVICE_MIN_BRIGHTNESS_LEVEL 0
