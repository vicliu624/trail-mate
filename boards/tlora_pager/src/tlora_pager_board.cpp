#if defined(ARDUINO_T_LORA_PAGER)
#include "boards/tlora_pager/tlora_pager_board.h"
#include "board/sd_utils.h"
#include "platform/radio/radio_power.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include <cmath>
#include <cstring>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>
#include <limits>
#include <new>

#include "display/drivers/ST7796.h"
#include "pins_arduino.h"
#include "platform/audio/audio_focus.h"
#include "platform/esp/arduino_common/power/battery_adc.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/boards/board_runtime.h"
#include "platform/esp/common/shared_spi_coordinator.h"
#include "platform/ui/audio/call_notification_tone.h"
#include "platform/ui/audio/pager_notification_tone.h"
#include "platform/ui/settings_store.h"
#include "sys/bus_access_scope.h"
#include "ui/runtime/ui_feedback.h"
#include <Preferences.h>

namespace boards::tlora_pager
{

#ifndef GPS_BOARD_LOG_ENABLE
#define GPS_BOARD_LOG_ENABLE 0
#endif

#if GPS_BOARD_LOG_ENABLE
#define GPS_BOARD_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define GPS_BOARD_LOG(...)
#endif

// ------------------------------
// I2C addresses from board configuration
// ------------------------------
static constexpr uint8_t I2C_XL9555 = 0x20;
static constexpr uint8_t I2C_BQ25896 = 0x6B;
static constexpr uint32_t kPagerDisplaySpiClockMhz = 80;
static constexpr uint32_t kRadioSpiLockTimeoutLogIntervalMs = 1000;
static constexpr uint32_t kSharedSpiBusResource = 1;
static constexpr uint32_t kSharedSpiBusOwnerId = 0x50414752U; // 'PAGR'
static constexpr const char* kSharedSpiBusOwner = "pager_board";

// ------------------------------
// I2C bus mutex (Gauge, PMU, RTC, XL9555 share Wire)
// ------------------------------
static SemaphoreHandle_t s_i2c_mutex = nullptr;

struct I2CGuard
{
    I2CGuard()
    {
        if (s_i2c_mutex != nullptr)
        {
            xSemaphoreTake(s_i2c_mutex, portMAX_DELAY);
        }
    }
    ~I2CGuard()
    {
        if (s_i2c_mutex != nullptr)
        {
            xSemaphoreGive(s_i2c_mutex);
        }
    }
};

// ------------------------------
// Gauge health tracking (BQ27220)
// ------------------------------
static int s_gauge_error_streak = 0;
static int s_gauge_reinit_count = 0;
static uint32_t s_last_radio_spi_lock_timeout_log_ms = 0;
static uint32_t s_suppressed_radio_spi_lock_timeout_logs = 0;
static portMUX_TYPE s_audio_owner_mux = portMUX_INITIALIZER_UNLOCKED;
static PagerAudioOwner s_audio_owner = PagerAudioOwner::None;
static platform::audio::FocusToken s_audio_focus;

const char* audioOwnerLabel(PagerAudioOwner owner)
{
    switch (owner)
    {
    case PagerAudioOwner::MessageTone:
        return "message_tone";
    case PagerAudioOwner::IncomingCallTone:
        return "incoming_call_tone";
    case PagerAudioOwner::ReticulumCall:
        return "reticulum_call";
    case PagerAudioOwner::VoiceMessage:
        return "voice_message";
    case PagerAudioOwner::Walkie:
        return "walkie";
    case PagerAudioOwner::Sstv:
        return "sstv";
    case PagerAudioOwner::Music:
        return "music";
    case PagerAudioOwner::None:
    default:
        return "none";
    }
}

PagerAudioOwner currentAudioOwner()
{
    portENTER_CRITICAL(&s_audio_owner_mux);
    const PagerAudioOwner owner = s_audio_owner;
    portEXIT_CRITICAL(&s_audio_owner_mux);
    return owner;
}

bool claimAudioOwner(PagerAudioOwner owner)
{
    if (owner == PagerAudioOwner::None)
    {
        return false;
    }

    platform::audio::FocusToken focus;
    if (owner == PagerAudioOwner::Music)
    {
        if (platform::audio::music_blocked()) return false;
    }
    else
    {
        using platform::audio::FocusOwner;
        const auto priority = owner == PagerAudioOwner::VoiceMessage ? FocusOwner::VoiceMessage : owner == PagerAudioOwner::Walkie                                                    ? FocusOwner::Walkie
                                                                                              : owner == PagerAudioOwner::ReticulumCall || owner == PagerAudioOwner::IncomingCallTone ? FocusOwner::Call
                                                                                              : owner == PagerAudioOwner::Sstv                                                        ? FocusOwner::Sstv
                                                                                                                                                                                      : FocusOwner::Notification;
        focus = platform::audio::request_focus(priority);
        if (!focus) return false;
    }
    portENTER_CRITICAL(&s_audio_owner_mux);
    const bool claimed = s_audio_owner == PagerAudioOwner::None;
    if (claimed)
    {
        s_audio_owner = owner;
        s_audio_focus = focus;
    }
    portEXIT_CRITICAL(&s_audio_owner_mux);
    if (!claimed) platform::audio::release_focus(focus);
    return claimed;
}

bool audioOwnerMatches(PagerAudioOwner owner)
{
    return owner != PagerAudioOwner::None && currentAudioOwner() == owner;
}

void releaseAudioOwner(PagerAudioOwner owner)
{
    platform::audio::FocusToken focus;
    portENTER_CRITICAL(&s_audio_owner_mux);
    if (s_audio_owner == owner)
    {
        s_audio_owner = PagerAudioOwner::None;
        focus = s_audio_focus;
        s_audio_focus = {};
    }
    portEXIT_CRITICAL(&s_audio_owner_mux);
    platform::audio::release_focus(focus);
}

// Temperature state (from BQ27220), used for safety / UI hints / brightness caps.
static float s_last_temp_c = NAN;
static bool s_temp_valid = false;
static bool s_temp_hot = false;
static bool s_temp_cold = false;
static uint32_t s_last_temp_notice_hot_ms = 0;
static uint32_t s_last_temp_notice_cold_ms = 0;

template <typename RadioT>
uint32_t radio_tx_timeout_ms(RadioT& radio, size_t len)
{
    const uint64_t air_us = static_cast<uint64_t>(radio.getTimeOnAir(len));
    uint64_t timeout_ms = 10ULL + ((air_us * 5ULL) + 999ULL) / 1000ULL;
    if (timeout_ms < 100ULL)
    {
        timeout_ms = 100ULL;
    }
    if (timeout_ms > 120000ULL)
    {
        timeout_ms = 120000ULL;
    }
    return static_cast<uint32_t>(timeout_ms);
}

void log_radio_spi_lock_timeout(const char* owner, TickType_t wait_ticks)
{
    const uint32_t now_ms = millis();
    ++s_suppressed_radio_spi_lock_timeout_logs;
    if (s_last_radio_spi_lock_timeout_log_ms != 0 &&
        now_ms - s_last_radio_spi_lock_timeout_log_ms <
            kRadioSpiLockTimeoutLogIntervalMs)
    {
        return;
    }
    Serial.printf("[SPI][RADIO] lock_timeout board=tlora_pager owner=%s wait_ticks=%lu suppressed=%lu\n",
                  owner ? owner : "-",
                  static_cast<unsigned long>(wait_ticks),
                  static_cast<unsigned long>(s_suppressed_radio_spi_lock_timeout_logs - 1));
    s_suppressed_radio_spi_lock_timeout_logs = 0;
    s_last_radio_spi_lock_timeout_log_ms = now_ms;
}

template <typename Fn>
bool withSharedSpiRadioAccess(const char* owner,
                              TickType_t wait_ticks,
                              Fn&& fn)
{
    const uint32_t wait_ms =
        static_cast<uint32_t>(wait_ticks) * portTICK_PERIOD_MS;
    sys::runtime::BusAcquireRequest request{};
    request.resource = kSharedSpiBusResource;
    request.policy = sys::runtime::BusAccessPolicy::InteractiveWorkerBounded;
    request.command_id = kSharedSpiBusOwnerId + 1;
    request.origin = kSharedSpiBusOwnerId;
    request.deadline_ms = sys::millis_now() + wait_ms;
    request.owner_label = owner ? owner : kSharedSpiBusOwner;
    sys::runtime::ScopedBusAccessToken bus_token(
        ::platform::esp::common::shared_spi_coordinator(),
        request);
    if (!bus_token.acquired())
    {
        if (wait_ticks != 0)
        {
            log_radio_spi_lock_timeout(owner, wait_ticks);
        }
        return false;
    }
    fn();
    return true;
}

#ifdef USING_XL9555_EXPANDS
#define BOSCH_BHI260_KLIO
#else
#define BOSCH_BHI260_GPIO
#endif
#include <BoschFirmware.h>

// Keyboard configuration
#ifdef USING_INPUT_DEV_KEYBOARD
// 4x10 character map
static constexpr char keymap[4][10] = {
    {'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'},
    {'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', '\n'},
    {'\0', 'z', 'x', 'c', 'v', 'b', 'n', 'm', '\0', '\0'},
    {' ', /*Space*/ '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0'}};
// 4x10 symbol map
static constexpr char symbol_map[4][10] = {
    {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'},
    {'*', '/', '+', '-', '=', ':', '\'', '"', '@', '\0'},
    {'\0', '_', '$', ';', '?', '!', ',', '.', '\0', '\0'},
    {' ' /*Space*/, '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0'}};

static const LilyGoKeyboardConfigure_t keyboardConfig = {
    .kb_rows = 4,
    .kb_cols = 10,
    .current_keymap = &keymap[0][0],
    .current_symbol_map = &symbol_map[0][0],
    .symbol_key_value = 0x1E,
    .alt_key_value = 0x14,
    .caps_key_value = 0x1C,
    .caps_b_key_value = 0xFF,
    .char_b_value = 0x19,
    .backspace_value = 0x1D,
    .has_symbol_key = false};
#endif

static QueueHandle_t rotaryMsg;
static TaskHandle_t rotaryHandler;
static TimerHandle_t hapticStopTimer;

static void hapticStopCallback(TimerHandle_t timer)
{
    auto* board = static_cast<TLoRaPagerBoard*>(pvTimerGetTimerID(timer));
    if (board)
    {
        board->stopVibrator();
    }
}

/**
 * @brief Read rotary encoder center button state with debouncing
 * @return true if button is pressed (LOW), false otherwise
 *
 * This function returns the debounced level, not a one-shot press edge. LVGL
 * needs to see a stable pressed state for the complete physical hold so an
 * action such as voice push-to-talk receives its matching release event.
 */
static bool getButtonState()
{
    static uint8_t buttonState = HIGH;
    static uint8_t lastButtonState = HIGH;
    static uint32_t lastDebounceTime = 0;
    const uint8_t debounceDelay = 20; // Debounce delay in milliseconds

    int reading = digitalRead(ROTARY_C);

    // Debouncing logic
    if (reading != lastButtonState)
    {
        // State changed, reset debounce timer
        lastDebounceTime = millis();
    }

    if (millis() - lastDebounceTime > debounceDelay)
    {
        // Debounce period elapsed, update state if changed
        if (reading != buttonState)
        {
            buttonState = reading;
        }
    }

    lastButtonState = reading;
    return buttonState == LOW;
}

TLoRaPagerBoard::TLoRaPagerBoard()
    : LilyGo_Display(SPI_DRIVER, false),
      LilyGoDispArduinoSPI(DISP_WIDTH, DISP_HEIGHT,
                           display::drivers::ST7796::getInitCommands(),
                           display::drivers::ST7796::getInitCommandsCount(),
                           // T-LoRa-Pager specific offsets:
                           // - Landscape orientations (90°, 270°): landscape_offset_x = 49
                           // - Portrait orientations (0°, 180°): portrait_offset_y = 49
                           display::drivers::ST7796::getRotationConfig(DISP_WIDTH, DISP_HEIGHT, 49, 49),
                           display::drivers::ST7796::getTransferConfig())
{
    devices_probe = 0;
}

TLoRaPagerBoard::~TLoRaPagerBoard()
{
}

TLoRaPagerBoard* TLoRaPagerBoard::getInstance()
{
    static TLoRaPagerBoard* instance = []() -> TLoRaPagerBoard*
    {
        void* storage = heap_caps_malloc_prefer(sizeof(TLoRaPagerBoard),
                                                2,
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (storage == nullptr)
        {
            storage = ::operator new(sizeof(TLoRaPagerBoard));
        }
        return new (storage) TLoRaPagerBoard();
    }();
    return instance;
}

void TLoRaPagerBoard::initShareSPIPins()
{
    const uint8_t shared_spi_chip_select_pins[] = {
        LORA_CS,
        SD_CS,
        DISP_CS,
    };
    for (auto pin : shared_spi_chip_select_pins)
    {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }

    // Reset is a control line, not a shared-SPI chip-select. Keep this list
    // separate so SD recovery can reason about actual bus participants.
    const uint8_t shared_spi_reset_pins[] = {
        LORA_RST,
    };
    for (auto pin : shared_spi_reset_pins)
    {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }
}

uint32_t TLoRaPagerBoard::beginDisplayHardware(uint32_t disable_hw_init)
{
    display_only_boot_ = true;
    const uint32_t result = begin(disable_hw_init);
    display_only_boot_ = false;
    return result;
}

uint32_t TLoRaPagerBoard::beginServices(uint32_t disable_hw_init)
{
    display_only_boot_ = false;
    return begin(disable_hw_init);
}

uint32_t TLoRaPagerBoard::begin(uint32_t disable_hw_init)
{
    Serial.printf("[TLoRaPagerBoard::begin] ===== HARDWARE INITIALIZATION START =====\n");
    Serial.printf("[TLoRaPagerBoard::begin] disable_hw_init=0x%08X\n", disable_hw_init);
    Serial.printf("[TLoRaPagerBoard::begin] NO_HW_GPS flag: %s\n", (disable_hw_init & NO_HW_GPS) ? "SET (GPS will be SKIPPED)" : "NOT SET (GPS will be initialized)");

    if (services_initialized_)
    {
        Serial.printf("[TLoRaPagerBoard::begin] Already initialized, returning devices_probe=0x%08X\n", devices_probe);
        return devices_probe;
    }
    if (display_hardware_initialized_ && display_only_boot_)
    {
        return devices_probe;
    }

    if (!display_hardware_initialized_)
    {
        bool res = false;

        devices_probe = 0x00;

        while (!psramFound())
        {
            log_d("ERROR:PSRAM NOT FOUND!");
            delay(1000);
        }

        devices_probe |= HW_PSRAM_ONLINE;

        Wire.begin(SDA, SCL);
        if (s_i2c_mutex == nullptr)
        {
            s_i2c_mutex = xSemaphoreCreateMutex();
        }

        // Initialize battery gauge (BQ27220)
        if (!gauge.begin(Wire, SDA, SCL))
        {
            log_w("Battery gauge (BQ27220) not found");
        }
        else
        {
            log_d("Battery gauge initialized successfully");
            devices_probe |= HW_GAUGE_ONLINE;
            Serial.printf("[TLoRaPagerBoard::begin] gauge capacity startup profile deferred\n");
        }

        // Initialize PMU (BQ25896 power management)
        Serial.printf("[TLoRaPagerBoard::begin] PMU init begin\n");
        res = initPMU();
        Serial.printf("[TLoRaPagerBoard::begin] PMU init end ok=%d\n", res ? 1 : 0);
        if (!res)
        {
            log_w("PMU (BQ25896) not found");
        }
        else
        {
            log_d("PMU initialized successfully");
            devices_probe |= HW_PMU_ONLINE;
        }
        Serial.printf("[TLoRaPagerBoard::begin] PMU settle begin ms=20\n");
        delay(20);
        Serial.printf("[TLoRaPagerBoard::begin] PMU settle end\n");

        // Initialize GPIO expander (XL9555) - controls power for various peripherals
#ifdef USING_XL9555_EXPANDS
        Serial.printf("[TLoRaPagerBoard::begin] expander init begin addr=0x20\n");
        if (io.begin(Wire, 0x20))
        {
            log_d("GPIO expander (XL9555) initialized successfully");
            Serial.printf("[TLoRaPagerBoard::begin] expander init end ok=1\n");
            devices_probe |= HW_EXPAND_ONLINE;

            // Configure GPIO expander pins as outputs and set them HIGH (enable peripherals)
            const uint8_t expand_pins[] = {
                EXPANDS_KB_RST,  // Keyboard reset
                EXPANDS_LORA_EN, // LoRa enable
                EXPANDS_GPS_EN,  // GPS enable
                EXPANDS_DRV_EN,  // Haptic driver enable
                EXPANDS_AMP_EN,  // Audio amplifier enable
#ifdef EXPANDS_GPS_RST
                EXPANDS_GPS_RST, // GPS reset
#endif
#ifdef EXPANDS_KB_EN
                EXPANDS_KB_EN, // Keyboard enable
#endif
#ifdef EXPANDS_GPIO_EN
                EXPANDS_GPIO_EN, // GPIO enable
#endif
#ifdef EXPANDS_SD_EN
                EXPANDS_SD_EN, // SD card enable
#endif
            };

            Serial.printf("[TLoRaPagerBoard::begin] expander power rails begin count=%u\n",
                          static_cast<unsigned>(sizeof(expand_pins) / sizeof(expand_pins[0])));
            for (auto pin : expand_pins)
            {
                io.pinMode(pin, OUTPUT);
                io.digitalWrite(pin, HIGH); // Enable peripheral power
                delay(1);                   // Small delay for power stabilization
            }
            Serial.printf("[TLoRaPagerBoard::begin] expander power rails settle begin ms=50\n");
            delay(50);
            Serial.printf("[TLoRaPagerBoard::begin] expander power rails settle end\n");

            io.pinMode(EXPANDS_UNUSED_SPI_AUX_EN, OUTPUT);
            io.digitalWrite(EXPANDS_UNUSED_SPI_AUX_EN, LOW);
            Serial.printf("[TLoRaPagerBoard::begin] unused spi aux rail off pin=%d\n", EXPANDS_UNUSED_SPI_AUX_EN);

            // SD card pull-up enable (input pin)
            Serial.printf("[TLoRaPagerBoard::begin] expander sd pull enable pin mode begin\n");
            io.pinMode(EXPANDS_SD_PULLEN, INPUT);
            Serial.printf("[TLoRaPagerBoard::begin] expander sd pull enable pin mode end\n");
        }
        else
        {
            log_w("GPIO expander (XL9555) initialization failed");
            Serial.printf("[TLoRaPagerBoard::begin] expander init end ok=0\n");
        }
#endif

        // Initialize backlight driver (AW9364)
        Serial.printf("[TLoRaPagerBoard::begin] backlight init begin\n");
        backlight.begin(DISP_BL);
        log_d("Backlight driver initialized (pin %d)", DISP_BL);
        Serial.printf("[TLoRaPagerBoard::begin] backlight init end\n");

        // Initialize shared SPI pins (CS pins for LoRa and SD)
        Serial.printf("[TLoRaPagerBoard::begin] shared spi pins init begin\n");
        initShareSPIPins();
        Serial.printf("[TLoRaPagerBoard::begin] shared spi pins init end\n");

        // Initialize display (ST7796)
        Serial.printf("[TLoRaPagerBoard::begin] display bus init begin\n");
        const bool display_init_ok =
            LilyGoDispArduinoSPI::init(DISP_SCK,
                                       DISP_MISO,
                                       DISP_MOSI,
                                       DISP_CS,
                                       DISP_RST,
                                       DISP_DC,
                                       -1,
                                       kPagerDisplaySpiClockMhz,
                                       SPI);
        Serial.printf("[TLoRaPagerBoard::begin] display bus init result ok=%d\n",
                      display_init_ok ? 1 : 0);
        if (!display_init_ok)
        {
            Serial.printf("[TLoRaPagerBoard::begin] display hardware not ready; "
                          "services will not claim display readiness\n");
            return devices_probe;
        }
        log_d("Display (ST7796) initialized: logical=%dx%d raw=%dx%d",
              LilyGoDispArduinoSPI::_width, LilyGoDispArduinoSPI::_height, DISP_WIDTH, DISP_HEIGHT);
        Serial.printf("[TLoRaPagerBoard::begin] display bus init end logical=%dx%d raw=%dx%d spi=%luMHz\n",
                      LilyGoDispArduinoSPI::_width,
                      LilyGoDispArduinoSPI::_height,
                      DISP_WIDTH,
                      DISP_HEIGHT,
                      static_cast<unsigned long>(kPagerDisplaySpiClockMhz));

        // DisplayInterface::init() is the sole SPI controller initializer for
        // this board. Do not call SPI.begin() again after the panel has been
        // initialized: reinitializing the shared peripheral here can reset
        // controller state after the ST7796 command sequence has completed.
        Serial.printf("[TLoRaPagerBoard::begin] shared spi bus ready owner=display "
                      "sck=%d miso=%d mosi=%d\n",
                      LORA_SCK,
                      LORA_MISO,
                      LORA_MOSI);

        display_hardware_initialized_ = true;
        Serial.printf("[TLoRaPagerBoard::begin] ===== DISPLAY HARDWARE READY =====\n");
        if (display_only_boot_)
        {
            // LVGL creates its input devices immediately after the display
            // hardware phase. The keyboard must therefore be online before
            // returning from this phase, otherwise beginLvglHelper() sees
            // hasKeyboard() == false and never creates keypad_read().
            if (!(disable_hw_init & NO_HW_KEYBOARD))
            {
                Serial.printf("[TLoRaPagerBoard::begin] keyboard init for display phase begin\n");
                const bool keyboard_ready = initKeyboard();
                Serial.printf("[TLoRaPagerBoard::begin] keyboard init for display phase end ok=%d\n",
                              keyboard_ready ? 1 : 0);
            }
            return devices_probe;
        }
    }

    // Initialize sensor (BHI260AP) - optional, can be disabled. This is
    // deliberately deferred until after the display boot frame is visible.
    if (!(disable_hw_init & NO_HW_SENSOR))
    {
        Serial.printf("[TLoRaPagerBoard::begin] sensor init begin\n");
        if (initSensor())
        {
            log_d("Sensor (BHI260AP) initialized successfully");
            Serial.printf("[TLoRaPagerBoard::begin] sensor init end ok=1\n");
        }
        else
        {
            Serial.printf("[TLoRaPagerBoard::begin] sensor init end ok=0\n");
        }
    }
    else
    {
        Serial.printf("[TLoRaPagerBoard::begin] sensor init skipped\n");
    }

    // Initialize RTC (PCF85063) - optional
    if (!(disable_hw_init & NO_HW_RTC))
    {
        Serial.printf("[TLoRaPagerBoard::begin] rtc init begin\n");
        if (initRTC())
        {
            log_d("RTC (PCF85063) initialized successfully");
            Serial.printf("[TLoRaPagerBoard::begin] rtc init end ok=1\n");
        }
        else
        {
            Serial.printf("[TLoRaPagerBoard::begin] rtc init end ok=0\n");
        }
    }
    else
    {
        Serial.printf("[TLoRaPagerBoard::begin] rtc init skipped\n");
    }

    // Initialize keyboard (TCA8418) - optional
    if (!(disable_hw_init & NO_HW_KEYBOARD))
    {
        Serial.printf("[TLoRaPagerBoard::begin] keyboard init begin\n");
        if (initKeyboard())
        {
            log_d("Keyboard (TCA8418) initialized successfully");
            Serial.printf("[TLoRaPagerBoard::begin] keyboard init end ok=1\n");
        }
        else
        {
            Serial.printf("[TLoRaPagerBoard::begin] keyboard init end ok=0\n");
        }
    }
    else
    {
        Serial.printf("[TLoRaPagerBoard::begin] keyboard init skipped\n");
    }

    // Initialize haptic driver (DRV2605) - optional
    if (!(disable_hw_init & NO_HW_DRV))
    {
        Serial.printf("[TLoRaPagerBoard::begin] haptic init begin\n");
        if (initDrv())
        {
            log_d("Haptic driver (DRV2605) initialized successfully");
            Serial.printf("[TLoRaPagerBoard::begin] haptic init end ok=1\n");
        }
        else
        {
            Serial.printf("[TLoRaPagerBoard::begin] haptic init end ok=0\n");
        }
    }
    else
    {
        Serial.printf("[TLoRaPagerBoard::begin] haptic init skipped\n");
    }

    // GPS service is initialized by AppContext after configuration is loaded

    // Initialize LoRa radio_ - optional
    if (!(disable_hw_init & NO_HW_LORA))
    {
        Serial.printf("[TLoRaPagerBoard::begin] lora init begin\n");
        if (initLoRa())
        {
            log_d("LoRa radio_ initialized successfully");
            Serial.printf("[TLoRaPagerBoard::begin] lora init end ok=1\n");
        }
        else
        {
            Serial.printf("[TLoRaPagerBoard::begin] lora init end ok=0\n");
        }
    }
    else
    {
        Serial.printf("[TLoRaPagerBoard::begin] lora init skipped\n");
    }

    // Initialize SD card - optional, with retry
    if (!(disable_hw_init & NO_HW_SD))
    {
        Serial.printf("[TLoRaPagerBoard::begin] sd init begin\n");
        const int max_retries = 2;
        for (int retry = 0; retry < max_retries; retry++)
        {
            if (installSD())
            {
                log_d("SD card initialized successfully");
                devices_probe |= HW_SD_ONLINE;
                Serial.printf("[TLoRaPagerBoard::begin] sd init end ok=1 retry=%d\n", retry);
                break;
            }
            else if (retry < max_retries - 1)
            {
                log_w("SD card initialization failed, retrying... (%d/%d)", retry + 1, max_retries);
                delay(100); // Small delay before retry
            }
            else
            {
                log_w("SD card not found after %d attempts", max_retries);
                Serial.printf("[TLoRaPagerBoard::begin] sd init end ok=0 retries=%d\n", max_retries);
            }
        }
    }
    else
    {
        Serial.printf("[TLoRaPagerBoard::begin] sd init skipped\n");
    }

    // Initialize audio codec (ES8311) - optional
#ifdef USING_AUDIO_CODEC
    if (!(disable_hw_init & NO_HW_CODEC))
    {
        Serial.printf("[TLoRaPagerBoard::begin] audio codec init begin\n");
        codec.setPins(I2S_MCLK, I2S_SCK, I2S_WS, I2S_SDOUT, I2S_SDIN);
        if (codec.begin(Wire, 0x18, CODEC_TYPE_ES8311))
        {
            devices_probe |= HW_CODEC_ONLINE;
            powerControl(POWER_SPEAK, false);
            codec.setPaPinCallback(
                [](bool enable, void* user_data)
                {
                    static_cast<ExtensionIOXL9555*>(user_data)->digitalWrite(
                        EXPANDS_AMP_EN, enable);
                },
                &io);
            log_d("Audio codec (ES8311) initialized successfully");
            Serial.printf("[TLoRaPagerBoard::begin] audio codec init end ok=1\n");
        }
        else
        {
            log_w("Audio codec (ES8311) not found");
            Serial.printf("[TLoRaPagerBoard::begin] audio codec init end ok=0\n");
        }
    }
    else
    {
        Serial.printf("[TLoRaPagerBoard::begin] audio codec init skipped\n");
    }
#endif

    // Create rotary encoder message queue and task
    Serial.printf("[TLoRaPagerBoard::begin] rotary runtime init begin\n");
    rotaryMsg = xQueueCreate(5, sizeof(RotaryMsg_t));
    if (rotaryMsg == nullptr)
    {
        log_e("Failed to create rotary encoder message queue");
        Serial.printf("[TLoRaPagerBoard::begin] rotary queue create ok=0\n");
    }
    else
    {
        Serial.printf("[TLoRaPagerBoard::begin] rotary queue create ok=1\n");
    }

    BaseType_t task_result = xTaskCreate(rotaryTask, "rotary", 2 * 1024, NULL, 10, &rotaryHandler);
    if (task_result != pdPASS)
    {
        log_e("Failed to create rotary encoder task");
        Serial.printf("[TLoRaPagerBoard::begin] rotary task create ok=0 result=%ld\n", static_cast<long>(task_result));
    }
    else
    {
        log_d("Rotary encoder task created successfully");
        Serial.printf("[TLoRaPagerBoard::begin] rotary task create ok=1\n");
    }
    Serial.printf("[TLoRaPagerBoard::begin] rotary runtime init end\n");

    // Initialize power button handling
    Serial.printf("[TLoRaPagerBoard::begin] power button init begin\n");
    if (!initPowerButton())
    {
        log_w("Power button initialization failed");
        Serial.printf("[TLoRaPagerBoard::begin] power button init end ok=0\n");
    }
    else
    {
        log_d("Power button initialized successfully");
        Serial.printf("[TLoRaPagerBoard::begin] power button init end ok=1\n");
    }

    log_d("Board initialization complete. Hardware online: 0x%08X", devices_probe);
    Serial.printf("[TLoRaPagerBoard::begin] ===== HARDWARE INITIALIZATION COMPLETE =====\n");
    Serial.printf("[TLoRaPagerBoard::begin] devices_probe=0x%08X\n", devices_probe);
    const char* gps_state =
        (devices_probe & HW_GPS_ONLINE) ? "YES" : ((disable_hw_init & NO_HW_GPS) ? "SKIPPED" : "DEFERRED");
    Serial.printf("[TLoRaPagerBoard::begin] GPS online: %s\n", gps_state);
    services_initialized_ = true;
    return devices_probe;
}

void TLoRaPagerBoard::loop()
{
}

bool TLoRaPagerBoard::initPMU()
{
    bool res = pmu.init(Wire, SDA, SCL);
    if (!res)
    {
        return false;
    }
    // Reset PMU
    pmu.resetDefault();

    // Set the charging target voltage full voltage to 4288mV
    pmu.setChargeTargetVoltage(4288);

    // The charging current should not be greater than half of the battery capacity.
    pmu.setChargerConstantCurr(704);

    // Configure system power-down (SYS VOFF) voltage to protect battery from over-discharge.
    // 3200mV is a conservative and commonly used under-voltage shutdown threshold.
    pmu.setSysPowerDownVoltage(3200);

    // Enable measure
    pmu.enableMeasure();

    return res;
}

bool TLoRaPagerBoard::initSensor()
{
    bool res = false;
    Wire.setClock(1000000UL);
    log_d("Init BHI260AP Sensor");
    sensor.setPins(-1);
    sensor.setFirmware(bosch_firmware_image, bosch_firmware_size, bosch_firmware_type);
    sensor.setBootFromFlash(false);
    res = sensor.begin(Wire);
    if (!res)
    {
        log_e("Failed to find BHI260AP");
    }
    else
    {
        log_d("Initializing BHI260AP succeeded");
        devices_probe |= HW_BHI260AP_ONLINE;
        sensor.setRemapAxes(SensorBHI260AP::BOTTOM_LAYER_TOP_LEFT_CORNER);
        pinMode(SENSOR_INT, INPUT);
    }
    Wire.setClock(400000UL);
    return res;
}

bool TLoRaPagerBoard::initRTC()
{
    bool res = false;
    log_d("Init PCF85063 RTC");
    res = rtc.begin(Wire);
    if (!res)
    {
        log_e("Failed to find PCF85063");
    }
    else
    {
        devices_probe |= HW_RTC_ONLINE;
        log_d("Initializing PCF85063 succeeded");
        rtc.hwClockRead(); // Synchronize RTC clock to system clock
        rtc.setClockOutput(SensorPCF85063::CLK_LOW);

        pinMode(RTC_INT, INPUT_PULLUP);
        // Note: Interrupt handling would require event group setup
    }
    return res;
}

bool TLoRaPagerBoard::initDrv()
{
    bool res = false;
    // DRV2605 Address: 0x5A
    log_d("Init DRV2605 Haptic Driver");
    powerControl(POWER_HAPTIC_DRIVER, true);
    delay(5);
    res = drv.begin(Wire);
    if (!res)
    {
        log_e("Failed to find DRV2605");
        powerControl(POWER_HAPTIC_DRIVER, false);
    }
    else
    {
        log_d("Initializing DRV2605 succeeded");
        drv.selectLibrary(1);
        drv.setMode(SensorDRV2605::MODE_INTTRIG);
        drv.useERM();
        // Do not vibrate during power-on; vibration is triggered later via vibrator() when needed.
        drv.setWaveform(0, 0);
        drv.setWaveform(1, 0);
        powerControl(POWER_HAPTIC_DRIVER, false);
        devices_probe |= HW_DRV_ONLINE;
    }
    return res;
}

bool TLoRaPagerBoard::initKeyboard()
{
#ifdef USING_INPUT_DEV_KEYBOARD
    if (devices_probe & HW_KEYBOARD_ONLINE)
    {
        return true;
    }

    if (devices_probe & HW_EXPAND_ONLINE)
    {
        powerControl(POWER_KEYBOARD, true);
        delay(5);
#ifdef EXPANDS_KB_RST
        // Reset is held high in the steady state; pulse low to force a clean
        // controller restart before probing the TCA8418 over I2C.
        io.digitalWrite(EXPANDS_KB_RST, LOW);
        delay(5);
        io.digitalWrite(EXPANDS_KB_RST, HIGH);
        delay(20);
#endif
    }

    // Configure keyboard backlight pin
    kb.setPins(KB_BACKLIGHT);

    // Initialize keyboard (TCA8418 I2C keyboard controller)
    bool res = kb.begin(keyboardConfig, Wire, KB_INT);
    if (!res)
    {
        log_w("Keyboard (TCA8418) not found");
        return false;
    }

    log_d("Keyboard (TCA8418) initialized successfully");
    devices_probe |= HW_KEYBOARD_ONLINE;
    return true;
#else
    return false;
#endif
}

bool TLoRaPagerBoard::initLoRa()
{
    int state = RADIOLIB_ERR_NONE;
    const bool bus_acquired = withSharedSpiRadioAccess(
        "radio_init",
        pdMS_TO_TICKS(200),
        [&]()
        {
            radio_.reset();
            state = radio_.begin();
        });

    if (!bus_acquired)
    {
        devices_probe &= ~HW_RADIO_ONLINE;
        return false;
    }

    if (state != RADIOLIB_ERR_NONE)
    {
        devices_probe &= ~HW_RADIO_ONLINE;
        log_e("❌Radio init failed, code :%d", state);
        return false;
    }

    devices_probe |= HW_RADIO_ONLINE;
#if defined(ARDUINO_LILYGO_LORA_LR1121)
    static const uint32_t rfswitch_dio_pins[] = {
        RADIOLIB_LR11X0_DIO5,
        RADIOLIB_LR11X0_DIO6,
        RADIOLIB_NC,
        RADIOLIB_NC,
        RADIOLIB_NC,
    };
    static const Module::RfSwitchMode_t rfswitch_table[] = {
        {LR11x0::MODE_STBY, {LOW, LOW}},
        {LR11x0::MODE_RX, {LOW, HIGH}},
        {LR11x0::MODE_TX, {HIGH, LOW}},
        {LR11x0::MODE_TX_HP, {HIGH, LOW}},
        {LR11x0::MODE_TX_HF, {LOW, LOW}},
        {LR11x0::MODE_GNSS, {LOW, LOW}},
        {LR11x0::MODE_WIFI, {LOW, LOW}},
        END_OF_MODE_TABLE,
    };

    radio_.setRfSwitchTable(rfswitch_dio_pins, rfswitch_table);
    const bool tcxo_bus_acquired = withSharedSpiRadioAccess(
        "radio_tcxo",
        pdMS_TO_TICKS(200),
        [&]()
        {
            state = radio_.setTCXO(3.0f);
        });
    if (!tcxo_bus_acquired)
    {
        log_w("LR1121 TCXO configuration skipped: shared SPI unavailable");
    }
    else if (state != RADIOLIB_ERR_NONE)
    {
        log_w("LR1121 TCXO config returned code: %d", state);
    }
#endif
    log_i("✅Radio init succeeded");
    return true;
}

bool TLoRaPagerBoard::installSD()
{
    if (!::platform::esp::boards::storageStartupGateSatisfied())
    {
        log_d("SD mount deferred: first shared-SPI display transaction incomplete");
        return false;
    }

    // The power-cycle runs before installSpiSd() acquires RecoveryExclusive.
    // It must not drive a live display CS. The recovery peer set below is used
    // only by installSpiSd(), after that coordinator token has been acquired.
    static const int power_cycle_peer_cs_pins[] = {
        LORA_CS,
    };
    static const int recovery_peer_cs_pins[] = {
        LORA_CS,
        DISP_CS,
    };

    // Check SD card detection pin (if available)
#ifdef EXPANDS_SD_DET
    if (devices_probe & HW_EXPAND_ONLINE)
    {
        io.pinMode(EXPANDS_SD_DET, INPUT);
        if (io.digitalRead(EXPANDS_SD_DET))
        {
            log_d("SD card detection pin indicates no card present");
            return false;
        }
    }
#endif

    // Ensure SPI pins are initialized
    initShareSPIPins();

#ifdef EXPANDS_SD_EN
    if (devices_probe & HW_EXPAND_ONLINE)
    {
        sdutil::releaseSdBusDevices(
            SD_CS,
            power_cycle_peer_cs_pins,
            sizeof(power_cycle_peer_cs_pins) / sizeof(power_cycle_peer_cs_pins[0]));
        Serial.println("[SD] power cycle begin");
        powerControl(POWER_SD_CARD, false);
        delay(120);
        powerControl(POWER_SD_CARD, true);
        delay(250);
        sdutil::releaseSdBusDevices(
            SD_CS,
            power_cycle_peer_cs_pins,
            sizeof(power_cycle_peer_cs_pins) / sizeof(power_cycle_peer_cs_pins[0]));
        Serial.println("[SD] power cycle end");
    }
#endif

    uint8_t card_type = sdutil::kCardNone;
    uint32_t card_size_mb = 0;
    bool ok = sdutil::installSpiSd(SD_CS, SD_SPI_FREQUENCY, "/sd",
                                   recovery_peer_cs_pins,
                                   sizeof(recovery_peer_cs_pins) / sizeof(recovery_peer_cs_pins[0]),
                                   &card_type, &card_size_mb);
    if (!ok)
    {
        log_w("SD card initialization failed");
        return false;
    }
    log_d("SD card detected, type=%u size=%lu MB",
          (unsigned)card_type, (unsigned long)card_size_mb);
    return true;
}

bool TLoRaPagerBoard::ensureSDReady()
{
    if (isCardReady())
    {
        devices_probe |= HW_SD_ONLINE;
        return true;
    }

    const bool ok = installSD();
    if (ok)
    {
        devices_probe |= HW_SD_ONLINE;
    }
    else
    {
        devices_probe &= ~HW_SD_ONLINE;
    }
    return ok;
}

void TLoRaPagerBoard::uninstallSD()
{
    ::platform::esp::arduino_common::storage::unmount_sd_card();
    log_d("SD card unmounted");
}

bool TLoRaPagerBoard::isCardReady()
{
    return ::platform::esp::arduino_common::storage::sd_card_ready();
}

void TLoRaPagerBoard::powerControl(PowerCtrlChannel_t ch, bool enable)
{
    I2CGuard i2c; // XL9555 io uses I2C
    switch (ch)
    {
    case POWER_DISPLAY_BACKLIGHT:
        break;
    case POWER_RADIO:
#ifdef USING_XL9555_EXPANDS
        io.digitalWrite(EXPANDS_LORA_EN, enable);
#endif
        break;
    case POWER_HAPTIC_DRIVER:
#ifdef USING_XL9555_EXPANDS
        io.digitalWrite(EXPANDS_DRV_EN, enable);
#endif
        break;
    case POWER_GPS:
#ifdef USING_XL9555_EXPANDS
        io.digitalWrite(EXPANDS_GPS_EN, enable);
#endif
        break;
    case POWER_SD_CARD:
#ifdef USING_XL9555_EXPANDS
        io.digitalWrite(EXPANDS_SD_EN, enable);
#endif
        break;
    case POWER_SPEAK:
#ifdef USING_XL9555_EXPANDS
        io.digitalWrite(EXPANDS_AMP_EN, enable);
#endif
        break;
    case POWER_KEYBOARD:
#ifdef EXPANDS_KB_EN
        io.digitalWrite(EXPANDS_KB_EN, enable);
#endif
        break;
    default:
        break;
    }
}

void TLoRaPagerBoard::vibrator()
{
    log_d("[vibrator] Called, devices_probe=0x%08X, HW_DRV_ONLINE=%s, _haptic_effects=%d",
          devices_probe, (devices_probe & HW_DRV_ONLINE) ? "YES" : "NO", _haptic_effects);

    // Lazy re-init if needed
    if (!(devices_probe & HW_DRV_ONLINE))
    {
        log_d("[vibrator] Device not online, attempting re-initialization...");
        powerControl(POWER_HAPTIC_DRIVER, true);
        delay(5);
        log_d("[vibrator] Power enabled, calling drv.begin(Wire)...");
        if (drv.begin(Wire))
        {
            log_d("[vibrator] drv.begin() succeeded, configuring driver...");
            drv.selectLibrary(1);
            drv.setMode(SensorDRV2605::MODE_INTTRIG);
            drv.useERM();
            devices_probe |= HW_DRV_ONLINE;
            log_d("[vibrator] Driver re-initialized successfully, devices_probe=0x%08X", devices_probe);
        }
        else
        {
            powerControl(POWER_HAPTIC_DRIVER, false);
            log_e("[vibrator] Haptic driver re-initialization FAILED, skip vibrate");
            return;
        }
    }
    else
    {
        log_d("[vibrator] Device already online, skipping re-initialization");
    }

    log_d("[vibrator] Enabling power and starting vibration (effect=%d)...", _haptic_effects);
    powerControl(POWER_HAPTIC_DRIVER, true);
    drv.setWaveform(0, _haptic_effects);
    drv.setWaveform(1, _haptic_effects);
    drv.setWaveform(2, 0);
    drv.run();
    log_d("[vibrator] Vibration started, setting up stop timer...");

    if (hapticStopTimer == nullptr)
    {
        log_d("[vibrator] Creating haptic stop timer...");
        hapticStopTimer = xTimerCreate("haptic_stop",
                                       pdMS_TO_TICKS(2000),
                                       pdFALSE,
                                       this,
                                       hapticStopCallback);
        if (hapticStopTimer == nullptr)
        {
            log_e("[vibrator] FAILED to create haptic stop timer!");
        }
        else
        {
            log_d("[vibrator] Haptic stop timer created successfully");
        }
    }
    else
    {
        log_d("[vibrator] Haptic stop timer already exists, reusing it");
    }

    if (hapticStopTimer != nullptr)
    {
        xTimerStop(hapticStopTimer, 0);
        xTimerChangePeriod(hapticStopTimer, pdMS_TO_TICKS(2000), 0);
        BaseType_t timer_result = xTimerStart(hapticStopTimer, 0);
        if (timer_result == pdPASS)
        {
            log_d("[vibrator] Haptic stop timer started successfully (2s delay)");
        }
        else
        {
            log_e("[vibrator] FAILED to start haptic stop timer! result=%d", timer_result);
        }
    }
    else
    {
        log_e("[vibrator] Cannot start timer - timer is nullptr!");
    }

    log_d("[vibrator] Function completed");
}

void TLoRaPagerBoard::stopVibrator()
{
    log_d("[stopVibrator] Called, devices_probe=0x%08X, HW_DRV_ONLINE=%s",
          devices_probe, (devices_probe & HW_DRV_ONLINE) ? "YES" : "NO");

    if (devices_probe & HW_DRV_ONLINE)
    {
        log_d("[stopVibrator] Stopping driver...");
        drv.stop();
    }
    else
    {
        log_w("[stopVibrator] Device not online, skipping drv.stop()");
    }

    log_d("[stopVibrator] Disabling power...");
    powerControl(POWER_HAPTIC_DRIVER, false);
    log_d("[stopVibrator] Power disabled, function completed");
}

int TLoRaPagerBoard::openAudioSession(PagerAudioOwner owner, uint8_t bits_per_sample,
                                      uint8_t channels, uint32_t sample_rate,
                                      bool speaker_enabled)
{
#ifndef USING_AUDIO_CODEC
    (void)owner;
    (void)bits_per_sample;
    (void)channels;
    (void)sample_rate;
    (void)speaker_enabled;
    return -1;
#else
    if (!(devices_probe & HW_CODEC_ONLINE) || owner == PagerAudioOwner::None ||
        channels == 0)
    {
        return -1;
    }
    if (!claimAudioOwner(owner))
    {
        Serial.printf("[Audio][Pager] busy requested=%s owner=%s\n",
                      audioOwnerLabel(owner),
                      audioOwnerLabel(currentAudioOwner()));
        return -2;
    }

    powerControl(POWER_SPEAK, false);
    int state = -1;
    {
        I2CGuard i2c;
        state = codec.open(bits_per_sample, channels, sample_rate);
        if (state == 0)
        {
            codec.setMute(true);
            codec.setOutMute(true);
        }
    }
    if (state != 0)
    {
        releaseAudioOwner(owner);
        Serial.printf("[Audio][Pager] open failed owner=%s rc=%d rate=%lu logical_ch=%u hw_ch=%u\n",
                      audioOwnerLabel(owner),
                      state,
                      static_cast<unsigned long>(sample_rate),
                      static_cast<unsigned>(channels),
                      static_cast<unsigned>(channels));
        return state;
    }

    if (!speaker_enabled)
    {
        powerControl(POWER_SPEAK, false);
    }
    else
    {
        delay(10);
    }
    Serial.printf("[Audio][Pager] open owner=%s rate=%lu logical_ch=%u hw_ch=%u speaker=%u\n",
                  audioOwnerLabel(owner),
                  static_cast<unsigned long>(sample_rate),
                  static_cast<unsigned>(channels),
                  static_cast<unsigned>(channels),
                  speaker_enabled ? 1U : 0U);
    return 0;
#endif
}

void TLoRaPagerBoard::closeAudioSession(PagerAudioOwner owner)
{
#ifdef USING_AUDIO_CODEC
    if (!audioOwnerMatches(owner))
    {
        return;
    }

    {
        I2CGuard i2c;
        codec.setMute(true);
        codec.setOutMute(true);
    }
    powerControl(POWER_SPEAK, false);
    {
        I2CGuard i2c;
        codec.close();
    }
    releaseAudioOwner(owner);
    Serial.printf("[Audio][Pager] close owner=%s\n", audioOwnerLabel(owner));
#else
    (void)owner;
#endif
}

int TLoRaPagerBoard::audioRead(PagerAudioOwner owner, uint8_t* buffer, size_t size)
{
#ifdef USING_AUDIO_CODEC
    if (!buffer || size == 0 || !audioOwnerMatches(owner))
    {
        return -1;
    }
    return codec.read(buffer, size);
#else
    (void)owner;
    (void)buffer;
    (void)size;
    return -1;
#endif
}

int TLoRaPagerBoard::audioWrite(PagerAudioOwner owner, const uint8_t* buffer, size_t size)
{
#ifdef USING_AUDIO_CODEC
    if (!buffer || size == 0 || !audioOwnerMatches(owner))
    {
        return -1;
    }
    return codec.write(const_cast<uint8_t*>(buffer), size);
#else
    (void)owner;
    (void)buffer;
    (void)size;
    return -1;
#endif
}

bool TLoRaPagerBoard::audioSetVolume(PagerAudioOwner owner, uint8_t level)
{
#ifdef USING_AUDIO_CODEC
    if (!audioOwnerMatches(owner))
    {
        return false;
    }
    I2CGuard i2c;
    codec.setVolume(level);
    return true;
#else
    (void)owner;
    (void)level;
    return false;
#endif
}

bool TLoRaPagerBoard::audioSetGain(PagerAudioOwner owner, float db_value)
{
#ifdef USING_AUDIO_CODEC
    if (!audioOwnerMatches(owner))
    {
        return false;
    }
    I2CGuard i2c;
    codec.setGain(db_value);
    return true;
#else
    (void)owner;
    (void)db_value;
    return false;
#endif
}

bool TLoRaPagerBoard::audioSetMute(PagerAudioOwner owner, bool enabled)
{
#ifdef USING_AUDIO_CODEC
    if (!audioOwnerMatches(owner))
    {
        return false;
    }
    I2CGuard i2c;
    codec.setMute(enabled);
    return true;
#else
    (void)owner;
    (void)enabled;
    return false;
#endif
}

bool TLoRaPagerBoard::audioSetOutMute(PagerAudioOwner owner, bool enabled)
{
#ifdef USING_AUDIO_CODEC
    if (!audioOwnerMatches(owner))
    {
        return false;
    }
    I2CGuard i2c;
    codec.setOutMute(enabled);
    return true;
#else
    (void)owner;
    (void)enabled;
    return false;
#endif
}

void TLoRaPagerBoard::playMessageTone()
{
#ifndef USING_AUDIO_CODEC
    return;
#else
    if (_message_tone_volume == 0)
    {
        return;
    }
    if (!(devices_probe & HW_CODEC_ONLINE))
    {
        return;
    }

    static bool s_playing = false;
    static uint32_t s_last_play_ms = 0;

    if (s_playing)
    {
        return;
    }

    const uint32_t now = millis();
    if ((now - s_last_play_ms) < 240)
    {
        return;
    }

    s_playing = true;
    s_last_play_ms = now;

    static constexpr size_t kFramesPerChunk = 128;
    namespace pager_tone = ::platform::ui::audio::pager_notification;

    constexpr PagerAudioOwner kOwner = PagerAudioOwner::MessageTone;
    const int open_result = openAudioSession(
        kOwner, 16, 1, pager_tone::kPlaybackSampleRateHz, true);
    if (open_result != 0)
    {
        Serial.printf("[Audio][PagerTone] open failed kind=message rc=%d rate=%lu channels=1\n",
                      open_result,
                      static_cast<unsigned long>(pager_tone::kPlaybackSampleRateHz));
        s_playing = false;
        return;
    }

    if (!audioSetMute(kOwner, true) ||
        !audioSetVolume(kOwner, _message_tone_volume) ||
        !audioSetOutMute(kOwner, false))
    {
        closeAudioSession(kOwner);
        s_playing = false;
        return;
    }

    int16_t pcm[kFramesPerChunk];
    pager_tone::AdpcmPlaybackState tone_state{};
    while (pager_tone::hasMore(tone_state))
    {
        uint16_t frames = 0;
        while (frames < kFramesPerChunk &&
               pager_tone::nextPlaybackSample(tone_state, pcm[frames]))
        {
            ++frames;
        }
        if (frames == 0)
        {
            break;
        }
        const int write_result = audioWrite(
            kOwner, reinterpret_cast<const uint8_t*>(pcm), frames * sizeof(int16_t));
        if (write_result != 0)
        {
            Serial.printf("[Audio][PagerTone] write failed kind=message rc=%d frames=%u\n",
                          write_result,
                          static_cast<unsigned>(frames));
            break;
        }
        delay(1);
    }

    memset(pcm, 0, sizeof(pcm));
    (void)audioWrite(kOwner, reinterpret_cast<const uint8_t*>(pcm), sizeof(pcm));
    (void)audioWrite(kOwner, reinterpret_cast<const uint8_t*>(pcm), sizeof(pcm));
    delay(7);
    closeAudioSession(kOwner);
    s_playing = false;
#endif
}

void TLoRaPagerBoard::playIncomingCallTone(const volatile bool* stop_requested)
{
#ifndef USING_AUDIO_CODEC
    return;
#else
    if ((stop_requested && *stop_requested) || _message_tone_volume == 0)
    {
        return;
    }
    if (!(devices_probe & HW_CODEC_ONLINE))
    {
        return;
    }

    static bool s_playing = false;
    static uint32_t s_last_play_ms = 0;

    if (s_playing)
    {
        return;
    }

    const uint32_t now = millis();
    if ((now - s_last_play_ms) < 240)
    {
        return;
    }

    s_playing = true;
    s_last_play_ms = now;

    static constexpr size_t kFramesPerChunk = 128;
    namespace call_tone = ::platform::ui::audio::call_notification;

    constexpr PagerAudioOwner kOwner = PagerAudioOwner::IncomingCallTone;
    const int open_result = openAudioSession(
        kOwner, 16, 1, call_tone::kPlaybackSampleRateHz, true);
    if (open_result != 0)
    {
        Serial.printf("[Audio][PagerTone] open failed kind=call rc=%d rate=%lu channels=1\n",
                      open_result,
                      static_cast<unsigned long>(call_tone::kPlaybackSampleRateHz));
        s_playing = false;
        return;
    }

    if (!audioSetMute(kOwner, true) ||
        !audioSetVolume(kOwner, _message_tone_volume) ||
        !audioSetOutMute(kOwner, false))
    {
        closeAudioSession(kOwner);
        s_playing = false;
        return;
    }

    int16_t pcm[kFramesPerChunk];
    call_tone::AdpcmPlaybackState tone_state{};
    while (call_tone::hasMore(tone_state))
    {
        if (stop_requested && *stop_requested)
        {
            break;
        }
        uint16_t frames = 0;
        while (frames < kFramesPerChunk &&
               call_tone::nextPlaybackSample(tone_state, pcm[frames]))
        {
            ++frames;
        }
        if (frames == 0)
        {
            break;
        }
        const int write_result = audioWrite(
            kOwner, reinterpret_cast<const uint8_t*>(pcm), frames * sizeof(int16_t));
        if (write_result != 0)
        {
            Serial.printf("[Audio][PagerTone] write failed kind=call rc=%d frames=%u\n",
                          write_result,
                          static_cast<unsigned>(frames));
            break;
        }
        delay(1);
    }

    memset(pcm, 0, sizeof(pcm));
    (void)audioWrite(kOwner, reinterpret_cast<const uint8_t*>(pcm), sizeof(pcm));
    (void)audioWrite(kOwner, reinterpret_cast<const uint8_t*>(pcm), sizeof(pcm));
    delay(7);
    closeAudioSession(kOwner);
    s_playing = false;
#endif
}

void TLoRaPagerBoard::setMessageToneVolume(uint8_t volume_percent)
{
    if (volume_percent > 100)
    {
        volume_percent = 100;
    }
    _message_tone_volume = volume_percent;
}

uint8_t TLoRaPagerBoard::getMessageToneVolume() const
{
    return _message_tone_volume;
}

void TLoRaPagerBoard::setHapticEffects(uint8_t effects)
{
    if (effects > 127) effects = 127;
    _haptic_effects = effects;
}

uint8_t TLoRaPagerBoard::getHapticEffects()
{
    return _haptic_effects;
}

int TLoRaPagerBoard::getKey(char* c)
{
#ifdef USING_INPUT_DEV_KEYBOARD
    if (devices_probe & HW_KEYBOARD_ONLINE)
    {
        I2CGuard i2c;
        return kb.getKey(c);
    }
#endif
    return -1;
}

int TLoRaPagerBoard::getKeyChar(char* c)
{
    return getKey(c);
}

bool TLoRaPagerBoard::initGPS()
{
    GPS_BOARD_LOG("[TLoRaPagerBoard::initGPS] Starting GPS initialization...\n");
    GPS_BOARD_LOG("[TLoRaPagerBoard::initGPS] Opening Serial1: baud=38400, RX=%d, TX=%d\n", GPS_RX, GPS_TX);

    // Clear HW_GPS_ONLINE flag before attempting initialization
    // This ensures we don't have stale state if reinitializing
    devices_probe &= ~HW_GPS_ONLINE;

    Serial1.end();
    Serial1.begin(38400, SERIAL_8N1, GPS_RX, GPS_TX);
    delay(100); // Give Serial1 time to initialize
    gps.attach(&Serial1, GPS::ConfigProtocol::UbxValset);
    devices_probe |= HW_GPS_ONLINE;
    GPS_BOARD_LOG("[TLoRaPagerBoard::initGPS] GPS UART ready, devices_probe=0x%08X\n", devices_probe);
    return true;
}

void TLoRaPagerBoard::deinitGPS()
{
    Serial1.end();
    devices_probe &= ~HW_GPS_ONLINE;
}

void TLoRaPagerBoard::setBrightness(uint8_t level)
{
    // Low-battery cap: tier 1 = 70% max, tier 2 = 50% max (of DEVICE_MAX_BRIGHTNESS_LEVEL 16)
    uint8_t max_level = DEVICE_MAX_BRIGHTNESS_LEVEL;
    if (power_tier_ >= 2)
    {
        max_level = (DEVICE_MAX_BRIGHTNESS_LEVEL * 50) / 100; // 8
    }
    else if (power_tier_ >= 1)
    {
        max_level = (DEVICE_MAX_BRIGHTNESS_LEVEL * 70) / 100; // 11
    }

    // Over-temperature cap: when battery temperature is high, further clamp brightness to 50%.
    if (s_temp_hot)
    {
        uint8_t hot_cap = (DEVICE_MAX_BRIGHTNESS_LEVEL * 50) / 100;
        if (max_level > hot_cap)
        {
            max_level = hot_cap;
        }
    }
    if (level > max_level)
    {
        level = max_level;
    }
    backlight.setBrightness(level);
}

uint8_t TLoRaPagerBoard::getBrightness()
{
    return backlight.getBrightness();
}

void TLoRaPagerBoard::setRotation(uint8_t rotation)
{
    LilyGoDispArduinoSPI::setRotation(rotation);
}

uint8_t TLoRaPagerBoard::getRotation()
{
    return LilyGoDispArduinoSPI::getRotation();
}

uint16_t TLoRaPagerBoard::width()
{
    return LilyGoDispArduinoSPI::_width;
}

uint16_t TLoRaPagerBoard::height()
{
    return LilyGoDispArduinoSPI::_height;
}

void TLoRaPagerBoard::pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t* color)
{
    LilyGoDispArduinoSPI::pushColors(x1, y1, x2, y2, color);
}

DisplayTransferResult TLoRaPagerBoard::transferPixels(uint16_t x1,
                                                      uint16_t y1,
                                                      uint16_t x2,
                                                      uint16_t y2,
                                                      uint16_t* color)
{
    return LilyGoDispArduinoSPI::transferPixels(x1, y1, x2, y2, color);
}

bool TLoRaPagerBoard::pushColorsResult(uint16_t x1,
                                       uint16_t y1,
                                       uint16_t x2,
                                       uint16_t y2,
                                       uint16_t* color)
{
    return LilyGoDispArduinoSPI::pushColorsResult(x1, y1, x2, y2, color);
}

int TLoRaPagerBoard::transmitRadio(const uint8_t* data, size_t len)
{
    if (!platform::radio::power_policy().enabled()) return RADIOLIB_ERR_SPI_WRITE_FAILED;
    const uint32_t timeout_ms = radio_tx_timeout_ms(radio_, len);
    int rc = RADIOLIB_ERR_SPI_WRITE_FAILED;
    if (withSharedSpiRadioAccess("radio_tx", pdMS_TO_TICKS(50), [&]()
                                 { rc = radio_.startTransmit(data, len); }))
    {
        if (rc != RADIOLIB_ERR_NONE)
        {
            return rc;
        }
    }
    else
    {
        return RADIOLIB_ERR_SPI_WRITE_FAILED;
    }

    const uint32_t started_ms = millis();
    while (digitalRead(LORA_IRQ) == LOW)
    {
        if (static_cast<uint32_t>(millis() - started_ms) > timeout_ms)
        {
            (void)withSharedSpiRadioAccess("radio_tx_finish",
                                           pdMS_TO_TICKS(50),
                                           [&]()
                                           { (void)radio_.finishTransmit(); });
            return RADIOLIB_ERR_TX_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (withSharedSpiRadioAccess("radio_tx_finish", pdMS_TO_TICKS(50), [&]()
                                 { rc = radio_.finishTransmit(); }))
    {
        return rc;
    }
    return RADIOLIB_ERR_SPI_WRITE_FAILED;
}

int TLoRaPagerBoard::radioStandby()
{
    int rc = RADIOLIB_ERR_SPI_WRITE_FAILED;
    if (withSharedSpiRadioAccess("radio_cfg", pdMS_TO_TICKS(50), [&]()
                                 { rc = radio_.standby(); }))
    {
        return rc;
    }
    return RADIOLIB_ERR_SPI_WRITE_FAILED;
}

bool TLoRaPagerBoard::quiesceForExternalStorage()
{
    return !isRadioOnline() || radioStandby() == RADIOLIB_ERR_NONE;
}

int TLoRaPagerBoard::startRadioReceive()
{
    if (!platform::radio::power_policy().enabled()) return RADIOLIB_ERR_SPI_WRITE_FAILED;
    int rc = RADIOLIB_ERR_SPI_WRITE_FAILED;
    if (withSharedSpiRadioAccess("radio_rx", pdMS_TO_TICKS(50), [&]()
                                 { rc = radio_.startReceive(); }))
    {
        return rc;
    }
    return RADIOLIB_ERR_SPI_WRITE_FAILED;
}

uint32_t TLoRaPagerBoard::getRadioIrqFlags()
{
    uint32_t flags = 0;
    if (withSharedSpiRadioAccess("radio_irq", 0, [&]()
                                 { flags = radio_.getIrqFlags(); }))
    {
        return flags;
    }
    return 0;
}

int TLoRaPagerBoard::getRadioPacketLength(bool update)
{
    int len = 0;
    if (withSharedSpiRadioAccess("radio_rx", pdMS_TO_TICKS(20), [&]()
                                 { len = static_cast<int>(radio_.getPacketLength(update)); }))
    {
        return len;
    }
    return 0;
}

int TLoRaPagerBoard::readRadioData(uint8_t* buf, size_t len)
{
    int rc = RADIOLIB_ERR_SPI_WRITE_FAILED;
    if (withSharedSpiRadioAccess("radio_rx", pdMS_TO_TICKS(50), [&]()
                                 { rc = radio_.readData(buf, len); }))
    {
        return rc;
    }
    return RADIOLIB_ERR_SPI_WRITE_FAILED;
}

void TLoRaPagerBoard::clearRadioIrqFlags(uint32_t flags)
{
    (void)withSharedSpiRadioAccess("radio_irq", pdMS_TO_TICKS(20), [&]()
                                   { radio_.clearIrqFlags(flags); });
}

float TLoRaPagerBoard::getRadioRSSI()
{
    float rssi = std::numeric_limits<float>::quiet_NaN();
    if (withSharedSpiRadioAccess("radio_rssi", pdMS_TO_TICKS(20), [&]()
                                 { rssi = radio_.getRSSI(); }))
    {
        return rssi;
    }
    return std::numeric_limits<float>::quiet_NaN();
}

float TLoRaPagerBoard::getRadioInstantRSSI()
{
    float rssi = std::numeric_limits<float>::quiet_NaN();
    auto read_instant_rssi = [&]()
    {
#if defined(ARDUINO_LILYGO_LORA_SX1262)
        rssi = radio_.getRSSI(false);
#else
        rssi = radio_.getRSSI();
#endif
    };
    if (withSharedSpiRadioAccess("radio_rssi", pdMS_TO_TICKS(20), read_instant_rssi))
    {
        return rssi;
    }
    return std::numeric_limits<float>::quiet_NaN();
}

float TLoRaPagerBoard::getRadioSNR()
{
    float snr = std::numeric_limits<float>::quiet_NaN();
    if (withSharedSpiRadioAccess("radio_rssi", pdMS_TO_TICKS(20), [&]()
                                 { snr = radio_.getSNR(); }))
    {
        return snr;
    }
    return std::numeric_limits<float>::quiet_NaN();
}

int TLoRaPagerBoard::configureFskRadio(float freq_mhz, float bit_rate_kbps, float freq_dev_khz, float rx_bw_khz,
                                       int8_t tx_power, uint16_t preamble_len, float tcxo_voltage,
                                       const uint8_t* sync_word, size_t sync_word_len, uint8_t crc_len)
{
    if (!sync_word || sync_word_len == 0)
    {
        return -1;
    }

    int rc = RADIOLIB_ERR_SPI_WRITE_FAILED;
    auto configure = [&]()
    {
        rc = radio_.standby();
        if (rc == RADIOLIB_ERR_NONE)
        {
#if defined(ARDUINO_LILYGO_LORA_LR1121)
            rc = radio_.beginGFSK(freq_mhz, bit_rate_kbps, freq_dev_khz, rx_bw_khz,
                                  tx_power, preamble_len, 3.0f);
#else
            rc = radio_.beginFSK(freq_mhz, bit_rate_kbps, freq_dev_khz, rx_bw_khz,
                                 tx_power, preamble_len, tcxo_voltage);
#endif
        }
        if (rc == RADIOLIB_ERR_NONE)
        {
            rc = radio_.setSyncWord(const_cast<uint8_t*>(sync_word), sync_word_len);
        }
        if (rc == RADIOLIB_ERR_NONE)
        {
            rc = radio_.setCRC(crc_len);
        }
        if (rc == RADIOLIB_ERR_NONE)
        {
            rc = radio_.setPreambleLength(preamble_len);
        }
    };
    if (withSharedSpiRadioAccess("radio_cfg", pdMS_TO_TICKS(200), configure))
    {
        return rc;
    }
    return RADIOLIB_ERR_SPI_WRITE_FAILED;
}

int TLoRaPagerBoard::restoreLoRaRadio()
{
    CachedLoRaConfig cached = lora_config_;

    int rc = RADIOLIB_ERR_SPI_WRITE_FAILED;
    auto restore = [&]()
    {
#if defined(ARDUINO_LILYGO_LORA_LR1121)
        rc = radio_.begin(cached.valid ? cached.freq_mhz : 434.0f,
                          cached.valid ? cached.bw_khz : 125.0f,
                          cached.valid ? cached.sf : 9,
                          cached.valid ? cached.cr_denom : 7,
                          cached.valid ? cached.sync_word : RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE,
                          cached.valid ? cached.tx_power : 10,
                          cached.valid ? cached.preamble_len : 8,
                          3.0f);
#else
        rc = radio_.begin();
#endif
    };
    if (withSharedSpiRadioAccess("radio_cfg", pdMS_TO_TICKS(200), restore))
    {
        if (rc != RADIOLIB_ERR_NONE)
        {
            return rc;
        }

        if (cached.valid)
        {
            return configureLoraRadio(cached.freq_mhz,
                                      cached.bw_khz,
                                      cached.sf,
                                      cached.cr_denom,
                                      cached.tx_power,
                                      cached.preamble_len,
                                      cached.sync_word,
                                      cached.crc_len);
        }
        return rc;
    }
    return RADIOLIB_ERR_SPI_WRITE_FAILED;
}

int TLoRaPagerBoard::startRadioTransmit(const uint8_t* data, size_t len)
{
    int rc = RADIOLIB_ERR_SPI_WRITE_FAILED;
    if (withSharedSpiRadioAccess("radio_tx", pdMS_TO_TICKS(50), [&]()
                                 { rc = radio_.startTransmit(const_cast<uint8_t*>(data), len); }))
    {
        return rc;
    }
    return RADIOLIB_ERR_SPI_WRITE_FAILED;
}

int TLoRaPagerBoard::finishRadioTransmit()
{
    int rc = RADIOLIB_ERR_SPI_WRITE_FAILED;
    if (withSharedSpiRadioAccess("radio_tx_finish", pdMS_TO_TICKS(50), [&]()
                                 { rc = radio_.finishTransmit(); }))
    {
        return rc;
    }
    return RADIOLIB_ERR_SPI_WRITE_FAILED;
}

#if defined(ARDUINO_LILYGO_LORA_SX1262)
static void apply_tx_power(SX1262Access& radio, int8_t tx_power)
{
    constexpr int8_t kTxPowerMinDbm = -9;
    int8_t clipped = tx_power;
    if (clipped < kTxPowerMinDbm) clipped = kTxPowerMinDbm;
    radio.setOutputPower(clipped);
}
#endif

int TLoRaPagerBoard::configureLoraRadio(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr_denom,
                                        int8_t tx_power, uint16_t preamble_len, uint8_t sync_word,
                                        uint8_t crc_len)
{
    CachedLoRaConfig requested{};
    requested.valid = true;
    requested.freq_mhz = freq_mhz;
    requested.bw_khz = bw_khz;
    requested.sf = sf;
    requested.cr_denom = cr_denom;
    requested.tx_power = tx_power;
    requested.preamble_len = preamble_len;
    requested.sync_word = sync_word;
    requested.crc_len = crc_len;

    int config_rc = RADIOLIB_ERR_SPI_WRITE_FAILED;
    auto configure = [&]()
    {
        int first_error = RADIOLIB_ERR_NONE;
        const char* failed_step = nullptr;
        auto note_error = [&](const char* step, int rc)
        {
            if (rc != RADIOLIB_ERR_NONE && first_error == RADIOLIB_ERR_NONE)
            {
                first_error = rc;
                failed_step = step;
            }
        };

        note_error("setFrequency", radio_.setFrequency(freq_mhz));
        note_error("setBandwidth", radio_.setBandwidth(bw_khz));
        note_error("setSpreadingFactor", radio_.setSpreadingFactor(sf));
        note_error("setCodingRate", radio_.setCodingRate(cr_denom));
#if defined(ARDUINO_LILYGO_LORA_SX1262)
        apply_tx_power(radio_, tx_power);
#else
        note_error("setOutputPower", radio_.setOutputPower(tx_power));
#endif
        note_error("setPreambleLength", radio_.setPreambleLength(preamble_len));
        note_error("setSyncWord", radio_.setSyncWord(sync_word));
        note_error("setCRC", radio_.setCRC(crc_len));
        if (first_error != RADIOLIB_ERR_NONE)
        {
            log_w("LoRa config step %s returned code: %d",
                  failed_step ? failed_step : "unknown",
                  first_error);
        }
        config_rc = first_error;
    };
    if (!withSharedSpiRadioAccess("radio_cfg", pdMS_TO_TICKS(100), configure))
    {
        return RADIOLIB_ERR_SPI_WRITE_FAILED;
    }
    if (config_rc == RADIOLIB_ERR_NONE)
    {
        lora_config_ = requested;
    }
    return config_rc;
}

bool TLoRaPagerBoard::hasEncoder()
{
    return true;
}

bool TLoRaPagerBoard::hasKeyboard()
{
#ifdef USING_INPUT_DEV_KEYBOARD
    return (devices_probe & HW_KEYBOARD_ONLINE) != 0;
#else
    return false;
#endif
}

void TLoRaPagerBoard::keyboardSetBrightness(uint8_t level)
{
#ifdef USING_INPUT_DEV_KEYBOARD
    kb.setBrightness(level);
#else
    (void)level;
#endif
}

uint8_t TLoRaPagerBoard::keyboardGetBrightness()
{
#ifdef USING_INPUT_DEV_KEYBOARD
    return kb.getBrightness();
#else
    return 0;
#endif
}

bool TLoRaPagerBoard::syncTimeFromGPS(uint32_t gps_task_interval_ms)
{
    // Check if GPS time is valid
    if (!gps.date.isValid() || !gps.time.isValid())
    {
        GPS_BOARD_LOG("[TLoRaPagerBoard::syncTimeFromGPS] GPS time not valid (date valid=%d, time valid=%d)\n",
                      gps.date.isValid(), gps.time.isValid());
        return false;
    }

    // Check if RTC is ready
    if (!isRTCReady())
    {
        GPS_BOARD_LOG("[TLoRaPagerBoard::syncTimeFromGPS] RTC not ready\n");
        return false;
    }

    // Record timestamp when we start reading GPS time (for delay compensation)
    uint32_t read_start_ms = millis();

    // Get GPS date and time
    uint16_t year = gps.date.year();
    uint8_t month = gps.date.month();
    uint8_t day = gps.date.day();
    uint8_t hour = gps.time.hour();
    uint8_t minute = gps.time.minute();
    uint8_t second = gps.time.second();

    // Get satellite count for logging (may be 0 even if time is valid)
    uint8_t sat_count = gps.satellites.value();
    bool has_fix = gps.location.isValid();

    // Validate date/time values (basic sanity check)
    if (year < 2020 || year > 2100 ||
        month < 1 || month > 12 ||
        day < 1 || day > 31 ||
        hour >= 24 || minute >= 60 || second >= 60)
    {
        GPS_BOARD_LOG("[TLoRaPagerBoard::syncTimeFromGPS] Invalid GPS time values: %04d-%02d-%02d %02d:%02d:%02d\n",
                      year, month, day, hour, minute, second);
        return false;
    }

    // Calculate delay compensation:
    // The GPS task runs periodically, so we need to compensate for various delays:
    // 1. GPS NMEA message age: NMEA messages are typically 0.5-2 seconds old when received
    //    (depends on GPS module update rate, typically 1Hz = 1 second intervals)
    // 2. GPS task interval: If task runs every N seconds, GPS data could be up to N seconds old
    //    However, GPS module updates time every second, so worst case is ~1s old (not N seconds)
    // 3. Processing delay: time from reading GPS data to setting RTC (typically < 100ms)
    // 4. RTC write delay: I2C communication time (typically < 50ms)

    uint32_t processing_delay_ms = millis() - read_start_ms;

    // Estimate GPS message age: NMEA messages are typically 1 second old (1Hz update rate)
    // For higher update rates (5Hz, 10Hz), this would be smaller, but 1Hz is most common
    // Note: Even if GPS task runs every 60s, GPS module itself updates time every second,
    // so the time data is at most ~1 second old (worst case: we read just before next update)
    const uint32_t estimated_gps_message_age_ms = 1000; // Typical NMEA 1Hz update = 1 second old

    // However, we should also consider that GPS time might not be perfectly synchronized
    // with the actual current time. GPS time from satellites can have some inherent delay.
    // For better accuracy, we use a more conservative estimate: 2 seconds total
    // But if GPS task interval is very large (e.g., 60s), we should be more conservative
    const uint32_t base_delay_ms = 2000; // Base conservative estimate: 2 seconds

    // If GPS task interval is provided and is large, add additional compensation
    // (though GPS module updates every second, large task intervals mean we might miss
    //  the most recent update, so add half the interval as additional safety margin)
    // However, we cap this at a reasonable maximum (e.g., 5 seconds) to avoid over-compensation
    uint32_t task_interval_compensation_ms = 0;
    if (gps_task_interval_ms > 0 && gps_task_interval_ms > 5000)
    {
        // For large intervals (>5s), add compensation, but cap at 5 seconds
        // This accounts for the possibility that we read GPS data just before it updates
        task_interval_compensation_ms = (gps_task_interval_ms / 2);
        if (task_interval_compensation_ms > 5000)
        {
            task_interval_compensation_ms = 5000; // Cap at 5 seconds
        }
    }

    // Total delay = base delay + task interval compensation + processing delay
    uint32_t total_delay_ms = base_delay_ms + task_interval_compensation_ms + processing_delay_ms;

    // Log original GPS time before compensation for debugging
    GPS_BOARD_LOG("[TLoRaPagerBoard::syncTimeFromGPS] Original GPS time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  year, month, day, hour, minute, second);

    // Add delay compensation to seconds (round to nearest second)
    uint32_t total_seconds = (uint32_t)hour * 3600 + (uint32_t)minute * 60 + (uint32_t)second;
    uint32_t delay_seconds = (total_delay_ms + 500) / 1000; // Round to nearest second
    total_seconds += delay_seconds;

    // Handle day overflow
    if (total_seconds >= 86400)
    {
        total_seconds -= 86400;
        day++;
        // Handle month overflow (simplified - doesn't handle all edge cases like Feb 29)
        uint8_t days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        uint8_t max_days = days_in_month[month - 1];
        // Handle leap year for February
        if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)))
        {
            max_days = 29;
        }
        if (day > max_days)
        {
            day = 1;
            month++;
            if (month > 12)
            {
                month = 1;
                year++;
            }
        }
    }

    // Convert back to hour, minute, second
    hour = (total_seconds / 3600) % 24;
    minute = (total_seconds / 60) % 60;
    second = total_seconds % 60;

    // Set RTC time from GPS (with delay compensation)
    rtc.setDateTime(year, month, day, hour, minute, second);

    // Record timestamp after RTC write (for logging)
    uint32_t write_end_ms = millis();
    uint32_t total_operation_ms = write_end_ms - read_start_ms;

    // Note: GPS time can be valid even without location fix
    // Time information requires fewer satellites than location fix (typically 1-2 vs 4+)
    GPS_BOARD_LOG("[TLoRaPagerBoard::syncTimeFromGPS] Time synced: %04d-%02d-%02d %02d:%02d:%02d (sat=%d, has_fix=%d, base_delay=%lums, task_comp=%lums, proc_delay=%lums, total_delay=%lums, op_time=%lums)\n",
                  year, month, day, hour, minute, second, sat_count, has_fix,
                  base_delay_ms, task_interval_compensation_ms, processing_delay_ms, total_delay_ms, total_operation_ms);
    return true;
}

bool TLoRaPagerBoard::getRTCTimeString(char* buffer, size_t buffer_size, bool show_seconds)
{
    if (!isRTCReady() || buffer == nullptr)
    {
        return false;
    }

    // Check buffer size based on format
    if (show_seconds && buffer_size < 9)
    {
        return false; // Need at least 9 bytes for "HH:MM:SS\0"
    }
    if (!show_seconds && buffer_size < 6)
    {
        return false; // Need at least 6 bytes for "HH:MM\0"
    }

    // Read the time registers directly via I2C
    // PCF85063 time registers (I2C address 0x51, registers 0x04-0x06)
    I2CGuard i2c;
    uint8_t hour, minute, second = 0;

    uint8_t start_register = show_seconds ? 0x04 : 0x05; // Start at seconds or minutes
    uint8_t bytes_to_read = show_seconds ? 3 : 2;        // Read 3 bytes (sec,min,hour) or 2 bytes (min,hour)

    Wire.beginTransmission(0x51); // PCF85063 I2C address (0x51 = 0xA2 >> 1)
    Wire.write(start_register);
    uint8_t error = Wire.endTransmission();
    if (error != 0)
    {
        // I2C communication failed - try alternative address
        // Some PCF85063 modules use 0x68 instead of 0x51
        Wire.beginTransmission(0x68);
        Wire.write(start_register);
        error = Wire.endTransmission();
        if (error != 0)
        {
            return false;
        }
        Wire.requestFrom((uint8_t)0x68, (uint8_t)bytes_to_read);
    }
    else
    {
        Wire.requestFrom((uint8_t)0x51, (uint8_t)bytes_to_read);
    }

    if (Wire.available() < bytes_to_read)
    {
        return false;
    }

    if (show_seconds)
    {
        uint8_t sec_bcd = Wire.read();
        uint8_t min_bcd = Wire.read();
        uint8_t hour_bcd = Wire.read();

        // Convert BCD to decimal
        second = ((sec_bcd >> 4) & 0x07) * 10 + (sec_bcd & 0x0F);
        minute = ((min_bcd >> 4) & 0x07) * 10 + (min_bcd & 0x0F);
        hour = ((hour_bcd >> 4) & 0x03) * 10 + (hour_bcd & 0x0F);

        // Validate values
        if (hour >= 24 || minute >= 60 || second >= 60)
        {
            return false;
        }

        // Format time string as HH:MM:SS
        snprintf(buffer, buffer_size, "%02d:%02d:%02d", hour, minute, second);
    }
    else
    {
        // Read only minutes and hours (skip seconds register entirely)
        uint8_t min_bcd = Wire.read();
        uint8_t hour_bcd = Wire.read();

        // Convert BCD to decimal
        minute = ((min_bcd >> 4) & 0x07) * 10 + (min_bcd & 0x0F);
        hour = ((hour_bcd >> 4) & 0x03) * 10 + (hour_bcd & 0x0F);

        // Validate values
        if (hour >= 24 || minute >= 60)
        {
            return false;
        }

        // Format time string as HH:MM (minimum resource usage)
        snprintf(buffer, buffer_size, "%02d:%02d", hour, minute);
    }

    return true;
}

static int bcdToDec(uint8_t val)
{
    return ((val >> 4) * 10) + (val & 0x0F);
}

static uint8_t decToBcd(int val)
{
    return (uint8_t)(((val / 10) << 4) | (val % 10));
}

static int daysInMonth(int year, int month)
{
    static const int kDaysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int days = kDaysInMonth[month - 1];
    bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    if (month == 2 && leap)
    {
        days = 29;
    }
    return days;
}

bool TLoRaPagerBoard::adjustRTCByOffsetMinutes(int offset_minutes)
{
    if (!isRTCReady())
    {
        return false;
    }
    if (offset_minutes == 0)
    {
        return true;
    }

    uint8_t start_register = 0x04;
    uint8_t bytes_to_read = 7; // sec, min, hour, day, weekday, month, year
    uint8_t buf[7] = {0};

    {
        I2CGuard i2c;
        Wire.beginTransmission(0x51);
        Wire.write(start_register);
        uint8_t error = Wire.endTransmission();
        if (error != 0)
        {
            Wire.beginTransmission(0x68);
            Wire.write(start_register);
            error = Wire.endTransmission();
            if (error != 0)
            {
                return false;
            }
            Wire.requestFrom((uint8_t)0x68, bytes_to_read);
        }
        else
        {
            Wire.requestFrom((uint8_t)0x51, bytes_to_read);
        }

        if (Wire.available() < bytes_to_read)
        {
            return false;
        }
        for (uint8_t i = 0; i < bytes_to_read; ++i)
        {
            buf[i] = Wire.read();
        }
    }

    int second = bcdToDec(buf[0] & 0x7F);
    int minute = bcdToDec(buf[1] & 0x7F);
    int hour = bcdToDec(buf[2] & 0x3F);
    int day = bcdToDec(buf[3] & 0x3F);
    int month = bcdToDec(buf[5] & 0x1F);
    int year = 2000 + bcdToDec(buf[6]);

    int total_minutes = hour * 60 + minute + offset_minutes;
    while (total_minutes < 0)
    {
        total_minutes += 1440;
        day -= 1;
        if (day < 1)
        {
            month -= 1;
            if (month < 1)
            {
                month = 12;
                year -= 1;
            }
            day = daysInMonth(year, month);
        }
    }
    while (total_minutes >= 1440)
    {
        total_minutes -= 1440;
        day += 1;
        int dim = daysInMonth(year, month);
        if (day > dim)
        {
            day = 1;
            month += 1;
            if (month > 12)
            {
                month = 1;
                year += 1;
            }
        }
    }

    hour = total_minutes / 60;
    minute = total_minutes % 60;

    rtc.setDateTime(year, month, day, hour, minute, second);
    return true;
}

bool board_adjust_rtc_by_offset_minutes(int offset_minutes)
{
    return instance.adjustRTCByOffsetMinutes(offset_minutes);
}

int TLoRaPagerBoard::getBatteryLevel()
{
    constexpr int kVoltageMinMv = 3000;
    constexpr int kVoltageMaxMv = 4300;

    auto read_bq27220_soc_direct = []() -> int
    {
        // Caller must hold I2C mutex (used inside getBatteryLevel's I2CGuard block)
        constexpr uint8_t i2c_addr = 0x55;
        Wire.beginTransmission(i2c_addr);
        Wire.write(0x2C);
        uint8_t error = Wire.endTransmission();
        if (error != 0)
        {
            return -1;
        }
        Wire.requestFrom(i2c_addr, (uint8_t)2);
        if (Wire.available() < 2)
        {
            return -1;
        }
        const uint8_t lsb = Wire.read();
        const uint8_t msb = Wire.read();
        const uint16_t soc_raw = (uint16_t)msb << 8 | lsb;
        if (soc_raw <= 100)
        {
            return (int)soc_raw;
        }
        if (soc_raw <= 1000)
        {
            return (int)(soc_raw / 10);
        }
        return -1;
    };

    auto read_battery_mv_from_adc = []() -> int
    {
#ifdef BOARD_BAT_ADC
        return ::platform::esp::arduino_common::power::read_battery_adc_millivolts(BOARD_BAT_ADC, 2, 1);
#else
        return -1;
#endif
    };

    int soc = -1;
    int voltage_mv = -1;
    float temp_c = NAN;
    bool gauge_ok = false;
    bool temp_ok = false;
    bool charging = false;
    bool vbus_present = false;

    {
        I2CGuard i2c;
        if (isPMUReady())
        {
            charging = pmu.isCharging();
            vbus_present = pmu.isVbusIn();
        }

        if (isGaugeReady())
        {
            bool refresh_ok = gauge.refresh();
            if (refresh_ok)
            {
                soc = static_cast<int>(gauge.getStateOfCharge());
                voltage_mv = static_cast<int>(gauge.getVoltage());
                temp_c = gauge.getTemperature();
                temp_ok = std::isfinite(temp_c) && temp_c > -20.0f && temp_c < 80.0f;
                if (temp_ok)
                {
                    s_last_temp_c = temp_c;
                    s_temp_valid = true;

                    // Temperature bands: Hot / Cold with a small hysteresis window.
                    if (temp_c > 45.0f)
                    {
                        s_temp_hot = true;
                    }
                    else if (temp_c < 40.0f)
                    {
                        s_temp_hot = false;
                    }
                    if (temp_c < 5.0f)
                    {
                        s_temp_cold = true;
                    }
                    else if (temp_c > 8.0f)
                    {
                        s_temp_cold = false;
                    }
                }
                if (soc >= 0 && soc <= 100)
                {
                    gauge_ok = true;
                    s_gauge_error_streak = 0;
                }
            }

            if (!gauge_ok)
            {
                // Gauge read failed: increase error counter and attempt re-init after several consecutive failures.
                s_gauge_error_streak++;
                if (s_gauge_error_streak >= 3)
                {
                    log_w("Gauge SOC read failed %d times, attempting BQ27220 reinit", s_gauge_error_streak);
                    if (gauge.begin(Wire, SDA, SCL))
                    {
                        s_gauge_error_streak = 0;
                        s_gauge_reinit_count++;
                        log_w("Gauge reinit successful (count=%d)", s_gauge_reinit_count);
                    }
                    else
                    {
                        log_w("Gauge reinit failed");
                    }
                }
            }
        }

        if (!gauge_ok)
        {
            soc = read_bq27220_soc_direct();
            if (soc >= 0 && soc <= 100)
            {
                gauge_ok = true;
                // Direct register read succeeded; treat gauge as recovered and clear error counter.
                s_gauge_error_streak = 0;
            }
        }

        if (voltage_mv < kVoltageMinMv || voltage_mv > kVoltageMaxMv)
        {
            voltage_mv = -1;
        }

        if (voltage_mv < 0 && isPMUReady())
        {
            const uint16_t pmu_mv = pmu.getBattVoltage();
            if (pmu_mv >= kVoltageMinMv && pmu_mv <= kVoltageMaxMv)
            {
                voltage_mv = static_cast<int>(pmu_mv);
            }
        }
    }

    // Temperature notifications: only when we have a valid reading, with cooldowns to avoid spam.
    if (s_temp_valid)
    {
        const uint32_t now_ms = millis();
        if (s_temp_hot && (now_ms - s_last_temp_notice_hot_ms) > 60000)
        {
            ::ui::feedback::show_notice("Device hot - limiting brightness", 3000);
            s_last_temp_notice_hot_ms = now_ms;
        }
        if (s_temp_cold && (now_ms - s_last_temp_notice_cold_ms) > 600000)
        {
            ::ui::feedback::show_notice("Low temp - battery may read wrong", 3000);
            s_last_temp_notice_cold_ms = now_ms;
        }
    }

    int adc_mv = read_battery_mv_from_adc();
    if (adc_mv < kVoltageMinMv || adc_mv > kVoltageMaxMv)
    {
        adc_mv = -1;
    }

    power::BatteryEstimatorSample sample{};
    sample.now_ms = millis();
    sample.pmu_percent = gauge_ok ? soc : -1;
    sample.pmu_battery_mv = voltage_mv;
    sample.adc_battery_mv = adc_mv;
    sample.charging = charging;
    sample.vbus_present = vbus_present;

    const power::BatteryEstimate estimate = battery_estimator_.update(sample);
    if (estimate.percent < 0)
    {
        return -1;
    }
    return estimate.percent;
}

bool TLoRaPagerBoard::isCharging()
{
    if (!isPMUReady())
    {
        return false;
    }
    I2CGuard i2c;
    return pmu.isCharging();
}

int TLoRaPagerBoard::getUsbVoltageMv_bestEffort() const
{
    // Only available when PMU is online; returns 0 when VBUS is not present or read fails.
    TLoRaPagerBoard* self = const_cast<TLoRaPagerBoard*>(this);
    if (!self->isPMUReady())
    {
        return 0;
    }
    I2CGuard i2c;
    return self->pmu.getVbusVoltage();
}

int TLoRaPagerBoard::getChargeCurrentMa_bestEffort() const
{
    // Only meaningful while charging; returns 0 when not charging or on read failure.
    TLoRaPagerBoard* self = const_cast<TLoRaPagerBoard*>(this);
    if (!self->isPMUReady())
    {
        return 0;
    }
    I2CGuard i2c;
    return self->pmu.getChargeCurrent();
}

float TLoRaPagerBoard::getBatteryTemperatureC_bestEffort() const
{
    if (!s_temp_valid)
    {
        return NAN;
    }
    return s_last_temp_c;
}

void TLoRaPagerBoard::reloadGaugeCapacityFromPrefs()
{
    if (!isGaugeReady())
    {
        return;
    }

    uint16_t designCapacity = 1500;
    uint16_t fullChargeCapacity = 1500;
    uint32_t d = ::platform::ui::settings_store::get_uint("power", "gauge_design_mah", designCapacity);
    uint32_t f = ::platform::ui::settings_store::get_uint("power", "gauge_full_mah", fullChargeCapacity);
    if (d > 0 && d <= 10000)
    {
        designCapacity = static_cast<uint16_t>(d);
    }
    if (f > 0 && f <= 10000)
    {
        fullChargeCapacity = static_cast<uint16_t>(f);
    }

    {
        I2CGuard i2c;
        gauge.setNewCapacity(designCapacity, fullChargeCapacity);
    }
    log_d("Battery gauge capacity reapplied from prefs: design=%umAh full=%umAh",
          designCapacity, fullChargeCapacity);
}

int TLoRaPagerBoard::readADC(uint8_t pin, uint8_t samples)
{
    // Validate sample count (optimal: 8 samples for accuracy vs speed balance)
    if (samples == 0 || samples > 64)
    {
        samples = 8; // Default to 8 samples (optimal balance: accurate but fast)
    }

    // ESP32 ADC notes:
    // - ADC1: GPIO 32-39 (safe, no WiFi conflict)
    // - ADC2: GPIO 0, 2, 4, 12-15, 25-27 (conflicts with WiFi, avoid if WiFi is used)
    // - Use analogRead() which handles pin validation and attenuation setup

    // Multiple sampling with averaging for accuracy
    // This reduces noise while keeping resource usage minimal
    // 8 samples is optimal: good accuracy (~3-5% noise reduction) with minimal overhead (~1-2ms)
    uint32_t sum = 0;

    // Read multiple samples and sum them
    // No delay needed between samples - analogRead() has internal settling time
    for (uint8_t i = 0; i < samples; i++)
    {
        int value = analogRead(pin);
        if (value < 0)
        {
            return -1; // Invalid pin
        }
        sum += (uint32_t)value;
    }

    // Return average (rounded to nearest integer)
    return (int)((sum + samples / 2) / samples);
}

int TLoRaPagerBoard::readADCVoltage(uint8_t pin, uint8_t samples, uint8_t attenuation)
{
    // Read raw ADC value (0-4095 for 12-bit ESP32 ADC)
    int adc_value = readADC(pin, samples);
    if (adc_value < 0)
    {
        return -1;
    }

    // Convert ADC value to voltage in millivolts based on attenuation
    // ESP32 ADC attenuation settings:
    // 0 = 0dB:   0-1.1V  range (reference ~1.1V)
    // 1 = 2.5dB: 0-1.5V  range (reference ~1.5V)
    // 2 = 6dB:   0-2.2V  range (reference ~2.2V)
    // 3 = 11dB:  0-3.3V  range (reference ~3.3V, most common for battery voltage)

    uint32_t voltage_mv = 0;

    switch (attenuation)
    {
    case 0:                                               // 0dB
        voltage_mv = ((uint32_t)adc_value * 1100) / 4095; // 0-1.1V range
        break;
    case 1:                                               // 2.5dB
        voltage_mv = ((uint32_t)adc_value * 1500) / 4095; // 0-1.5V range
        break;
    case 2:                                               // 6dB
        voltage_mv = ((uint32_t)adc_value * 2200) / 4095; // 0-2.2V range
        break;
    case 3: // 11dB (default, most common)
    default:
        voltage_mv = ((uint32_t)adc_value * 3300) / 4095; // 0-3.3V range
        break;
    }

    return (int)voltage_mv;
}

RotaryMsg_t TLoRaPagerBoard::getRotary()
{
    static RotaryMsg_t msg;
    // LVGL polls input while the display-first boot frame is presented. The
    // rotary queue is deliberately created later in beginServices(), so an
    // uninitialized queue means "no input yet", not a valid FreeRTOS queue.
    if (rotaryMsg == nullptr)
    {
        msg.centerBtnPressed = false;
        msg.dir = ROTARY_DIR_NONE;
        return msg;
    }
    if (xQueueReceive(rotaryMsg, &msg, pdMS_TO_TICKS(50)) == pdPASS)
    {
        return msg;
    }
    // Keep the last debounced button level between queue events. The rotary
    // task queues only edges, whereas LVGL polls the current level every
    // input cycle. Clearing this here fabricated an immediate release a few
    // milliseconds after every press and made long-press UI actions unreliable.
    msg.dir = ROTARY_DIR_NONE;
    return msg;
}

void TLoRaPagerBoard::feedback(void* args)
{
    (void)args;
}

// BOOT button handling variables. The physical POWER key is wired to BQ25896 QON
// and is only useful while the device is already powered off.
static volatile bool power_button_event = false;
static volatile bool power_button_state = false; // true = pressed, false = released
static volatile bool power_button_long_press_handled = false;
static volatile uint32_t power_button_press_start = 0;
static const uint32_t BOOT_BUTTON_LONG_PRESS_MS = 3000; // 3 seconds for shutdown
static const uint32_t BOOT_BUTTON_DEBOUNCE_MS = 50;     // Debounce delay

/**
 * @brief BOOT button interrupt handler
 * Detects BOOT button press and release events
 */
static void IRAM_ATTR powerButtonISR()
{
    static uint32_t last_interrupt_time = 0;
    uint32_t current_time = micros() / 1000; // Convert to milliseconds

    // Debounce: ignore interrupts too close together
    if (current_time - last_interrupt_time < BOOT_BUTTON_DEBOUNCE_MS)
    {
        return;
    }
    last_interrupt_time = current_time;

    bool current_button_state = (digitalRead(BOOT_KEY) == LOW); // Active low

    // Only trigger event on state change
    if (current_button_state != power_button_state)
    {
        power_button_state = current_button_state;
        power_button_event = true;

        if (current_button_state)
        {
            // Button pressed
            power_button_press_start = current_time;
            power_button_long_press_handled = false;
        }
    }
}

bool TLoRaPagerBoard::initPowerButton()
{
    // Configure BOOT button pin
    pinMode(BOOT_KEY, INPUT_PULLUP);

    // Test initial pin state
    int initial_state = digitalRead(BOOT_KEY);
    log_d("BOOT button GPIO %d initial state: %d (0=pressed, 1=released)", BOOT_KEY, initial_state);

    // Attach interrupt for both rising and falling edges
    // Note: attachInterrupt returns void in Arduino, no error checking possible
    attachInterrupt(digitalPinToInterrupt(BOOT_KEY), powerButtonISR, CHANGE);

    log_d("BOOT button interrupt attached to GPIO %d", BOOT_KEY);
    return true;
}

void TLoRaPagerBoard::handlePowerButton()
{
    // According to LilyGo docs, the physical POWER key only wakes the device from
    // Power OFF. This handler is for the separate BOOT/GPIO0 key.
    if (power_button_state && !power_button_long_press_handled)
    {
        uint32_t press_duration_ms = millis() - power_button_press_start;
        if (press_duration_ms >= BOOT_BUTTON_LONG_PRESS_MS)
        {
            power_button_long_press_handled = true;
            log_i("BOOT key long-press (%lu ms) -> deep sleep", (unsigned long)press_duration_ms);
            shutdown(true);
        }
    }

    if (power_button_event)
    {
        power_button_event = false; // Clear the event flag

        if (power_button_state)
        {
            log_d("BOOT button pressed");

            // Check if we are waking from deep sleep.
            esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
            if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1)
            {
                log_d("Waking up from deep sleep via BOOT button");
                wakeUp();
            }
            else
            {
                log_d("BOOT button pressed while device is running");
            }
        }
        else
        {
            log_d("BOOT button released");
        }
    }
}

void TLoRaPagerBoard::shutdown(bool save_data)
{
    shutdownImpl(save_data, ShutdownMode::DeepSleep);
}

void TLoRaPagerBoard::shutdownImpl(bool save_data, ShutdownMode mode)
{
    (void)save_data;

    if (mode == ShutdownMode::PowerOff)
    {
        log_i("=== LILYGO BQ25896 POWER-OFF SEQUENCE ===");
        if (isUsbPresent_bestEffort())
        {
            log_w("Power OFF rejected: USB is connected");
            ui::feedback::show_notice("Unplug USB to power off", 3500);
            return;
        }
        if (!isPMUReady())
        {
            log_w("Power OFF rejected: PMU unavailable");
            ui::feedback::show_notice("Power chip unavailable", 2500);
            return;
        }

        log_i("Requesting BQ25896 Power OFF via BATFET_DIS");
        Serial.flush();
        {
            I2CGuard i2c;
            pmu.shutdown();
        }

        delay(1500);
        log_e("BQ25896 Power OFF request returned; device is still running");
        ui::feedback::show_notice("Power off failed", 3500);
        return;
    }

    log_i("=== LILYGO ESP DEEP-SLEEP SEQUENCE ===");

    // 1) Stop rotary task (LilyGo: vTaskDelete(rotaryHandler))
    if (rotaryHandler != nullptr)
    {
        vTaskDelete(rotaryHandler);
        rotaryHandler = nullptr;
    }

    // 2) Disable keyboard if online
    if (devices_probe & HW_KEYBOARD_ONLINE)
    {
        kb.end();
    }

    // 3) Turn off backlight
    backlight.setBrightness(0);

#if defined(USING_XL9555_EXPANDS)
    // 4) Disable audio codec
#if defined(USING_AUDIO_CODEC)
    codec.end();
#endif

    // 5) Pull down all XL9555 controlled lines (exact LilyGo order)
    const uint8_t expands[] = {
#ifdef EXPANDS_DISP_RST
        EXPANDS_DISP_RST,
#endif /*EXPANDS_DISP_RST*/
        EXPANDS_KB_RST,
        EXPANDS_LORA_EN,
        EXPANDS_GPS_EN,
        EXPANDS_DRV_EN,
        EXPANDS_AMP_EN,
#ifdef EXPANDS_GPS_RST
        EXPANDS_GPS_RST,
#endif /*EXPANDS_GPS_RST*/
#ifdef EXPANDS_KB_EN
        EXPANDS_KB_EN,
#endif /*EXPANDS_KB_EN*/
#ifdef EXPANDS_GPIO_EN
        EXPANDS_GPIO_EN,
#endif /*EXPANDS_GPIO_EN*/
#ifdef EXPANDS_SD_DET
        EXPANDS_SD_DET,
#endif /*EXPANDS_SD_DET*/
    };
    for (auto pin : expands)
    {
        io.digitalWrite(pin, LOW);
        delay(1);
    }
#endif

    // 6) Stop haptic driver
    drv.stop();

    // 7) Reset motion sensor
    sensor.reset();

    // 8) Put display to sleep and end SPI display
    LilyGoDispArduinoSPI::sleep();
    LilyGoDispArduinoSPI::end();

    // 9) LilyGo 3-second countdown
    int i = 3;
    while (i--)
    {
        log_d("%d second sleep ...", i);
        delay(1000);
    }

#if defined(USING_XL9555_EXPANDS)
    // 10) Handle SD card power
    if (io.digitalRead(EXPANDS_SD_DET))
    {
        uninstallSD();
    }
    else
    {
        powerControl(POWER_SD_CARD, false);
    }
#endif

    Serial.flush();
    delay(200);

    log_i("Entering BOOT-button deep sleep");

    // 11) End communication buses before ESP deep sleep
    Serial1.end();
    sys::runtime::BusAcquireRequest spi_end_request{};
    spi_end_request.resource = kSharedSpiBusResource;
    spi_end_request.policy = sys::runtime::BusAccessPolicy::RecoveryExclusive;
    spi_end_request.command_id = kSharedSpiBusOwnerId + 2U;
    spi_end_request.origin = kSharedSpiBusOwnerId;
    spi_end_request.deadline_ms = sys::millis_now() + 500U;
    spi_end_request.owner_label = "pager_shutdown_spi_end";
    sys::runtime::ScopedBusAccessToken spi_end_token(
        ::platform::esp::common::shared_spi_coordinator(),
        spi_end_request);
    if (spi_end_token.acquired())
    {
        SPI.end();
    }
    else
    {
        // Deep sleep resets the controller anyway. Never force teardown over
        // another valid owner merely to satisfy an optional shutdown cleanup.
        log_w("Skipping shared SPI end before deep sleep: coordinator unavailable");
    }
    spi_end_token.release();
    Wire.end();

    // 12) Set key GPIOs to OPEN_DRAIN (exact LilyGo pin list)
    const uint8_t pins[] = {
        SD_CS,
        KB_INT,
        KB_BACKLIGHT,
        ROTARY_A,
        ROTARY_B,
        ROTARY_C,
        RTC_INT,
        SENSOR_INT,
#if defined(USING_PDM_MICROPHONE)
        MIC_SCK,
        MIC_DAT,
#endif
#if defined(USING_PCM_AMPLIFIER)
        I2S_BCLK,
        I2S_WCLK,
        I2S_DOUT,
#endif
#if defined(USING_AUDIO_CODEC)
        I2S_WS,
        I2S_SCK,
        I2S_MCLK,
        I2S_SDIN,
        I2S_SDOUT,
#endif
        GPS_TX,
        GPS_RX,
        GPS_PPS,
        SCK,
        MISO,
        MOSI,
        DISP_CS,
        DISP_DC,
        DISP_BL,
        SDA,
        SCL,
        LORA_CS,
        LORA_RST,
        LORA_BUSY,
        LORA_IRQ,
    };

    for (auto pin : pins)
    {
        if (pin == BOOT_KEY)
        {
            // Keep BOOT wake pin as input for EXT1 wakeup (LilyGo uses GPIO0)
            continue;
        }
        log_d("Set pin %d to open drain", pin);
        gpio_reset_pin((gpio_num_t)pin);
        pinMode(pin, OPEN_DRAIN);
    }

    Serial.flush();
    delay(200);
    Serial.end();
    delay(1000);

    // 13) Configure fallback wakeup source (LilyGo: BOOT button on GPIO0 only)
    pinMode(BOOT_KEY, INPUT_PULLUP); // ensure stable HIGH when not pressed
    uint64_t wakeup_pin = (1ULL << BOOT_KEY);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_sleep_enable_ext1_wakeup_io(wakeup_pin, ESP_EXT1_WAKEUP_ANY_LOW);
#else
    esp_sleep_enable_ext1_wakeup(wakeup_pin, ESP_EXT1_WAKEUP_ANY_LOW);
#endif

    // 14) Enter deep sleep
    esp_deep_sleep_start();

    // Code will not reach here
    while (true)
    {
        delay(1000);
    }
}

void TLoRaPagerBoard::softwareShutdown()
{
    log_i("Shutdown requested - PMU Power OFF only; no deep-sleep fallback");
    shutdownImpl(true, ShutdownMode::PowerOff);
}

void TLoRaPagerBoard::wakeUp()
{
    // Re-initialize power button interrupt after wake up
    initPowerButton();
}

void TLoRaPagerBoard::enterScreenSleep()
{
    // LoRa stays on for mesh; GPS power is owned by GpsService/HalGps so
    // screen sleep does not desync the hardware rail from service state.
}

void TLoRaPagerBoard::exitScreenSleep()
{
    // Keep GPS power ownership in the GPS service path for consistent state.
}

void TLoRaPagerBoard::setPowerTier(int tier)
{
    if (tier < 0)
    {
        tier = 0;
    }
    if (tier > 2)
    {
        tier = 2;
    }
    power_tier_ = tier;
}

void TLoRaPagerBoard::rotaryTask(void* p)
{
    (void)p;
    RotaryMsg_t msg;
    bool last_btn_state = false;
    instance.rotary.begin();
    pinMode(ROTARY_C, INPUT);
    while (true)
    {
        msg.centerBtnPressed = getButtonState();
        uint8_t result = instance.rotary.process();
        if (result || msg.centerBtnPressed != last_btn_state)
        {
            switch (result)
            {
            case DIR_CW:
                msg.dir = ROTARY_DIR_UP;
                break;
            case DIR_CCW:
                msg.dir = ROTARY_DIR_DOWN;
                break;
            default:
                msg.dir = ROTARY_DIR_NONE;
                break;
            }
            last_btn_state = msg.centerBtnPressed;
            xQueueSend(rotaryMsg, (void*)&msg, portMAX_DELAY);
        }
        delay(2);
    }
}

// ------------------------------
// USB present detection (best effort)
// ------------------------------
bool TLoRaPagerBoard::isUsbPresent_bestEffort()
{
    TLoRaPagerBoard* board = TLoRaPagerBoard::getInstance();
    if (board && board->isPMUReady())
    {
        I2CGuard i2c;
        return board->pmu.isVbusIn();
    }
    return false;
}

// ------------------------------
// Step 1: turn off high-power rails/peripherals
// Note: active-high vs active-low depends on how EN pins are wired.
// Based on typical designs, EN=1 means ON, so we set to 0 to OFF.
// ------------------------------
namespace
{
TLoRaPagerBoard& getInstanceRef()
{
    return *TLoRaPagerBoard::getInstance();
}
} // namespace

TLoRaPagerBoard& instance = getInstanceRef();
BoardBase& board = getInstanceRef();

} // namespace boards::tlora_pager

BoardBase& board = ::boards::tlora_pager::instance;
#endif // defined(ARDUINO_T_LORA_PAGER)
