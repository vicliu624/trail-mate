#include "board/TLoRaPagerBoard.h"
#include "board/sd_utils.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include <cstring>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include "display/drivers/ST7796.h"
#include "pins_arduino.h"
#include "ui/widgets/system_notification.h"
#include <Preferences.h>

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

// Forward declarations for power management wrapper functions
Preferences& getPreferencesInstance();

#ifdef USING_ST25R3916
#include <rfal_rfst25r3916.h>
#endif

#ifdef USING_XL9555_EXPANDS
#define BOSCH_BHI260_KLIO
#else
#define BOSCH_BHI260_GPIO
#endif
#include <BoschFirmware.h>

#define TASK_ROTARY_START_PRESSED_FLAG _BV(0)

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

#ifdef USING_ST25R3916
static RfalRfST25R3916Class nfc_hw(&SPI, NFC_CS, NFC_INT);
static RfalNfcClass NFCReader(&nfc_hw);
#endif

static QueueHandle_t rotaryMsg;
static TaskHandle_t rotaryHandler;
static EventGroupHandle_t rotaryTaskFlag;
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
 * This function implements debouncing logic to filter out mechanical switch bounce.
 * It also handles the TASK_ROTARY_START_PRESSED_FLAG to prevent multiple triggers.
 */
static bool getButtonState()
{
    static uint8_t buttonState = HIGH;
    static uint8_t lastButtonState = HIGH;
    static uint32_t lastDebounceTime = 0;
    const uint8_t debounceDelay = 20; // Debounce delay in milliseconds

    int reading = digitalRead(ROTARY_C);

    // Check if button press flag is set (prevents multiple triggers)
    EventBits_t eventBits = xEventGroupGetBits(rotaryTaskFlag);
    if (eventBits & TASK_ROTARY_START_PRESSED_FLAG)
    {
        if (reading == HIGH)
        {
            // Button released, clear the flag
            xEventGroupClearBits(rotaryTaskFlag, TASK_ROTARY_START_PRESSED_FLAG);
        }
        else
        {
            // Button still pressed, don't trigger again
            return false;
        }
    }

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
            if (buttonState == LOW)
            {
                // Button pressed (LOW = pressed due to pull-up)
                lastButtonState = reading;
                return true;
            }
        }
    }

    lastButtonState = reading;
    return false;
}

TLoRaPagerBoard::TLoRaPagerBoard()
    : LilyGo_Display(SPI_DRIVER, false),
      LilyGoDispArduinoSPI(DISP_WIDTH, DISP_HEIGHT,
                           display::drivers::ST7796::getInitCommands(),
                           display::drivers::ST7796::getInitCommandsCount(),
                           // T-LoRa-Pager specific offsets:
                           // - Landscape orientations (90°, 270°): landscape_offset_x = 49
                           // - Portrait orientations (0°, 180°): portrait_offset_y = 49
                           display::drivers::ST7796::getRotationConfig(DISP_WIDTH, DISP_HEIGHT, 49, 49))
#ifdef USING_ST25R3916
      ,
      nfc(&NFCReader)
#endif
{
    devices_probe = 0;
}

TLoRaPagerBoard::~TLoRaPagerBoard()
{
}

TLoRaPagerBoard* TLoRaPagerBoard::getInstance()
{
    static TLoRaPagerBoard instance;
    return &instance;
}

void TLoRaPagerBoard::initShareSPIPins()
{
    const uint8_t share_spi_bus_devices_cs_pins[] = {
        NFC_CS,
        LORA_CS,
        SD_CS,
        LORA_RST,
    };
    for (auto pin : share_spi_bus_devices_cs_pins)
    {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }
}

