/**
 * @file TLoRaPagerBoard.h
 * @brief T-LoRa-Pager board hardware abstraction layer
 *
 * This class provides a unified interface to all hardware components on the
 * LilyGo T-LoRa-Pager board, including display, GPS, LoRa, NFC, sensors, etc.
 */

#pragma once

#include <AW9364LedDriver.hpp>
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <memory>

#include "BoardBase.h"
#include "GpsBoard.h"
#include "LoraBoard.h"
#include "MotionBoard.h"
#include "SdBoard.h"

// Forward declaration to avoid circular includes
namespace app
{
class AppContext;
}

// Power management and battery
#define XPOWERS_CHIP_BQ25896
#include <GaugeBQ27220.hpp>
#include <XPowersLib.h>

// Sensors and peripherals
#include <ExtensionIOXL9555.hpp>
#include <RadioLib.h>
#include <SensorBHI260AP.hpp>
#include <SensorDRV2605.hpp>
#include <SensorPCF85063.hpp>

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

#define newModule() new Module(LORA_CS, LORA_IRQ, LORA_RST, LORA_BUSY)

/**
 * @class TLoRaPagerBoard
 * @brief Main board class for T-LoRa-Pager hardware
 *
 * This class manages all hardware components on the T-LoRa-Pager board.
 * It provides initialization, control, and status query functions for each component.
 */
class TLoRaPagerBoard : public BoardBase,
                        public LoraBoard,
                        public GpsBoard,
                        public MotionBoard,
                        public SdBoard,
                        public LilyGo_Display,
                        public LilyGoDispArduinoSPI,
                        public BrightnessController<TLoRaPagerBoard, 0, 16, 50>
{
  public:
    /**
     * @brief Get the singleton instance of TLoRaPagerBoard
     * @return Pointer to the singleton instance
     */
    static TLoRaPagerBoard* getInstance();

    /**
     * @brief Initialize the board and all hardware components
     * @param disable_hw_init Bitmask to disable specific hardware initialization
     *                        Use NO_HW_* flags from TLoRaPagerTypes.h
     * @return Bitmask indicating which hardware components are online (HW_* flags)
     */
    uint32_t begin(uint32_t disable_hw_init = 0) override;

    /**
     * @brief Main loop function - call this periodically in your main loop
     *
     * This function processes background tasks such as NFC worker.
     */
    void loop();

    // Hardware initialization functions
    bool initPMU();
    bool initGPS() override;
    bool initLoRa();
    bool initNFC();
    bool initKeyboard();
    bool initDrv();
    bool initSensor();
    bool initRTC();
    bool installSD();
    void uninstallSD();
    bool isCardReady() override;

    /**
     * @brief Sync RTC time from GPS (if GPS time is valid)
     * @param gps_task_interval_ms Optional: GPS task interval in milliseconds (for delay compensation)
     * @return true if time was synced successfully, false otherwise
     *
     * This function checks if GPS date and time are valid, and if RTC is ready.
     * If both conditions are met, it sets the RTC time from GPS with delay compensation.
     * Can be called from GPS task or from UI (e.g., clock settings page).
     *
     * Delay compensation accounts for:
     * - GPS NMEA message age (typically 0.5-2 seconds)
     * - GPS task interval (if provided, for better accuracy)
     * - Processing and RTC write delays
     */
    bool syncTimeFromGPS(uint32_t gps_task_interval_ms = 0) override;

    /**
     * @brief Update GPS online flag
     * @param online true to set HW_GPS_ONLINE, false to clear it
     */
    void setGPSOnlineInternal(bool online)
    {
        if (online)
        {
            devices_probe |= HW_GPS_ONLINE;
        }
        else
        {
            devices_probe &= ~HW_GPS_ONLINE;
        }
    }

    // Power control
    void powerControl(PowerCtrlChannel_t ch, bool enable) override;

    // Power button management
    /**
     * @brief Initialize power button handling
     * @return true if initialization successful, false otherwise
     */
    bool initPowerButton();

    /**
     * @brief Handle power button press (call this in main loop)
     * Processes power button events for wake/shutdown functionality
     */
    void handlePowerButton() override;

    /**
     * @brief Shutdown the device safely
     * @param save_data If true, save application data before shutdown
     */
    void shutdown(bool save_data = true);

    /**
     * @brief Software-initiated shutdown (for UI buttons, etc.)
     * Checks shutdown conditions before proceeding
     */
    void softwareShutdown() override;

    /**
     * @brief Wake up the device from sleep
     */
    void wakeUp() override;

    // Display
    void setBrightness(uint8_t level) override;
    uint8_t getBrightness() override;
    void setRotation(uint8_t rotation) override;
    uint8_t getRotation() override;
    uint16_t width() override;
    uint16_t height() override;
    void pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t* color) override;

    // Rotary encoder
    bool hasEncoder() override;
    RotaryMsg_t getRotary() override;
    void feedback(void* args = NULL) override;

    // Touch (not available on T-LoRa-Pager)
    bool hasTouch() override { return false; }

    // Haptic feedback
    /**
     * @brief Trigger haptic feedback (vibration)
     */
    void vibrator() override;

    /**
     * @brief Stop haptic feedback
     */
    void stopVibrator() override;

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
    void keyboardSetBrightness(uint8_t level) override;
    uint8_t keyboardGetBrightness() override;
    int getKey(char* c);
    int getKeyChar(char* c) override;

    // NFC functions
