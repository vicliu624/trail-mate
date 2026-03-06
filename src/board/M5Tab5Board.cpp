#include "M5Tab5Board.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gps/GPS.h"

// NOTE: This is a minimal skeleton implementation that allows the firmware
// to build and run on the M5Stack Tab5 environment. Hardware-specific
// display, touch, GNSS, LoRa, and sensor wiring will be filled in gradually.

M5Tab5Board::M5Tab5Board() : LilyGo_Display(SPI_DRIVER, false) {}

M5Tab5Board* M5Tab5Board::getInstance()
{
    static M5Tab5Board instance;
    return &instance;
}

uint32_t M5Tab5Board::begin(uint32_t /*disable_hw_init*/)
{
    // For now, just mark display/touch as logically ready so UI can run.
    display_ready_ = true;
    touch_ready_ = true;
    rtc_ready_ = false;
    sd_ready_ = false;
    gps_ready_ = false;

    // Default rotation: landscape.
    rotation_ = 1;
    return 0;
}

void M5Tab5Board::setBrightness(uint8_t level)
{
    brightness_ = level;
    // TODO: hook into Tab5 backlight control when integrating display driver.
}

void M5Tab5Board::setRotation(uint8_t rotation)
{
    rotation_ = rotation;
}

void M5Tab5Board::pushColors(uint16_t /*x1*/, uint16_t /*y1*/, uint16_t /*x2*/, uint16_t /*y2*/, uint16_t* /*color*/)
{
    // TODO: forward to Tab5 display driver (MIPI DSI) when integrated.
}

uint16_t M5Tab5Board::width()
{
    return SCREEN_WIDTH;
}

uint16_t M5Tab5Board::height()
{
    return SCREEN_HEIGHT;
}

uint8_t M5Tab5Board::getPoint(int16_t* x, int16_t* y, uint8_t get_point)
{
    if (!touch_ready_ || !x || !y)
    {
        return 0;
    }
    // TODO: wire to Tab5 touch driver (GT911/ST7123). For now, report no touch.
    (void)get_point;
    return 0;
}

RotaryMsg_t M5Tab5Board::getRotary()
{
    // Tab5 has no rotary encoder; always return "no movement".
    RotaryMsg_t msg{};
    msg.dir = ROTARY_DIR_NONE;
    msg.centerBtnPressed = false;
    return msg;
}

int M5Tab5Board::getKeyChar(char* c)
{
    // No physical keyboard on Tab5. Virtual keyboard events are injected
    // directly via LVGL, so this always reports "no key".
    if (c)
    {
        *c = '\0';
    }
    return -1;
}

// ---- LoraBoard stubs ----

int M5Tab5Board::transmitRadio(const uint8_t* /*data*/, size_t /*len*/) { return -1; }
int M5Tab5Board::startRadioReceive() { return -1; }
uint32_t M5Tab5Board::getRadioIrqFlags() { return 0; }
int M5Tab5Board::getRadioPacketLength(bool /*update*/) { return 0; }
int M5Tab5Board::readRadioData(uint8_t* /*buf*/, size_t /*len*/) { return 0; }
void M5Tab5Board::clearRadioIrqFlags(uint32_t /*flags*/) {}
float M5Tab5Board::getRadioRSSI() { return 0.0f; }
float M5Tab5Board::getRadioSNR() { return 0.0f; }
void M5Tab5Board::configureLoraRadio(float /*freq_mhz*/, float /*bw_khz*/, uint8_t /*sf*/, uint8_t /*cr_denom*/,
                                     int8_t /*tx_power*/, uint16_t /*preamble_len*/, uint8_t /*sync_word*/,
                                     uint8_t /*crc_len*/)
{
}

// ---- GpsBoard stubs ----

static GPS s_dummy_gps;

bool M5Tab5Board::initGPS()
{
    gps_ready_ = false;
    return false;
}

GPS& M5Tab5Board::getGPS()
{
    return s_dummy_gps;
}

void M5Tab5Board::powerControl(PowerCtrlChannel_t /*ch*/, bool /*enable*/)
{
}

bool M5Tab5Board::syncTimeFromGPS(uint32_t /*gps_task_interval_ms*/)
{
    return false;
}

// ---- MotionBoard stubs ----

SensorBHI260AP& M5Tab5Board::getMotionSensor()
{
    // Tab5 has IMU, but we don't wire it yet. This is a stub to satisfy interface.
    // Code paths that rely on motion sensor should be disabled for Tab5 via HAS_* flags.
    for (;;)
    {
        vTaskDelay(portMAX_DELAY);
    }
}

// ---- SdBoard stubs ----

bool M5Tab5Board::installSD()
{
    sd_ready_ = false;
    return false;
}

void M5Tab5Board::uninstallSD()
{
}

bool M5Tab5Board::isCardReady()
{
    return false;
}

// ---- Global board instance for Tab5 env ----

namespace
{
M5Tab5Board& getInstanceRef()
{
    return *M5Tab5Board::getInstance();
}
} // namespace

M5Tab5Board& instance = getInstanceRef();
BoardBase& board = getInstanceRef();