uint32_t TLoRaPagerBoard::begin(uint32_t disable_hw_init)
{
    Serial.printf("[TLoRaPagerBoard::begin] ===== HARDWARE INITIALIZATION START =====\n");
    Serial.printf("[TLoRaPagerBoard::begin] disable_hw_init=0x%08X\n", disable_hw_init);
    Serial.printf("[TLoRaPagerBoard::begin] NO_HW_GPS flag: %s\n", (disable_hw_init & NO_HW_GPS) ? "SET (GPS will be SKIPPED)" : "NOT SET (GPS will be initialized)");

    static bool initialized = false;
    if (initialized)
    {
        Serial.printf("[TLoRaPagerBoard::begin] Already initialized, returning devices_probe=0x%08X\n", devices_probe);
        return devices_probe;
    }
    initialized = true;

    bool res = false;

    devices_probe = 0x00;

    while (!psramFound())
    {
        log_d("ERROR:PSRAM NOT FOUND!");
        delay(1000);
    }

    devices_probe |= HW_PSRAM_ONLINE;

    Wire.begin(SDA, SCL);

    // Initialize battery gauge (BQ27220)
    if (!gauge.begin(Wire, SDA, SCL))
    {
        log_w("Battery gauge (BQ27220) not found");
    }
    else
    {
        log_d("Battery gauge initialized successfully");
        devices_probe |= HW_GAUGE_ONLINE;
        // Configure battery capacity (1500mAh for T-LoRa-Pager)
        const uint16_t designCapacity = 1500;
        const uint16_t fullChargeCapacity = 1500;
        gauge.setNewCapacity(designCapacity, fullChargeCapacity);
        log_d("Battery capacity set to %dmAh", designCapacity);
    }

    // Initialize PMU (BQ25896 power management)
    res = initPMU();
    if (!res)
    {
        log_w("PMU (BQ25896) not found");
    }
    else
    {
        log_d("PMU initialized successfully");
        devices_probe |= HW_PMU_ONLINE;
    }

    // Initialize GPIO expander (XL9555) - controls power for various peripherals
#ifdef USING_XL9555_EXPANDS
    if (io.begin(Wire, 0x20))
    {
        log_d("GPIO expander (XL9555) initialized successfully");
        devices_probe |= HW_EXPAND_ONLINE;

        // Configure GPIO expander pins as outputs and set them HIGH (enable peripherals)
        const uint8_t expand_pins[] = {
            EXPANDS_KB_RST,  // Keyboard reset
            EXPANDS_LORA_EN, // LoRa enable
            EXPANDS_GPS_EN,  // GPS enable
            EXPANDS_DRV_EN,  // Haptic driver enable
            EXPANDS_AMP_EN,  // Audio amplifier enable
            EXPANDS_NFC_EN,  // NFC enable
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

        for (auto pin : expand_pins)
        {
            io.pinMode(pin, OUTPUT);
            io.digitalWrite(pin, HIGH); // Enable peripheral power
            delay(1);                   // Small delay for power stabilization
        }

        // SD card pull-up enable (input pin)
        io.pinMode(EXPANDS_SD_PULLEN, INPUT);
    }
    else
    {
        log_w("GPIO expander (XL9555) initialization failed");
    }
#endif

    // Initialize sensor (BHI260AP) - optional, can be disabled
    if (!(disable_hw_init & NO_HW_SENSOR))
    {
        if (initSensor())
        {
            log_d("Sensor (BHI260AP) initialized successfully");
        }
    }

    // Initialize backlight driver (AW9364)
    backlight.begin(DISP_BL);
    log_d("Backlight driver initialized (pin %d)", DISP_BL);

    // Initialize shared SPI pins (CS pins for LoRa, NFC, SD)
    initShareSPIPins();

    // Initialize display (ST7796)
    LilyGoDispArduinoSPI::init(DISP_SCK, DISP_MISO, DISP_MOSI, DISP_CS, DISP_RST, DISP_DC, -1);
    log_d("Display (ST7796) initialized: %dx%d", DISP_WIDTH, DISP_HEIGHT);

    // Initialize SPI bus for LoRa/SD/NFC (shared SPI bus)
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI);
    log_d("SPI bus initialized (SCK=%d, MISO=%d, MOSI=%d)", LORA_SCK, LORA_MISO, LORA_MOSI);

    // Configure NFC interrupt pin
    pinMode(NFC_INT, INPUT_PULLUP);

    // Initialize RTC (PCF85063) - optional
    if (!(disable_hw_init & NO_HW_RTC))
    {
        if (initRTC())
        {
            log_d("RTC (PCF85063) initialized successfully");
        }
    }

    // Initialize NFC (ST25R3916) - optional
    if (!(disable_hw_init & NO_HW_NFC))
    {
        if (initNFC())
        {
            log_d("NFC (ST25R3916) initialized successfully");
        }
    }

    // Initialize keyboard (TCA8418) - optional
    if (!(disable_hw_init & NO_HW_KEYBOARD))
    {
        if (initKeyboard())
        {
            log_d("Keyboard (TCA8418) initialized successfully");
        }
    }

    // Initialize haptic driver (DRV2605) - optional
    if (!(disable_hw_init & NO_HW_DRV))
    {
        if (initDrv())
        {
            log_d("Haptic driver (DRV2605) initialized successfully");
        }
    }

    // GPS service is initialized by AppContext after configuration is loaded

    // Initialize LoRa radio_ - optional
    if (!(disable_hw_init & NO_HW_LORA))
    {
        if (initLoRa())
        {
            log_d("LoRa radio_ initialized successfully");
        }
    }

    // Initialize SD card - optional, with retry
    if (!(disable_hw_init & NO_HW_SD))
    {
        const int max_retries = 2;
        for (int retry = 0; retry < max_retries; retry++)
        {
            if (installSD())
            {
                log_d("SD card initialized successfully");
                devices_probe |= HW_SD_ONLINE;
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
            }
        }
    }

    // Initialize audio codec (ES8311) - optional
#ifdef USING_AUDIO_CODEC
    if (!(disable_hw_init & NO_HW_CODEC))
    {
        codec.setPins(I2S_MCLK, I2S_SCK, I2S_WS, I2S_SDOUT, I2S_SDIN);
        if (codec.begin(Wire, 0x18, CODEC_TYPE_ES8311))
        {
            devices_probe |= HW_CODEC_ONLINE;
            log_d("Audio codec (ES8311) initialized successfully");

            // Set power amplifier control callback
            codec.setPaPinCallback([](bool enable, void* user_data)
                                   { ((ExtensionIOXL9555*)user_data)->digitalWrite(EXPANDS_AMP_EN, enable); },
                                   &io);
        }
        else
        {
            log_w("Audio codec (ES8311) not found");
        }
    }
#endif

    // Create rotary encoder message queue and task
    rotaryMsg = xQueueCreate(5, sizeof(RotaryMsg_t));
    if (rotaryMsg == nullptr)
    {
        log_e("Failed to create rotary encoder message queue");
    }

    rotaryTaskFlag = xEventGroupCreate();
    if (rotaryTaskFlag == nullptr)
    {
        log_e("Failed to create rotary encoder event group");
    }

    BaseType_t task_result = xTaskCreate(rotaryTask, "rotary", 2 * 1024, NULL, 10, &rotaryHandler);
    if (task_result != pdPASS)
    {
        log_e("Failed to create rotary encoder task");
    }
    else
    {
        log_d("Rotary encoder task created successfully");
    }

    // Initialize power button handling
    if (!initPowerButton())
    {
        log_w("Power button initialization failed");
    }
    else
    {
        log_d("Power button initialized successfully");
    }

    log_d("Board initialization complete. Hardware online: 0x%08X", devices_probe);
    Serial.printf("[TLoRaPagerBoard::begin] ===== HARDWARE INITIALIZATION COMPLETE =====\n");
    Serial.printf("[TLoRaPagerBoard::begin] devices_probe=0x%08X\n", devices_probe);
    const char* gps_state =
        (devices_probe & HW_GPS_ONLINE) ? "YES" : ((disable_hw_init & NO_HW_GPS) ? "SKIPPED" : "DEFERRED");
    Serial.printf("[TLoRaPagerBoard::begin] GPS online: %s\n", gps_state);
    Serial.printf("[TLoRaPagerBoard::begin] NFC online: %s (HW_NFC_ONLINE=0x%08X)\n",
                  (devices_probe & HW_NFC_ONLINE) ? "YES" : "NO", HW_NFC_ONLINE);

    return devices_probe;
}