#ifdef USING_ST25R3916
    /**
     * @brief Start NFC discovery mode
     * @param techs2Find Technologies to find (e.g., RFAL_NFC_POLL_TECH_A)
     * @param totalDuration Total discovery duration in ms
     * @return true if successful, false otherwise
     */
    bool startNFCDiscovery(uint16_t techs2Find = RFAL_NFC_POLL_TECH_A, uint16_t totalDuration = 1000);

    /**
     * @brief Stop NFC discovery mode
     */
    void stopNFCDiscovery();

    /**
     * @brief Poll NFC interrupt registers (IRQ line free)
     */
    void pollNfcIrq();

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
    bool isGPSReady() const override { return isHardwareOnline(HW_GPS_ONLINE); }

    /**
     * @brief Check if LoRa is initialized and online
     */
    bool isRadioOnline() const override { return isHardwareOnline(HW_RADIO_ONLINE); }
    int transmitRadio(const uint8_t* data, size_t len) override;
    int startRadioReceive() override;
    uint32_t getRadioIrqFlags() override;
    int getRadioPacketLength(bool update) override;
    int readRadioData(uint8_t* buf, size_t len) override;
    void clearRadioIrqFlags(uint32_t flags) override;
    void configureLoraRadio(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr_denom,
                            int8_t tx_power, uint16_t preamble_len, uint8_t sync_word,
                            uint8_t crc_len) override;

    // GpsBoard
    void setGPSOnline(bool online) override { setGPSOnlineInternal(online); }
    GPS& getGPS() override { return gps; }

    // MotionBoard
    SensorBHI260AP& getMotionSensor() override { return sensor; }
    bool isSensorReady() const override { return isHardwareOnline(HW_BHI260AP_ONLINE); }

    /**
     * @brief Check if SD card is ready
     */
    bool isSDReady() const override { return isHardwareOnline(HW_SD_ONLINE); }

    /**
     * @brief Check if RTC is initialized and online
     */
    bool isRTCReady() const override { return isHardwareOnline(HW_RTC_ONLINE); }

    /**
     * @brief Get current time string from RTC
     * @param buffer Buffer to store the time string
     * @param buffer_size Size of the buffer (should be at least 16 bytes)
     * @param show_seconds If true, format as HH:MM:SS, else HH:MM (more efficient)
     * @return true if time was retrieved successfully, false otherwise
     */
    bool getRTCTimeString(char* buffer, size_t buffer_size, bool show_seconds = true);

    /**
     * @brief Adjust RTC time by offset minutes (e.g., timezone change)
     * @param offset_minutes Minutes to add (negative to subtract)
     * @return true if RTC updated successfully
     */
    bool adjustRTCByOffsetMinutes(int offset_minutes);

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

    /**
     * @brief Get battery level percentage (0-100)
     * @return Battery percentage, or -1 if gauge is not ready
     */
    int getBatteryLevel() override;

    /**
     * @brief Check if battery is currently charging
     * @return true if charging, false if not charging or PMU is not ready
     */
    bool isCharging() override;

    /**
     * @brief Read ADC value with optimal accuracy and minimal resource usage
     * @param pin GPIO pin number (must be ADC-capable)
     * @param samples Number of samples to average (default: 8, range: 1-64)
     *                 More samples = more accurate but slower. 8 is usually optimal.
     * @return ADC value (0-4095 for 12-bit), or -1 if pin is invalid
     *
     * This function uses multiple sampling and averaging to reduce noise,
     * while keeping resource usage minimal by using a small sample count.
     * For best accuracy with minimal overhead, use 8 samples (default).
     */
    int readADC(uint8_t pin, uint8_t samples = 8);

    /**
     * @brief Read ADC voltage in millivolts
     * @param pin GPIO pin number (must be ADC-capable)
     * @param samples Number of samples to average (default: 8)
     * @param attenuation ADC attenuation (0-3, default: 3 for 11dB = 0-3.3V range)
     *                    0 = 0dB (0-1.1V), 1 = 2.5dB (0-1.5V), 2 = 6dB (0-2.2V), 3 = 11dB (0-3.3V)
     * @return Voltage in millivolts, or -1 if pin is invalid
     *
     * This function reads ADC and converts to voltage.
     * Default attenuation (3 = 11dB) allows 0-3.3V range.
     */
    int readADCVoltage(uint8_t pin, uint8_t samples = 8, uint8_t attenuation = 3);

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
    RfalNfcClass* nfc;
