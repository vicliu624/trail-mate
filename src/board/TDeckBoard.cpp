#include "board/TDeckBoard.h"
#include "display/drivers/ST7789TDeck.h"

TDeckBoard::TDeckBoard()
    : LilyGo_Display(SPI_DRIVER, false),
      disp_(SCREEN_WIDTH, SCREEN_HEIGHT,
            display::drivers::ST7789TDeck::getInitCommands(),
            display::drivers::ST7789TDeck::getInitCommandsCount(),
            display::drivers::ST7789TDeck::getRotationConfig(SCREEN_WIDTH, SCREEN_HEIGHT))
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

    // Initialize trackball pins early; they are active-low in LilyGo examples.
#if defined(TRACKBALL_UP) && defined(TRACKBALL_DOWN) && defined(TRACKBALL_LEFT) && defined(TRACKBALL_RIGHT)
    pinMode(TRACKBALL_UP, INPUT_PULLUP);
    pinMode(TRACKBALL_DOWN, INPUT_PULLUP);
    pinMode(TRACKBALL_LEFT, INPUT_PULLUP);
    pinMode(TRACKBALL_RIGHT, INPUT_PULLUP);
#endif
#if defined(TRACKBALL_CLICK)
    pinMode(TRACKBALL_CLICK, INPUT_PULLUP);
#endif

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
    // ST7789 on T-Deck is sensitive to long full-frame bursts; use a safer SPI clock.
    disp_.init(DISP_SCK, DISP_MISO, DISP_MOSI, DISP_CS, DISP_RST, DISP_DC, DISP_BL, 20, SPI);
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

RotaryMsg_t TDeckBoard::getRotary()
{
    RotaryMsg_t msg{};

    const uint32_t now = millis();
    const uint32_t repeat_ms = 120;   // Trackball repeat rate
    const uint32_t click_ms = 200;    // Click debounce

#if defined(TRACKBALL_RIGHT) && defined(TRACKBALL_LEFT)
    bool right = digitalRead(TRACKBALL_RIGHT) == LOW;
    bool left = digitalRead(TRACKBALL_LEFT) == LOW;

    if ((right || left) && (now - last_trackball_ms_) >= repeat_ms)
    {
        // Map horizontal movement to encoder diff for menu switching.
        msg.dir = right ? ROTARY_DIR_UP : ROTARY_DIR_DOWN;
        last_trackball_ms_ = now;
    }
#endif

#if defined(TRACKBALL_CLICK)
    bool click = digitalRead(TRACKBALL_CLICK) == LOW;
    if (click && (now - last_click_ms_) >= click_ms)
    {
        msg.centerBtnPressed = true;
        last_click_ms_ = now;
    }
#endif

    return msg;
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