void TLoRaPagerBoard::loop()
{
    // Process NFC worker if NFC is online
#ifdef USING_ST25R3916
    if (devices_probe & HW_NFC_ONLINE)
    {
        if (LilyGoDispArduinoSPI::lock(0))
        { // Try to lock, don't wait
            NFCReader.rfalNfcWorker();
            LilyGoDispArduinoSPI::unlock();
        }
    }
#endif
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
        // 不在上电时震动，效果在需要时?vibrator() 触发
        drv.setWaveform(0, 0);
        drv.setWaveform(1, 0);
        powerControl(POWER_HAPTIC_DRIVER, false);
        devices_probe |= HW_DRV_ONLINE;
    }
    return res;
}

bool TLoRaPagerBoard::initNFC()
{
#ifdef USING_ST25R3916
    bool res = false;
    ReturnCode rc = ERR_NONE;
    log_d("Init NFC");

    // Enable NFC power before initialization
    powerControl(POWER_NFC, true);
    delay(10); // Wait for power to stabilize

    // Initialize NFC reader
    rc = NFCReader.rfalNfcInitialize();
    res = (rc == ERR_NONE);
    if (!res)
    {
        log_e("Failed to find NFC Reader (rc=%d)", rc);
        Serial.printf("[TLoRaPagerBoard::initNFC] NFC init failed rc=%d\n", rc);
        powerControl(POWER_NFC, false);
    }
    else
    {
        log_d("Initializing NFC Reader succeeded");
        Serial.printf("[TLoRaPagerBoard::initNFC] NFC init ok\n");
        devices_probe |= HW_NFC_ONLINE;
        detachInterrupt(NFC_INT);
        // Turn off NFC power after initialization (will be enabled when needed)
        powerControl(POWER_NFC, false);
    }
    return res;
#else
    return false;
#endif
}

