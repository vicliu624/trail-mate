#include "board/TDeckBoard.h"

namespace
{
// Minimal ST7789 init sequence with required delays.
static const CommandTable_t kSt7789Init[] = {
    {0x11, {0}, 0x80},      // Sleep out + delay
    {0x3A, {0x55}, 1},      // 16-bit color
    {0x21, {0}, 0},         // Display inversion on
    {0x29, {0}, 0x80},      // Display on + delay
};

static const DispRotationConfig_t kSt7789Rot[4] = {
    {static_cast<uint8_t>(0x00 | 0x08), SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0},
    {static_cast<uint8_t>(0x60 | 0x08), SCREEN_HEIGHT, SCREEN_WIDTH, 0, 0},
    {static_cast<uint8_t>(0xC0 | 0x08), SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0},
    {static_cast<uint8_t>(0xA0 | 0x08), SCREEN_HEIGHT, SCREEN_WIDTH, 0, 0},
};
} // namespace

TDeckBoard::TDeckBoard()
    : LilyGo_Display(SPI_DRIVER, false),
      disp_(SCREEN_WIDTH, SCREEN_HEIGHT, kSt7789Init, sizeof(kSt7789Init) / sizeof(kSt7789Init[0]),
            kSt7789Rot)
{
}

TDeckBoard* TDeckBoard::getInstance()
{
    static TDeckBoard instance;
    return &instance;
}

bool TDeckBoard::initGPS()
{
    // T-Deck examples wire GPS to UART on pins 43/44.
    Serial1.begin(38400, SERIAL_8N1, GPS_RX, GPS_TX);
    delay(50);

    bool ok = gps_.init(&Serial1);
    setGPSOnline(ok);
    Serial.printf("[TDeckBoard] GPS init: %s\n", ok ? "OK" : "FAIL");
    return ok;
}

uint32_t TDeckBoard::begin(uint32_t disable_hw_init)
{
    (void)disable_hw_init;

    // Early probe: try to bring up serial ASAP for boot diagnostics.
    Serial.begin(115200);
    delay(20);
    Serial.println("[TDeckBoard] begin: early probe start");

    // T-Deck requires the power enable pin to be asserted very early.
#ifdef BOARD_POWERON
    pinMode(BOARD_POWERON, OUTPUT);
    digitalWrite(BOARD_POWERON, HIGH);
    delay(50);
    Serial.println("[TDeckBoard] poweron=HIGH");
#endif

    // Follow LilyGo T-Deck examples: de-conflict shared SPI bus early.
#ifdef SD_CS
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
#endif
#ifdef LORA_CS
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
#endif
#ifdef DISP_CS
    pinMode(DISP_CS, OUTPUT);
    digitalWrite(DISP_CS, HIGH);
#endif
#ifdef MISO
    pinMode(MISO, INPUT_PULLUP);
#endif
    SPI.begin(SCK, MISO, MOSI);
    Serial.println("[TDeckBoard] SPI bus initialized");

    // Ensure backlight rail is enabled even before full display init.
#ifdef DISP_BL
    if (DISP_BL >= 0)
    {
        pinMode(DISP_BL, OUTPUT);
        // Blink backlight briefly as a visual heartbeat.
        digitalWrite(DISP_BL, LOW);
        delay(60);
        digitalWrite(DISP_BL, HIGH);
        delay(60);
        digitalWrite(DISP_BL, LOW);
        delay(60);
        digitalWrite(DISP_BL, HIGH);
        Serial.println("[TDeckBoard] backlight blinked");
    }
#endif

    // Initialize display (ST7789) before LVGL starts flushing.
#if defined(DISP_SCK) && defined(DISP_MISO) && defined(DISP_MOSI) && defined(DISP_CS) && defined(DISP_DC)
    disp_.init(DISP_SCK, DISP_MISO, DISP_MOSI, DISP_CS, DISP_RST, DISP_DC, DISP_BL, 40, SPI);
    rotation_ = disp_.getRotation();
    Serial.printf("[TDeckBoard] display init OK: %ux%u\n", disp_._width, disp_._height);
#else
    Serial.println("[TDeckBoard] display init skipped: missing DISP_* pins");
#endif

    // Initialize radio minimally; only mark online on success.
    devices_probe_ = 0;
    radio_.reset();
    int radio_state = radio_.begin();
    if (radio_state == RADIOLIB_ERR_NONE)
    {
        devices_probe_ |= HW_RADIO_ONLINE;
        Serial.println("[TDeckBoard] radio init OK");
    }
    else
    {
        Serial.printf("[TDeckBoard] radio init failed: %d\n", radio_state);
    }

    if ((disable_hw_init & NO_HW_GPS) == 0)
    {
        (void)initGPS();
    }
    else
    {
        Serial.println("[TDeckBoard] GPS init skipped by NO_HW_GPS");
    }
    Serial.println("[TDeckBoard] begin: early probe done");
    return devices_probe_;
}

void TDeckBoard::setRotation(uint8_t rotation)
{
    disp_.setRotation(rotation);
    rotation_ = disp_.getRotation();
}

uint8_t TDeckBoard::getRotation()
{
    return disp_.getRotation();
}

void TDeckBoard::pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t* color)
{
    disp_.pushColors(x1, y1, x2, y2, color);
}

uint16_t TDeckBoard::width()
{
    return disp_._width;
}

uint16_t TDeckBoard::height()
{
    return disp_._height;
}

namespace
{
TDeckBoard& getInstanceRef()
{
    return *TDeckBoard::getInstance();
}
} // namespace

TDeckBoard& instance = getInstanceRef();
BoardBase& board = getInstanceRef();