#endif

#ifdef USING_AUDIO_CODEC
    EspCodec codec;
#endif

#if defined(ARDUINO_LILYGO_LORA_SX1262)
    SX1262 radio_ = newModule();
#elif defined(ARDUINO_LILYGO_LORA_SX1280)
    SX1280 radio_ = newModule();
#elif defined(ARDUINO_LILYGO_LORA_CC1101)
    CC1101 radio_ = newModule();
#elif defined(ARDUINO_LILYGO_LORA_LR1121)
    LR1121 radio_ = newModule();
#elif defined(ARDUINO_LILYGO_LORA_SI4432)
    Si4432 radio_ = newModule();
#endif

  private:
    // Singleton pattern - prevent copy and assignment
    TLoRaPagerBoard();
    ~TLoRaPagerBoard();
    TLoRaPagerBoard(const TLoRaPagerBoard&) = delete;
    TLoRaPagerBoard& operator=(const TLoRaPagerBoard&) = delete;

    /**
     * @brief Initialize shared SPI bus CS pins
     */
    void initShareSPIPins();

    /**
     * @brief Rotary encoder task (FreeRTOS task)
     * @param p Task parameter (unused)
     */
    static void rotaryTask(void* p);

  private:
    // Two-stage power-off implementation
    bool isUsbPresent_bestEffort();

    uint32_t devices_probe = 0;   ///< Hardware detection status bitmask
    uint8_t _haptic_effects = 15; ///< Default haptic effect (strong buzz for message notification)
};

extern TLoRaPagerBoard& instance;
extern BoardBase& board;

#define DEVICE_MAX_BRIGHTNESS_LEVEL 16
#define DEVICE_MIN_BRIGHTNESS_LEVEL 0