bool TLoRaPagerBoard::initKeyboard()
{
#ifdef USING_INPUT_DEV_KEYBOARD
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
    radio_.reset();

    int state = radio_.begin();

    if (state != RADIOLIB_ERR_NONE)
    {
        devices_probe &= ~HW_RADIO_ONLINE;
        log_e("❌Radio init failed, code :%d", state);
        return false;
    }

    devices_probe |= HW_RADIO_ONLINE;
    log_i("✅Radio init succeeded");
    return true;
}

bool TLoRaPagerBoard::installSD()
{
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

    uint8_t card_type = CARD_NONE;
    uint32_t card_size_mb = 0;
    bool ok = sdutil::installSpiSd(*this, SD_CS, 4000000U, "/sd",
                                   nullptr, 0, &card_type, &card_size_mb);
    if (!ok)
    {
        log_w("SD card initialization failed");
        return false;
    }
    log_d("SD card detected, type=%u size=%lu MB",
          (unsigned)card_type, (unsigned long)card_size_mb);
    return true;
}

void TLoRaPagerBoard::uninstallSD()
{
    // Safely unmount SD card (requires SPI lock)
    if (LilyGoDispArduinoSPI::lock(portMAX_DELAY))
    {
        SD.end();
        LilyGoDispArduinoSPI::unlock();
        log_d("SD card unmounted");
    }
    else
    {
        log_w("Failed to acquire SPI lock for SD card unmount");
    }
}

bool TLoRaPagerBoard::isCardReady()
{
    // Check if SD card is ready (requires SPI lock)
    bool ready = false;
    if (LilyGoDispArduinoSPI::lock(pdTICKS_TO_MS(100)))
    {
        ready = (SD.sectorSize() != 0);
        LilyGoDispArduinoSPI::unlock();
    }
    return ready;
}

void TLoRaPagerBoard::powerControl(PowerCtrlChannel_t ch, bool enable)
{
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
    case POWER_NFC:
#ifdef USING_XL9555_EXPANDS
        io.digitalWrite(EXPANDS_NFC_EN, enable);
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
        return kb.getKey(c);
    }
#endif
    return -1;
}

