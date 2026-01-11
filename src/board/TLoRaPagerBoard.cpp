#include "board/TLoRaPagerBoard.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "display/drivers/ST7796.h"
#include "pins_arduino.h"

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
    {' ',/*Space*/ '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0'}
};
// 4x10 symbol map
static constexpr char symbol_map[4][10] = {
    {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'},
    {'*', '/', '+', '-', '=', ':', '\'', '"', '@', '\0'},
    {'\0', '_', '$', ';', '?', '!', ',', '.', '\0', '\0'},
    {' '/*Space*/, '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0'}
};

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
    .has_symbol_key = false
};
#endif

#ifdef USING_ST25R3916
static RfalRfST25R3916Class nfc_hw(&SPI, NFC_CS, NFC_INT);
static RfalNfcClass NFCReader(&nfc_hw);
#endif

static QueueHandle_t rotaryMsg;
static TaskHandle_t rotaryHandler;
static EventGroupHandle_t rotaryTaskFlag;

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
    const uint8_t debounceDelay = 20;  // Debounce delay in milliseconds
    
    int reading = digitalRead(ROTARY_C);

    // Check if button press flag is set (prevents multiple triggers)
    EventBits_t eventBits = xEventGroupGetBits(rotaryTaskFlag);
    if (eventBits & TASK_ROTARY_START_PRESSED_FLAG) {
        if (reading == HIGH) {
            // Button released, clear the flag
            xEventGroupClearBits(rotaryTaskFlag, TASK_ROTARY_START_PRESSED_FLAG);
        } else {
            // Button still pressed, don't trigger again
            return false;
        }
    }

    // Debouncing logic
    if (reading != lastButtonState) {
        // State changed, reset debounce timer
        lastDebounceTime = millis();
    }
    
    if (millis() - lastDebounceTime > debounceDelay) {
        // Debounce period elapsed, update state if changed
        if (reading != buttonState) {
            buttonState = reading;
            if (buttonState == LOW) {
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
    , nfc(&NFCReader)
#endif
{
    devices_probe = 0;
}

TLoRaPagerBoard::~TLoRaPagerBoard()
{
}

TLoRaPagerBoard *TLoRaPagerBoard::getInstance()
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
    for (auto pin : share_spi_bus_devices_cs_pins) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }
}

uint32_t TLoRaPagerBoard::begin(uint32_t disable_hw_init)
{
    static bool initialized = false;
    if (initialized) {
        return devices_probe;
    }
    initialized = true;

    bool res = false;

    devices_probe = 0x00;

    while (!psramFound()) {
        log_d("ERROR:PSRAM NOT FOUND!"); 
        delay(1000);
    }

    devices_probe |= HW_PSRAM_ONLINE;

    Wire.begin(SDA, SCL);

    // Initialize battery gauge (BQ27220)
    if (!gauge.begin(Wire, SDA, SCL)) {
        log_w("Battery gauge (BQ27220) not found");
    } else {
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
    if (!res) {
        log_w("PMU (BQ25896) not found");
    } else {
        log_d("PMU initialized successfully");
        devices_probe |= HW_PMU_ONLINE;
    }

    // Initialize GPIO expander (XL9555) - controls power for various peripherals
#ifdef USING_XL9555_EXPANDS
    if (io.begin(Wire, 0x20)) {
        log_d("GPIO expander (XL9555) initialized successfully");
        devices_probe |= HW_EXPAND_ONLINE;
        
        // Configure GPIO expander pins as outputs and set them HIGH (enable peripherals)
        const uint8_t expand_pins[] = {
            EXPANDS_KB_RST,      // Keyboard reset
            EXPANDS_LORA_EN,     // LoRa enable
            EXPANDS_GPS_EN,      // GPS enable
            EXPANDS_DRV_EN,      // Haptic driver enable
            EXPANDS_AMP_EN,      // Audio amplifier enable
            EXPANDS_NFC_EN,      // NFC enable
#ifdef EXPANDS_GPS_RST
            EXPANDS_GPS_RST,     // GPS reset
#endif
#ifdef EXPANDS_KB_EN
            EXPANDS_KB_EN,        // Keyboard enable
#endif
#ifdef EXPANDS_GPIO_EN
            EXPANDS_GPIO_EN,      // GPIO enable
#endif
#ifdef EXPANDS_SD_EN
            EXPANDS_SD_EN,        // SD card enable
#endif
        };
        
        for (auto pin : expand_pins) {
            io.pinMode(pin, OUTPUT);
            io.digitalWrite(pin, HIGH);  // Enable peripheral power
            delay(1);  // Small delay for power stabilization
        }
        
        // SD card pull-up enable (input pin)
        io.pinMode(EXPANDS_SD_PULLEN, INPUT);
    } else {
        log_w("GPIO expander (XL9555) initialization failed");
    }
#endif

    // Initialize sensor (BHI260AP) - optional, can be disabled
    if (!(disable_hw_init & NO_HW_SENSOR)) {
        if (initSensor()) {
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
    pinMode(NFC_INT, INPUT);

    // Initialize RTC (PCF85063) - optional
    if (!(disable_hw_init & NO_HW_RTC)) {
        if (initRTC()) {
            log_d("RTC (PCF85063) initialized successfully");
        }
    }

    // Initialize NFC (ST25R3916) - optional
    if (!(disable_hw_init & NO_HW_NFC)) {
        if (initNFC()) {
            log_d("NFC (ST25R3916) initialized successfully");
        }
    }

    // Initialize keyboard (TCA8418) - optional
    if (!(disable_hw_init & NO_HW_KEYBOARD)) {
        if (initKeyboard()) {
            log_d("Keyboard (TCA8418) initialized successfully");
        }
    }

    // Initialize haptic driver (DRV2605) - optional
    if (!(disable_hw_init & NO_HW_DRV)) {
        if (initDrv()) {
            log_d("Haptic driver (DRV2605) initialized successfully");
        }
    }

    // Initialize GPS (UBlox MIA-M10Q) - optional
    if (!(disable_hw_init & NO_HW_GPS)) {
        if (initGPS()) {
            log_d("GPS (UBlox MIA-M10Q) initialized successfully");
        }
    }

    // Initialize LoRa radio - optional
    if (!(disable_hw_init & NO_HW_LORA)) {
        if (initLoRa()) {
            log_d("LoRa radio initialized successfully");
        }
    }

    // Initialize SD card - optional, with retry
    if (!(disable_hw_init & NO_HW_SD)) {
        const int max_retries = 2;
        for (int retry = 0; retry < max_retries; retry++) {
            if (installSD()) {
                log_d("SD card initialized successfully");
                devices_probe |= HW_SD_ONLINE;
                break;
            } else if (retry < max_retries - 1) {
                log_w("SD card initialization failed, retrying... (%d/%d)", retry + 1, max_retries);
                delay(100);  // Small delay before retry
            } else {
                log_w("SD card not found after %d attempts", max_retries);
            }
        }
    }

    // Initialize audio codec (ES8311) - optional
#ifdef USING_AUDIO_CODEC
    if (!(disable_hw_init & NO_HW_CODEC)) {
        codec.setPins(I2S_MCLK, I2S_SCK, I2S_WS, I2S_SDOUT, I2S_SDIN);
        if (codec.begin(Wire, 0x18, CODEC_TYPE_ES8311)) {
            devices_probe |= HW_CODEC_ONLINE;
            log_d("Audio codec (ES8311) initialized successfully");
            
            // Set power amplifier control callback
            codec.setPaPinCallback([](bool enable, void *user_data) {
                ((ExtensionIOXL9555 *)user_data)->digitalWrite(EXPANDS_AMP_EN, enable);
            }, &io);
        } else {
            log_w("Audio codec (ES8311) not found");
        }
    }
#endif

    // Create rotary encoder message queue and task
    rotaryMsg = xQueueCreate(5, sizeof(RotaryMsg_t));
    if (rotaryMsg == nullptr) {
        log_e("Failed to create rotary encoder message queue");
    }
    
    rotaryTaskFlag = xEventGroupCreate();
    if (rotaryTaskFlag == nullptr) {
        log_e("Failed to create rotary encoder event group");
    }
    
    BaseType_t task_result = xTaskCreate(rotaryTask, "rotary", 2 * 1024, NULL, 10, &rotaryHandler);
    if (task_result != pdPASS) {
        log_e("Failed to create rotary encoder task");
    } else {
        log_d("Rotary encoder task created successfully");
    }

    log_d("Board initialization complete. Hardware online: 0x%08X", devices_probe);
    return devices_probe;
}

void TLoRaPagerBoard::loop()
{
    // Process NFC worker if NFC is online
#ifdef USING_ST25R3916
    if (devices_probe & HW_NFC_ONLINE) {
        if (LilyGoDispArduinoSPI::lock(0)) {  // Try to lock, don't wait
            NFCReader.rfalNfcWorker();
            LilyGoDispArduinoSPI::unlock();
        }
    }
#endif
}

bool TLoRaPagerBoard::initPMU()
{
    bool res = pmu.init(Wire, SDA, SCL);
    if (!res) {
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
    if (!res) {
        log_e("Failed to find BHI260AP");
    } else {
        log_d("Initializing BHI260AP succeeded");
        devices_probe |= HW_BHI260AP_ONLINE;
        sensor.setRemapAxes(SensorBHI260AP::BOTTOM_LAYER_TOP_LEFT_CORNER);
        pinMode(SENSOR_INT, INPUT);
        // Note: Interrupt handling would require event group setup
    }
    Wire.setClock(400000UL);
    return res;
}

bool TLoRaPagerBoard::initRTC()
{
    bool res = false;
    log_d("Init PCF85063 RTC");
    res = rtc.begin(Wire);
    if (!res) {
        log_e("Failed to find PCF85063");
    } else {
        devices_probe |= HW_RTC_ONLINE;
        log_d("Initializing PCF85063 succeeded");
        rtc.hwClockRead();  // Synchronize RTC clock to system clock
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
    res = drv.begin(Wire);
    if (!res) {
        log_e("Failed to find DRV2605");
    } else {
        log_d("Initializing DRV2605 succeeded");
        drv.selectLibrary(1);
        drv.setMode(SensorDRV2605::MODE_INTTRIG);
        drv.useERM();
        // set the effect to play
        drv.setWaveform(0, 15);  // play effect
        drv.setWaveform(1, 0);   // end waveform
        drv.run();
        devices_probe |= HW_DRV_ONLINE;
    }
    return res;
}

bool TLoRaPagerBoard::initNFC()
{
#ifdef USING_ST25R3916
    bool res = false;
    log_d("Init NFC");
    
    // Enable NFC power before initialization
    powerControl(POWER_NFC, true);
    delay(10);  // Wait for power to stabilize
    
    // Initialize NFC reader
    res = NFCReader.rfalNfcInitialize() == ST_ERR_NONE;
    if (!res) {
        log_e("Failed to find NFC Reader");
        powerControl(POWER_NFC, false);
    } else {
        log_d("Initializing NFC Reader succeeded");
        devices_probe |= HW_NFC_ONLINE;
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
    if (!res) {
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
    radio.reset();

    int state = radio.begin();

    if (state != RADIOLIB_ERR_NONE) {
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
    if (devices_probe & HW_EXPAND_ONLINE) {
        io.pinMode(EXPANDS_SD_DET, INPUT);
        if (io.digitalRead(EXPANDS_SD_DET)) {
            log_d("SD card detection pin indicates no card present");
            return false;
        }
    }
#endif

    // Ensure SPI pins are initialized
    initShareSPIPins();
    
    // Initialize SD card with 4MHz SPI speed, mount point: /sd
    if (!SD.begin(SD_CS, SPI, 4000000U, "/sd")) {
        log_w("SD card initialization failed");
        return false;
    }
    
    // Verify card is actually present
    if (SD.cardType() != CARD_NONE) {
        uint64_t cardSizeMB = SD.cardSize() / (1024 * 1024);
        log_d("SD card detected, size: %llu MB", cardSizeMB);
        return true;
    }
    
    log_w("SD card type is NONE");
    return false;
}

void TLoRaPagerBoard::uninstallSD()
{
    // Safely unmount SD card (requires SPI lock)
    if (LilyGoDispArduinoSPI::lock(portMAX_DELAY)) {
        SD.end();
        LilyGoDispArduinoSPI::unlock();
        log_d("SD card unmounted");
    } else {
        log_w("Failed to acquire SPI lock for SD card unmount");
    }
}

bool TLoRaPagerBoard::isCardReady()
{
    // Check if SD card is ready (requires SPI lock)
    bool ready = false;
    if (LilyGoDispArduinoSPI::lock(pdTICKS_TO_MS(100))) {
        ready = (SD.sectorSize() != 0);
        LilyGoDispArduinoSPI::unlock();
    }
    return ready;
}

void TLoRaPagerBoard::powerControl(PowerCtrlChannel_t ch, bool enable)
{
    switch (ch) {
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
    if (devices_probe & HW_DRV_ONLINE) {
        drv.setWaveform(0, _haptic_effects);
        drv.setWaveform(1, 0);
        drv.run();
    }
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

int TLoRaPagerBoard::getKey(char *c)
{
#ifdef USING_INPUT_DEV_KEYBOARD
    if (devices_probe & HW_KEYBOARD_ONLINE) {
        return kb.getKey(c);
    }
#endif
    return -1;
}

int TLoRaPagerBoard::getKeyChar(char *c)
{
    return getKey(c);
}

#ifdef USING_ST25R3916
bool TLoRaPagerBoard::startNFCDiscovery(uint8_t techs2Find, uint16_t totalDuration)
{
    if (!(devices_probe & HW_NFC_ONLINE)) {
        log_e("NFC not initialized");
        return false;
    }

    // Enable NFC power
    powerControl(POWER_NFC, true);
    delay(10);  // Wait for power to stabilize

    // Reinitialize NFC reader
    if (NFCReader.rfalNfcInitialize() != ST_ERR_NONE) {
        log_e("Failed to reinitialize NFC");
        powerControl(POWER_NFC, false);
        return false;
    }

    // Setup discovery parameters
    rfalNfcDiscoverParam discover_params;
    discover_params.devLimit = 1;
    discover_params.techs2Find = techs2Find;
    discover_params.GBLen = RFAL_NFCDEP_GB_MAX_LEN;
    discover_params.notifyCb = nullptr;  // Can be set by user if needed
    discover_params.totalDuration = totalDuration;
    discover_params.wakeupEnabled = false;

    // Start discovery
    if (NFCReader.rfalNfcDiscover(&discover_params) != ST_ERR_NONE) {
        log_e("Failed to start NFC discovery");
        powerControl(POWER_NFC, false);
        return false;
    }

    log_d("NFC discovery started");
    return true;
}

void TLoRaPagerBoard::stopNFCDiscovery()
{
    if (!(devices_probe & HW_NFC_ONLINE)) {
        return;
    }

    // Deactivate NFC
    NFCReader.rfalNfcDeactivate(true);
    
    // Turn off NFC power
    powerControl(POWER_NFC, false);
    
    log_d("NFC discovery stopped");
}
#endif

bool TLoRaPagerBoard::initGPS()
{
    Serial.printf("[TLoRaPagerBoard::initGPS] Starting GPS initialization...\n");
    Serial.printf("[TLoRaPagerBoard::initGPS] Opening Serial1: baud=38400, RX=%d, TX=%d\n", GPS_RX, GPS_TX);
    Serial1.begin(38400, SERIAL_8N1, GPS_RX, GPS_TX);
    delay(100);  // Give Serial1 time to initialize
    Serial.printf("[TLoRaPagerBoard::initGPS] Serial1 opened, calling gps.init(&Serial1)...\n");
    bool result = gps.init(&Serial1);
    Serial.printf("[TLoRaPagerBoard::initGPS] gps.init() returned: %d\n", result);
    if (result) {
        Serial.printf("[TLoRaPagerBoard::initGPS] GPS initialized successfully, model: %s\n", gps.getModel().c_str());
        devices_probe |= HW_GPS_ONLINE;
        Serial.printf("[TLoRaPagerBoard::initGPS] Set HW_GPS_ONLINE flag, devices_probe=0x%08X\n", devices_probe);
    } else {
        Serial.printf("[TLoRaPagerBoard::initGPS] GPS initialization FAILED\n");
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

void TLoRaPagerBoard::pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *color)
{
    LilyGoDispArduinoSPI::pushColors(x1, y1, x2, y2, color);
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

RotaryMsg_t TLoRaPagerBoard::getRotary()
{
    static RotaryMsg_t msg;
    if (xQueueReceive(rotaryMsg, &msg, pdMS_TO_TICKS(50)) == pdPASS) {
        return msg;
    }
    msg.centerBtnPressed = false;
    msg.dir = ROTARY_DIR_NONE;
    return msg;
}

void TLoRaPagerBoard::feedback(void *args)
{
    (void)args;
}

void TLoRaPagerBoard::rotaryTask(void *p)
{
    (void)p;
    RotaryMsg_t msg;
    bool last_btn_state = false;
    instance.rotary.begin();
    pinMode(ROTARY_C, INPUT);
    while (true) {
        msg.centerBtnPressed = getButtonState();
        uint8_t result = instance.rotary.process();
        if (result || msg.centerBtnPressed != last_btn_state) {
            switch (result) {
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
            xQueueSend(rotaryMsg, (void *)&msg, portMAX_DELAY);
        }
        delay(2);
    }
}

namespace {
TLoRaPagerBoard &getInstanceRef()
{
    return *TLoRaPagerBoard::getInstance();
}
}

TLoRaPagerBoard &instance = getInstanceRef();