int TLoRaPagerBoard::getKeyChar(char* c)
{
    return getKey(c);
}

#ifdef USING_ST25R3916
bool TLoRaPagerBoard::startNFCDiscovery(uint16_t techs2Find, uint16_t totalDuration)
{
    if (!(devices_probe & HW_NFC_ONLINE))
    {
        log_e("NFC not initialized");
        return false;
    }

    // Enable NFC power
    powerControl(POWER_NFC, true);
    delay(10); // Wait for power to stabilize

    // Reinitialize NFC reader
    ReturnCode rc = NFCReader.rfalNfcInitialize();
    if (rc != ERR_NONE)
    {
        log_e("Failed to reinitialize NFC");
        Serial.printf("[TLoRaPagerBoard::startNFCDiscovery] rfalNfcInitialize rc=%d\n", rc);
        powerControl(POWER_NFC, false);
        return false;
    }
    detachInterrupt(NFC_INT);

    // Setup discovery parameters
    rfalNfcDiscoverParam discover_params;
    rfalNfcDefaultDiscParams(&discover_params);
    discover_params.devLimit = 1;
    discover_params.techs2Find = techs2Find;
    discover_params.notifyCb = nullptr; // Can be set by user if needed
    discover_params.totalDuration = totalDuration;

    if (techs2Find & RFAL_NFC_LISTEN_TECH_A)
    {
        static bool nfcid_init = false;
        static uint8_t nfcid[RFAL_NFCID1_TRIPLE_LEN] = {0};
        if (!nfcid_init)
        {
            nfcid[0] = static_cast<uint8_t>(random(1, 255));
            nfcid[1] = static_cast<uint8_t>(random(0, 256));
            nfcid[2] = static_cast<uint8_t>(random(0, 256));
            nfcid[3] = static_cast<uint8_t>(random(0, 256));
            nfcid_init = true;
        }
        discover_params.lmConfigPA.nfcidLen = RFAL_LM_NFCID_LEN_04;
        memcpy(discover_params.lmConfigPA.nfcid, nfcid, sizeof(nfcid));
        discover_params.lmConfigPA.SENS_RES[0] = 0x04;
        discover_params.lmConfigPA.SENS_RES[1] = 0x00;
        discover_params.lmConfigPA.SEL_RES = RFAL_NFCA_SEL_RES_CONF_T4T;
    }

    // Start discovery
    rc = NFCReader.rfalNfcDiscover(&discover_params);
    if (rc != ERR_NONE)
    {
        log_e("Failed to start NFC discovery");
        Serial.printf("[TLoRaPagerBoard::startNFCDiscovery] rfalNfcDiscover rc=%d\n", rc);
        powerControl(POWER_NFC, false);
        return false;
    }

    log_d("NFC discovery started");
    return true;
}

void TLoRaPagerBoard::stopNFCDiscovery()
{
    if (!(devices_probe & HW_NFC_ONLINE))
    {
        return;
    }

    // Deactivate NFC
    NFCReader.rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);

    // Turn off NFC power
    powerControl(POWER_NFC, false);

    log_d("NFC discovery stopped");
}

void TLoRaPagerBoard::pollNfcIrq()
{
    if (!nfc)
    {
        return;
    }
    RfalRfClass* rf = nfc->getRfalRf();
    if (!rf)
    {
        return;
    }
    static_cast<RfalRfST25R3916Class*>(rf)->st25r3916CheckForReceivedInterrupts();
}
#endif

bool TLoRaPagerBoard::initGPS()
{
    GPS_BOARD_LOG("[TLoRaPagerBoard::initGPS] Starting GPS initialization...\n");
    GPS_BOARD_LOG("[TLoRaPagerBoard::initGPS] Opening Serial1: baud=38400, RX=%d, TX=%d\n", GPS_RX, GPS_TX);

    // Clear HW_GPS_ONLINE flag before attempting initialization
    // This ensures we don't have stale state if reinitializing
    devices_probe &= ~HW_GPS_ONLINE;

    Serial1.begin(38400, SERIAL_8N1, GPS_RX, GPS_TX);
    delay(100); // Give Serial1 time to initialize
    GPS_BOARD_LOG("[TLoRaPagerBoard::initGPS] Serial1 opened, calling gps.init(&Serial1)...\n");
    bool result = gps.init(&Serial1);
    GPS_BOARD_LOG("[TLoRaPagerBoard::initGPS] gps.init() returned: %d\n", result);
    if (result)
    {
        GPS_BOARD_LOG("[TLoRaPagerBoard::initGPS] GPS initialized successfully, model: %s\n", gps.getModel().c_str());
        devices_probe |= HW_GPS_ONLINE;
        GPS_BOARD_LOG("[TLoRaPagerBoard::initGPS] Set HW_GPS_ONLINE flag, devices_probe=0x%08X\n", devices_probe);
    }
    else
    {
        GPS_BOARD_LOG("[TLoRaPagerBoard::initGPS] GPS initialization FAILED\n");
        // Ensure flag is cleared on failure
        devices_probe &= ~HW_GPS_ONLINE;
    }
    return result;
}

void TLoRaPagerBoard::setBrightness(uint8_t level)
{
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

int TLoRaPagerBoard::transmitRadio(const uint8_t* data, size_t len)
{
    if (LilyGoDispArduinoSPI::lock(pdMS_TO_TICKS(50)))
    {
        int rc = radio_.transmit(data, len);
        LilyGoDispArduinoSPI::unlock();
        return rc;
    }
    return RADIOLIB_ERR_SPI_WRITE_FAILED;
}

int TLoRaPagerBoard::startRadioReceive()
{
    if (LilyGoDispArduinoSPI::lock(pdMS_TO_TICKS(50)))
    {
        int rc = radio_.startReceive();
        LilyGoDispArduinoSPI::unlock();
        return rc;
    }
    return RADIOLIB_ERR_SPI_WRITE_FAILED;
}

uint32_t TLoRaPagerBoard::getRadioIrqFlags()
{
    if (LilyGoDispArduinoSPI::lock(pdMS_TO_TICKS(20)))
    {
        uint32_t flags = radio_.getIrqFlags();
        LilyGoDispArduinoSPI::unlock();
        return flags;
    }
    return 0;
}

int TLoRaPagerBoard::getRadioPacketLength(bool update)
{
    if (LilyGoDispArduinoSPI::lock(pdMS_TO_TICKS(20)))
    {
        int len = static_cast<int>(radio_.getPacketLength(update));
        LilyGoDispArduinoSPI::unlock();
        return len;
    }
    return 0;
}

int TLoRaPagerBoard::readRadioData(uint8_t* buf, size_t len)
{
    if (LilyGoDispArduinoSPI::lock(pdMS_TO_TICKS(50)))
    {
        int rc = radio_.readData(buf, len);
        LilyGoDispArduinoSPI::unlock();
        return rc;
    }
    return RADIOLIB_ERR_SPI_WRITE_FAILED;
}

void TLoRaPagerBoard::clearRadioIrqFlags(uint32_t flags)
{
    if (LilyGoDispArduinoSPI::lock(pdMS_TO_TICKS(20)))
    {
        radio_.clearIrqFlags(flags);
        LilyGoDispArduinoSPI::unlock();
    }
}

void TLoRaPagerBoard::configureLoraRadio(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr_denom,
                                         int8_t tx_power, uint16_t preamble_len, uint8_t sync_word,
                                         uint8_t crc_len)
{
    if (LilyGoDispArduinoSPI::lock(pdMS_TO_TICKS(100)))
    {
        radio_.setFrequency(freq_mhz);
        radio_.setBandwidth(bw_khz);
        radio_.setSpreadingFactor(sf);
        radio_.setCodingRate(cr_denom);
        radio_.setOutputPower(tx_power);
        radio_.setPreambleLength(preamble_len);
        radio_.setSyncWord(sync_word);
        radio_.setCRC(crc_len);
        LilyGoDispArduinoSPI::unlock();
    }
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
    Serial.printf("[TLoRaPagerBoard::syncTimeFromGPS] Original GPS time: %04d-%02d-%02d %02d:%02d:%02d\n",
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
    Serial.printf("[TLoRaPagerBoard::syncTimeFromGPS] Time synced: %04d-%02d-%02d %02d:%02d:%02d (sat=%d, has_fix=%d, base_delay=%lums, task_comp=%lums, proc_delay=%lums, total_delay=%lums, op_time=%lums)\n",
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
    // Register 0x04: Seconds (BCD format)
    // Register 0x05: Minutes (BCD format)
    // Register 0x06: Hours (BCD format)

    uint8_t hour, minute, second = 0;

    // Use the same Wire instance that RTC uses
    // For minimum resource usage, start reading from minutes register (0x05) if not showing seconds
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
    if (!isGaugeReady())
    {
        return -1;
    }

    // Get battery state of charge (percentage) from BQ27220
    // Try library methods first, then fallback to direct I2C read

    int level = -1;

    // Attempt 1: Try common library methods (uncomment if library supports):
    // level = gauge.getSOC();
    // level = gauge.getPercentage();
    // level = gauge.stateOfCharge();
    // level = gauge.getStateOfCharge();
    // level = gauge.readSOC();

    // Attempt 2: Direct I2C read if library methods don't work
    // BQ27220 I2C address: 0x55 (7-bit address)
    // SOC (State of Charge) register: 0x2C (according to BQ27220 datasheet)
    // Note: BQ27220 uses 16-bit registers, so we need to read 2 bytes

    if (level < 0)
    {
        // Try direct I2C read
        // BQ27220 SOC register is at 0x2C (16-bit value, percentage 0-100)
        uint8_t i2c_addr = 0x55; // BQ27220 default I2C address (7-bit)

        Wire.beginTransmission(i2c_addr);
        Wire.write(0x2C); // SOC register (0x2C = StateOfCharge)
        uint8_t error = Wire.endTransmission();
        if (error != 0)
        {
            // I2C communication failed
            return -1;
        }

        // BQ27220 registers are 16-bit, read 2 bytes
        Wire.requestFrom(i2c_addr, (uint8_t)2);
        if (Wire.available() < 2)
        {
            return -1;
        }

        // Read 16-bit value (little-endian: LSB first, then MSB)
        uint8_t lsb = Wire.read();
        uint8_t msb = Wire.read();
        uint16_t soc_raw = (uint16_t)msb << 8 | lsb;

        // BQ27220 SOC register (0x2C) format:
        // Returns percentage directly (0-100) as 16-bit value
        // Value represents remaining capacity as percentage of full charge capacity

        if (soc_raw <= 100)
        {
            level = (int)soc_raw; // Already in percentage (0-100)
        }
        else if (soc_raw <= 1000)
        {
            level = (int)(soc_raw / 10); // Convert from 0.1% units (0-1000 -> 0-100)
        }
        else
        {
            // If value is very large, it might be capacity in mAh, not percentage
            // Try to convert: if it's around 1500 (battery capacity), it's not percentage
            return -1; // Invalid value for percentage
        }

        // Validate final value
        if (level < 0 || level > 100)
        {
            return -1;
        }
    }

    return level;
}

bool TLoRaPagerBoard::isCharging()
{
    if (!isPMUReady())
    {
        return false;
    }
    return false;
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
    if (xQueueReceive(rotaryMsg, &msg, pdMS_TO_TICKS(50)) == pdPASS)
    {
        return msg;
    }
    msg.centerBtnPressed = false;
    msg.dir = ROTARY_DIR_NONE;
    return msg;
}

void TLoRaPagerBoard::feedback(void* args)
{
    (void)args;
}

// Power button handling variables
static volatile bool power_button_event = false;
static volatile bool power_button_state = false; // true = pressed, false = released
static volatile uint32_t power_button_press_start = 0;
static const uint32_t POWER_BUTTON_LONG_PRESS_MS = 3000; // 3 seconds for shutdown
static const uint32_t POWER_BUTTON_DEBOUNCE_MS = 50;     // Debounce delay

/**
 * @brief Power button interrupt handler
 * Detects power button press and release events
 */
static void IRAM_ATTR powerButtonISR()
{
    static uint32_t last_interrupt_time = 0;
    uint32_t current_time = micros() / 1000; // Convert to milliseconds

    // Debounce: ignore interrupts too close together
    if (current_time - last_interrupt_time < POWER_BUTTON_DEBOUNCE_MS)
    {
        return;
    }
    last_interrupt_time = current_time;

    bool current_button_state = (digitalRead(POWER_KEY) == LOW); // Active low

    // Only trigger event on state change
    if (current_button_state != power_button_state)
    {
        power_button_state = current_button_state;
        power_button_event = true;

        if (current_button_state)
        {
            // Button pressed
            power_button_press_start = current_time;
        }
    }
}

bool TLoRaPagerBoard::initPowerButton()
{
    // Configure power button pin
    pinMode(POWER_KEY, INPUT_PULLUP);

    // Test initial pin state
    int initial_state = digitalRead(POWER_KEY);
    log_d("Power button GPIO %d initial state: %d (0=pressed, 1=released)", POWER_KEY, initial_state);

    // Attach interrupt for both rising and falling edges
    // Note: attachInterrupt returns void in Arduino, no error checking possible
    attachInterrupt(digitalPinToInterrupt(POWER_KEY), powerButtonISR, CHANGE);

    log_d("Power button interrupt attached to GPIO %d", POWER_KEY);
    return true;
}

void TLoRaPagerBoard::handlePowerButton()
{
    // 根据LilyGo文档：POWER键只负责从Power OFF状态唤醒，不负责关?    // "The power button is only valid when the device is turned off"

    if (power_button_event)
    {
        power_button_event = false; // Clear the event flag

        if (power_button_state)
        {
            // POWER键按?- 这是一个唤醒信?            log_d("POWER button pressed - wake up signal");

            // 检查是否从deep sleep唤醒
            esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
            if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0)
            {
                log_d("Waking up from deep sleep via POWER button");
                wakeUp();
            }
            else
            {
                // 设备已经在运行状?- POWER键按下可能用于其他功?                log_d("POWER button pressed while device is running");
                // 可以在这里添加屏幕开关或其他功能
            }
        }
        else
        {
            // POWER键释?            log_d("POWER button released");
        }
    }
}

void TLoRaPagerBoard::shutdown(bool save_data)
{
    log_i("=== LILYGO OFFICIAL SHUTDOWN SEQUENCE ===");

    (void)save_data;

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
        EXPANDS_NFC_EN,
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

    // 11) End communication buses
    Serial1.end();
    SPI.end();
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
        NFC_INT,
        SENSOR_INT,
        NFC_CS,
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
        LORA_IRQ
    };

    for (auto pin : pins)
    {
        if (pin == POWER_KEY)
        {
            // Keep boot/power wake pin as input for EXT1 wakeup (LilyGo uses GPIO0)
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

    // 13) Configure wakeup source (LilyGo: BOOT button on GPIO0 only)
    pinMode(POWER_KEY, INPUT_PULLUP); // ensure stable HIGH when not pressed
    uint64_t wakeup_pin = (1ULL << POWER_KEY);
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
    // Check USB connection; avoid shutting down while host power is present.
    if (isUsbPresent_bestEffort())
    {
        log_w("Cannot shutdown: USB is connected (PMIC will maintain power)");
        ui::SystemNotification::show("???? USB ?????");
        return;
    }

    log_i("Shutdown conditions met - entering Power OFF mode (26µA)");
    shutdown(true);
}

void TLoRaPagerBoard::wakeUp()
{
    // Re-initialize power button interrupt after wake up
    initPowerButton();
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
    // Try to detect USB by checking PMU status if available
    TLoRaPagerBoard* board = TLoRaPagerBoard::getInstance();
    if (board && board->isPMUReady())
    {
        // Check if PMU reports VBUS present
        // Note: XPowersLib may have methods to check this
        // For now, assume we can check via PMU status
        return false; // TODO: Implement proper USB detection
    }
    return false; // Default: assume no USB
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
