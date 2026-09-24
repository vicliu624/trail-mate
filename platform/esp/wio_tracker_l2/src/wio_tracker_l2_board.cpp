#if defined(ARDUINO_WIO_TRACKER_L2)

#include "platform/esp/wio_tracker_l2/wio_tracker_l2_board.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/common/memory_budget.h"
#include "platform/ui/screen_runtime.h"

// Keep the codec library's GPIO class out of the global ESP/Lovyan namespace.
#define NO_USING_NAMESPACE_AUDIO_DRIVER 1
#define TRAIL_MATE_AUDIO_DRIVER_EXPLICIT_INSTANCES 1
#include <AudioDriver.h>
#include <DriverDeviceInfo.h>
#include <LovyanGFX.hpp>
#include <Wire.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <codec2.h>
#include <cstring>
#include <ctime>
#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>
#include <freertos/semphr.h>
#include <limits>
#include <new>
#include <sys/time.h>

namespace boards::wio_tracker_l2
{
static_assert(SDA == gpio::kI2cSda && SCL == gpio::kI2cScl,
              "Arduino variant I2C pins must match the WIO board profile");
static_assert(TX == gpio::kGpsTx && RX == gpio::kGpsRx,
              "Arduino variant UART pins must match the WIO board profile");
static_assert(SCK == gpio::kRadioSck && MISO == gpio::kRadioMiso &&
                  MOSI == gpio::kRadioMosi && SS == gpio::kRadioCs,
              "Arduino variant SPI pins must match the WIO board profile");
#if defined(RGB_BUILTIN) || defined(LED_BUILTIN)
#error "WIO's user LED belongs to the expander, not an Arduino GPIO/NeoPixel alias"
#endif
namespace
{
StaticSemaphore_t s_i2c_mutex_storage;
StaticSemaphore_t s_radio_mutex_storage;
SemaphoreHandle_t s_i2c_mutex = nullptr;
SemaphoreHandle_t s_radio_mutex = nullptr;
StaticSemaphore_t s_audio_mutex_storage;
SemaphoreHandle_t s_audio_mutex = nullptr;
std::atomic<bool> s_tone_pending{false};
audio_driver::DriverDeviceInfo s_codec_pins;
audio_driver::AudioDriverES8311Class s_codec;
constexpr uint32_t kAudioSampleRate = 16000;
int16_t s_tone_pcm[256];

class BusLock
{
  public:
    explicit BusLock(SemaphoreHandle_t mutex, uint32_t wait_ms = 100)
        : mutex_(mutex), locked_(mutex && xSemaphoreTakeRecursive(mutex, pdMS_TO_TICKS(wait_ms)) == pdTRUE) {}
    ~BusLock()
    {
        if (locked_) xSemaphoreGiveRecursive(mutex_);
    }
    explicit operator bool() const { return locked_; }

  private:
    SemaphoreHandle_t mutex_;
    bool locked_;
};

bool writeBytes(uint8_t address, uint8_t reg, const uint8_t* data, size_t size)
{
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.write(data, size);
    return Wire.endTransmission() == 0;
}

bool writeByte(uint8_t address, uint8_t reg, uint8_t value)
{
    return writeBytes(address, reg, &value, 1);
}

bool readBytes(uint8_t address, uint8_t reg, uint8_t* data, size_t size)
{
    Wire.beginTransmission(address);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(address, size) != size)
    {
        return false;
    }
    for (size_t index = 0; index < size; ++index) data[index] = Wire.read();
    return true;
}

bool writeExpanderWord(uint8_t reg, uint16_t value)
{
    const uint8_t bytes[] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)};
    return writeBytes(i2c::kExpanderAddress, reg, bytes, sizeof(bytes));
}

bool readTouchRegister(uint8_t address, uint16_t reg, uint8_t* data, size_t size)
{
    Wire.beginTransmission(address);
    Wire.write(static_cast<uint8_t>(reg >> 8));
    Wire.write(static_cast<uint8_t>(reg));
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom(address, size) != size) return false;
    for (size_t index = 0; index < size; ++index) data[index] = Wire.read();
    return true;
}

bool clearTouchStatus(uint8_t address)
{
    Wire.beginTransmission(address);
    Wire.write(0x81);
    Wire.write(0x4E);
    Wire.write(0);
    return Wire.endTransmission() == 0;
}

class WioDisplay final : public lgfx::LGFX_Device
{
  public:
    WioDisplay()
    {
        auto bus = bus_.config();
        bus.spi_host = SPI3_HOST;
        bus.spi_mode = 3;
        bus.freq_write = 40000000;
        bus.freq_read = 16000000;
        bus.pin_sclk = gpio::kDisplayClock;
        bus.pin_io0 = gpio::kDisplayIo0;
        bus.pin_io1 = gpio::kDisplayIo1;
        bus.pin_io2 = gpio::kDisplayIo2;
        bus.pin_io3 = gpio::kDisplayIo3;
        bus_.config(bus);
        panel_.setBus(&bus_);
        auto panel = panel_.config();
        panel.pin_cs = gpio::kDisplayCs;
        panel.pin_rst = -1;
        panel.pin_busy = -1;
        panel.panel_width = panel.memory_width = kPanelWidth;
        panel.panel_height = panel.memory_height = kPanelHeight;
        panel.offset_rotation = 1;
        panel.invert = true;
        panel.rgb_order = true;
        panel.dlen_16bit = false;
        panel.bus_shared = false;
        panel_.config(panel);
        setPanel(&panel_);
    }

  private:
    lgfx::Bus_SPI bus_;
    lgfx::Panel_NV3031B panel_;
};

WioDisplay s_display;
} // namespace

WioTrackerL2Board::WioTrackerL2Board()
    : LilyGo_Display(SPI_DRIVER, false),
      radio_spi_(FSPI),
      radio_module_(gpio::kRadioCs, gpio::kRadioDio1, gpio::kRadioReset, gpio::kRadioBusy,
                    radio_spi_, SPISettings(8000000, MSBFIRST, SPI_MODE0)),
      radio_(&radio_module_) {}

WioTrackerL2Board& WioTrackerL2Board::instance()
{
    // Match T-Deck's placement policy. The current OPI SDK initializes PSRAM
    // before global constructors; internal RAM remains the fallback. This
    // object is task-context state (radio IRQs are polled). RTOS locks and
    // peripheral DMA buffers retain their separate internal-memory ownership.
    static WioTrackerL2Board* board = []() -> WioTrackerL2Board*
    {
        void* storage = heap_caps_malloc_prefer(sizeof(WioTrackerL2Board),
                                                2,
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (storage == nullptr)
        {
            storage = ::operator new(sizeof(WioTrackerL2Board));
        }
        return new (storage) WioTrackerL2Board();
    }();
    return *board;
}

bool WioTrackerL2Board::writeExpander(ExpanderPin pin, bool high)
{
    BusLock lock(s_i2c_mutex);
    if (!lock || !expander_ready_) return false;
    const uint16_t bit = uint16_t{1} << static_cast<uint8_t>(pin);
    const uint16_t next = high ? output_latch_ | bit : output_latch_ & ~bit;
    if (!writeExpanderWord(2, next)) return false;
    output_latch_ = next;
    return true;
}

bool WioTrackerL2Board::initializePower()
{
    BusLock lock(s_i2c_mutex);
    if (!lock) return false;
    // LCD power/control high; GPS reset asserted, ADC enabled. OTG, PA,
    // Grove, SD and GPS power remain low until their owners request them.
    output_latch_ = (1U << 4) | (1U << 5) | (1U << 9) | (1U << 15);
    direction_mask_ = 0x0007; // P00..P02 inputs; touch INT low selects 0x5D.
    if (!writeExpanderWord(2, output_latch_) || !writeExpanderWord(6, direction_mask_))
    {
        Serial.println("[WioL2] GPIO expander initialization failed");
        return false;
    }
    expander_ready_ = true;
    // AW35615 operates as a USB-C sink. VBUS is the upstream L2 power-status
    // source; it does not measure charge current or charge completion.
    uint8_t device_id = 0;
    if (readBytes(i2c::kUsbControllerAddress, 0x01, &device_id, 1))
    {
        writeByte(i2c::kUsbControllerAddress, 0x0B, 0x0F);
        writeByte(i2c::kUsbControllerAddress, 0x02, 0x03);
        writeByte(i2c::kUsbControllerAddress, 0x08, 0x05);
    }
    return true;
}

bool WioTrackerL2Board::initializeBacklight()
{
    BusLock lock(s_i2c_mutex);
    if (!lock) return false;
    // LP5814 manual PWM mode. All four channels illuminate the panel.
    bool ok = writeByte(i2c::kBacklightAddress, 0x00, 0x01);
    ok = writeByte(i2c::kBacklightAddress, 0x01, 0x01) && ok;
    ok = writeByte(i2c::kBacklightAddress, 0x02, 0x00) && ok;
    ok = writeByte(i2c::kBacklightAddress, 0x04, 0x4E) && ok;
    ok = writeByte(i2c::kBacklightAddress, 0x05, 0xF0) && ok;
    for (uint8_t channel = 0; channel < 4; ++channel)
    {
        ok = writeByte(i2c::kBacklightAddress, 0x14 + channel, 200) && ok;
        ok = writeByte(i2c::kBacklightAddress, 0x18 + channel, 0) && ok;
    }
    ok = writeByte(i2c::kBacklightAddress, 0x02, 0x0F) && ok;
    ok = writeByte(i2c::kBacklightAddress, 0x0F, 0x55) && ok;
    delay(5);
    return ok;
}

bool WioTrackerL2Board::initializeDisplay()
{
    if (!writeExpander(ExpanderPin::DisplayReset, true)) return false;
    delay(5);
    if (!writeExpander(ExpanderPin::DisplayReset, false)) return false;
    delay(10);
    if (!writeExpander(ExpanderPin::DisplayReset, true)) return false;
    delay(150);
    if (!s_display.init()) return false;
    s_display.setRotation(rotation_);
    s_display.setSwapBytes(false); // LVGL supplies native little-endian RGB565.
    s_display.fillScreen(0);

    {
        BusLock lock(s_i2c_mutex);
        if (!lock || !writeExpander(ExpanderPin::TouchReset, true)) return false;
        delay(60);
        direction_mask_ |= 1U << static_cast<uint8_t>(ExpanderPin::TouchInterrupt);
        if (!writeExpanderWord(6, direction_mask_)) return false;
        uint8_t product_id[4]{};
        for (uint8_t address : {i2c::kTouchAddress, i2c::kTouchAlternateAddress})
        {
            if (readTouchRegister(address, 0x8140, product_id, sizeof(product_id)) &&
                product_id[0] == '9' && product_id[1] == '1')
            {
                touch_address_ = address;
                touch_ready_ = true;
                clearTouchStatus(address);
                break;
            }
        }
    }
    return true;
}

uint32_t WioTrackerL2Board::begin(uint32_t disable_hw_init)
{
    if (started_) return display_ready_ ? 0 : 1;
    s_i2c_mutex = xSemaphoreCreateRecursiveMutexStatic(&s_i2c_mutex_storage);
    s_radio_mutex = xSemaphoreCreateRecursiveMutexStatic(&s_radio_mutex_storage);
    s_audio_mutex = xSemaphoreCreateRecursiveMutexStatic(&s_audio_mutex_storage);
    if (!s_i2c_mutex || !s_radio_mutex || !s_audio_mutex || !Wire.begin(gpio::kI2cSda, gpio::kI2cScl, i2c::kFrequencyHz))
    {
        return 1;
    }
    Wire.setTimeOut(50);
    if (!initializePower()) return 1;
    backlight_ready_ = initializeBacklight();
    display_ready_ = initializeDisplay();
    if (display_ready_) writeBacklight(brightness_);
    radio_spi_.begin(gpio::kRadioSck, gpio::kRadioMiso, gpio::kRadioMosi, gpio::kRadioCs);
    const int radio_result = radio_.begin(869.525, 250.0, 11, 5, 0x2B, 14, 16, 1.8);
    radio_ready_ = radio_result == RADIOLIB_ERR_NONE;
    if (radio_ready_) radio_.setDio2AsRfSwitch(true);
    if ((disable_hw_init & NO_HW_GPS) == 0) initGPS();
    if ((disable_hw_init & NO_HW_SD) == 0) installSD();
    started_ = true;
    Serial.printf("[WioL2] display=%d touch=%d backlight=%d radio=%d (rc=%d)\n",
                  display_ready_, touch_ready_, backlight_ready_, radio_ready_, radio_result);
    return display_ready_ ? 0 : 1;
}

void WioTrackerL2Board::writeBacklight(uint8_t level)
{
    BusLock lock(s_i2c_mutex);
    if (!lock || !backlight_ready_) return;
    const uint8_t pwm = (static_cast<uint32_t>(level) * 255U) / DEVICE_MAX_BRIGHTNESS_LEVEL;
    for (uint8_t channel = 0; channel < 4; ++channel)
    {
        if (!writeByte(i2c::kBacklightAddress, 0x18 + channel, pwm))
            Serial.println("[WioL2] backlight write failed");
    }
}

void WioTrackerL2Board::setBrightness(uint8_t level)
{
    brightness_ = std::min<uint8_t>(level, DEVICE_MAX_BRIGHTNESS_LEVEL);
    writeBacklight(screen_sleeping_ ? 0 : brightness_);
}

void WioTrackerL2Board::setRotation(uint8_t rotation)
{
    // The target selects orientation policy; the adapter executes rotation.
    rotation_ = rotation & 3U;
    if (display_ready_) s_display.setRotation(rotation_);
}

uint8_t WioTrackerL2Board::getPoint(int16_t* x, int16_t* y, uint8_t count)
{
    if (!touch_ready_ || !x || !y || count == 0) return 0;
    BusLock lock(s_i2c_mutex, 10);
    if (!lock) return 0;
    uint8_t status = 0;
    if (!readTouchRegister(touch_address_, 0x814E, &status, 1))
    {
        touch_pressed_ = false;
        return 0;
    }
    if (status & 0x80)
    {
        const uint8_t contacts = status & 0x0F;
        uint8_t coordinates[4]{};
        // LVGL consumes a single pointer. Read only the first contact and
        // reject malformed counts instead of trusting a controller-sized loop.
        touch_pressed_ = contacts >= 1 && contacts <= 5 &&
                         readTouchRegister(touch_address_, 0x8150, coordinates, sizeof(coordinates));
        if (touch_pressed_)
        {
            const uint16_t raw_x = coordinates[0] | (coordinates[1] << 8);
            const uint16_t raw_y = coordinates[2] | (coordinates[3] << 8);
            touch_pressed_ = raw_x < kPanelWidth && raw_y < kPanelHeight;
            // Upstream L2: panel rotation offset 1 plus touch offset 2 = 3.
            // This swaps the native axes and mirrors logical X.
            touch_x_ = static_cast<int16_t>(319 - raw_y);
            touch_y_ = static_cast<int16_t>(raw_x);
            switch (rotation_)
            {
            case 1:
                touch_x_ = raw_x;
                touch_y_ = raw_y;
                break;
            case 2:
                touch_x_ = raw_y;
                touch_y_ = static_cast<int16_t>(239 - raw_x);
                break;
            case 3:
                touch_x_ = static_cast<int16_t>(239 - raw_x);
                touch_y_ = static_cast<int16_t>(319 - raw_y);
                break;
            default:
                break;
            }
        }
        if (!clearTouchStatus(touch_address_)) touch_pressed_ = false;
        touch_sample_ms_ = millis();
    }
    if (!touch_pressed_ || millis() - touch_sample_ms_ > 100) return 0;
    *x = touch_x_;
    *y = touch_y_;
    return 1;
}

DisplayTransferResult WioTrackerL2Board::transferPixels(
    uint16_t x,
    uint16_t y,
    uint16_t w,
    uint16_t h,
    uint16_t* pixels)
{
    if (!display_ready_)
    {
        return DisplayTransferResult::Failed;
    }

    if (pixels == nullptr || w == 0 || h == 0)
    {
        return DisplayTransferResult::Failed;
    }

    if (x >= width() ||
        y >= height() ||
        static_cast<uint32_t>(x) + w > width() ||
        static_cast<uint32_t>(y) + h > height())
    {
        return DisplayTransferResult::Failed;
    }

    const uint32_t pixel_count =
        static_cast<uint32_t>(w) *
        static_cast<uint32_t>(h);

    s_display.startWrite();
    s_display.setAddrWindow(x, y, w, h);
    s_display.writePixels(pixels, pixel_count, true);
    s_display.endWrite();
    s_display.waitDMA();

    return DisplayTransferResult::Completed;
}

void WioTrackerL2Board::pushColors(
    uint16_t x,
    uint16_t y,
    uint16_t w,
    uint16_t h,
    uint16_t* pixels)
{
    (void)transferPixels(x, y, w, h, pixels);
}

void WioTrackerL2Board::enterScreenSleep()
{
    screen_sleeping_ = true;
    writeBacklight(0);
}

void WioTrackerL2Board::exitScreenSleep()
{
    screen_sleeping_ = false;
    writeBacklight(brightness_);
}

void WioTrackerL2Board::wakeUp() { exitScreenSleep(); }

bool WioTrackerL2Board::isRTCReady() const
{
    return time_ready_ || std::time(nullptr) >= 1577836800;
}

void WioTrackerL2Board::handlePowerButton()
{
    const uint32_t now = millis();
    if (now - button_poll_ms_ < 30 || !expander_ready_) return;
    button_poll_ms_ = now;
    uint8_t input = 0xFF;
    {
        BusLock lock(s_i2c_mutex, 10);
        if (!lock || !readBytes(i2c::kExpanderAddress, 0, &input, 1)) return;
    }
    const bool pressed = (input & 1) == 0;
    if (pressed && !wake_pressed_)
    {
        wake_pressed_ms_ = now;
        ::platform::ui::screen::handle_input();
    }
    else if (!pressed && wake_pressed_)
    {
        ::platform::ui::screen::handle_input_release();
    }
    wake_pressed_ = pressed;
    if (pressed && now - wake_pressed_ms_ >= 3000) softwareShutdown();
}

void WioTrackerL2Board::softwareShutdown()
{
    enterScreenSleep();
    deinitGPS();
    uninstallSD();
    {
        BusLock lock(s_radio_mutex);
        if (lock && radio_ready_) radio_.sleep();
    }
    writeExpander(ExpanderPin::AudioAmplifierEnable, false);
    writeExpander(ExpanderPin::UsbOtgEnable, false);
    // BOOT is a direct RTC GPIO. Avoid expander interrupt wake until held
    // button/interrupt-latch behavior has been validated on the PCB.
    esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(gpio::kBootButton), 0);
    esp_deep_sleep_start();
}

bool WioTrackerL2Board::installSD()
{
    if (!writeExpander(ExpanderPin::SdPower, true)) return false;
    delay(10);
    platform::esp::arduino_common::storage::SdmmcSdConfig config;
    config.clock = gpio::kSdClock;
    config.command = gpio::kSdCommand;
    config.data0 = gpio::kSdData0;
    return platform::esp::arduino_common::storage::mount_sd_card(config);
}
bool WioTrackerL2Board::ensureSDReady() { return isCardReady() || installSD(); }
bool WioTrackerL2Board::isSDReady() const { return platform::esp::arduino_common::storage::sd_card_ready(); }
bool WioTrackerL2Board::isCardReady() { return isSDReady(); }
void WioTrackerL2Board::uninstallSD() { platform::esp::arduino_common::storage::unmount_sd_card(); }

bool WioTrackerL2Board::initGPS()
{
    if (!writeExpander(ExpanderPin::GpsPower, true))
    {
        Serial.println("[WioL2][GPS] power enable failed");
        return false;
    }

    // L76K reset is active HIGH on Wio Tracker L2.
    if (!writeExpander(ExpanderPin::GpsReset, true))
    {
        Serial.println("[WioL2][GPS] reset assert failed");
        return false;
    }

    delay(10);

    if (!writeExpander(ExpanderPin::GpsReset, false))
    {
        Serial.println("[WioL2][GPS] reset release failed");
        return false;
    }

    // Give L76K time to leave reset before opening the UART.
    delay(100);

    const uint32_t baud =
        gps_config_.baud >= 4800 &&
                gps_config_.baud <= 115200
            ? gps_config_.baud
            : 9600;

    Serial1.end();
    Serial1.begin(
        baud,
        SERIAL_8N1,
        gpio::kGpsRx,
        gpio::kGpsTx);

    delay(20);

    gps_.attach(&Serial1);
    gps_ready_ = true;

    Serial.printf(
        "[WioL2][GPS] L76K ready baud=%lu rx=%d tx=%d protocol=nmea\n",
        static_cast<unsigned long>(baud),
        gpio::kGpsRx,
        gpio::kGpsTx);

    return true;
}

void WioTrackerL2Board::deinitGPS()
{
    gps_.attach(nullptr);
    Serial1.end();
    gps_ready_ = false;
    writeExpander(ExpanderPin::GpsPower, false);
}

void WioTrackerL2Board::powerControl(PowerCtrlChannel_t channel, bool enabled)
{
    if (channel == POWER_GPS)
    {
        if (enabled) initGPS();
        else deinitGPS();
    }
}

bool WioTrackerL2Board::syncTimeFromGPS(uint32_t)
{
    if (!gps_.date.isValid() || !gps_.time.isValid() || gps_.date.age() > 5000 || gps_.time.age() > 5000) return false;
    int year = gps_.date.year();
    const unsigned month = gps_.date.month();
    const unsigned day = gps_.date.day();
    if (year < 2020 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31) return false;
    // Civil UTC date to Unix days; independent of the configured local timezone.
    year -= month <= 2;
    const int era = year / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = static_cast<int64_t>(era) * 146097 + doe - 719468;
    timeval tv{};
    tv.tv_sec = days * 86400 + gps_.time.hour() * 3600 + gps_.time.minute() * 60 + gps_.time.second() + gps_.time.age() / 1000;
    time_ready_ = settimeofday(&tv, nullptr) == 0;
    return time_ready_;
}

int WioTrackerL2Board::transmitRadio(const uint8_t* data, size_t len)
{
    BusLock lock(s_radio_mutex, 2000);
    return lock && radio_ready_ ? radio_.transmit(data, len) : RADIOLIB_ERR_SPI_WRITE_FAILED;
}
int WioTrackerL2Board::startRadioReceive()
{
    BusLock lock(s_radio_mutex);
    return lock && radio_ready_ ? radio_.startReceive() : RADIOLIB_ERR_SPI_WRITE_FAILED;
}
uint32_t WioTrackerL2Board::getRadioIrqFlags()
{
    BusLock lock(s_radio_mutex, 10);
    return lock && radio_ready_ ? radio_.getIrqFlags() : 0;
}
int WioTrackerL2Board::getRadioPacketLength(bool update)
{
    BusLock lock(s_radio_mutex);
    return lock && radio_ready_ ? radio_.getPacketLength(update) : 0;
}
int WioTrackerL2Board::readRadioData(uint8_t* buffer, size_t len)
{
    BusLock lock(s_radio_mutex);
    return lock && radio_ready_ ? radio_.readData(buffer, len) : RADIOLIB_ERR_SPI_WRITE_FAILED;
}
void WioTrackerL2Board::clearRadioIrqFlags(uint32_t flags)
{
    BusLock lock(s_radio_mutex);
    if (lock && radio_ready_) radio_.clearIrqFlags(flags);
}
float WioTrackerL2Board::getRadioRSSI()
{
    BusLock lock(s_radio_mutex);
    return lock && radio_ready_ ? radio_.getRSSI() : std::numeric_limits<float>::quiet_NaN();
}
float WioTrackerL2Board::getRadioInstantRSSI()
{
    BusLock lock(s_radio_mutex);
    return lock && radio_ready_ ? radio_.getRSSI(false) : std::numeric_limits<float>::quiet_NaN();
}
float WioTrackerL2Board::getRadioSNR()
{
    BusLock lock(s_radio_mutex);
    return lock && radio_ready_ ? radio_.getSNR() : std::numeric_limits<float>::quiet_NaN();
}
int WioTrackerL2Board::configureLoraRadio(float frequency, float bandwidth, uint8_t sf, uint8_t cr,
                                          int8_t power, uint16_t preamble, uint8_t sync_word, uint8_t crc)
{
    BusLock lock(s_radio_mutex, 500);
    if (!lock || !radio_ready_) return RADIOLIB_ERR_SPI_WRITE_FAILED;
    int first_error = RADIOLIB_ERR_NONE;
    const auto note = [&](int result)
    { if (first_error == RADIOLIB_ERR_NONE) first_error = result; };
    note(radio_.standby());
    note(radio_.setDio2AsRfSwitch(true));
    note(radio_.setCurrentLimit(140));
    note(radio_.setFrequency(frequency));
    note(radio_.setBandwidth(bandwidth));
    note(radio_.setSpreadingFactor(sf));
    note(radio_.setCodingRate(cr));
    note(radio_.setOutputPower(std::max<int8_t>(-9, std::min<int8_t>(20, power))));
    note(radio_.setPreambleLength(preamble));
    note(radio_.setSyncWord(sync_word));
    note(radio_.setCRC(crc));
    return first_error;
}
bool WioTrackerL2Board::quiesceForExternalStorage()
{
    BusLock lock(s_radio_mutex);
    return lock && (!radio_ready_ || radio_.standby() == RADIOLIB_ERR_NONE);
}

int WioTrackerL2Board::getBatteryLevel()
{
    const uint32_t now = millis();
    if (battery_read_ms_ && now - battery_read_ms_ < 10000) return battery_level_;
    battery_read_ms_ = now;
    BusLock lock(s_i2c_mutex);
    if (!lock || !expander_ready_) return -1;
    // ADS1115: AIN0, +/-4.096 V, single conversion, 860 SPS; divider is 1:2.
    const uint8_t config[] = {0xC3, 0xE3};
    uint8_t data[2]{};
    if (!writeBytes(i2c::kBatteryAdcAddress, 1, config, sizeof(config))) return battery_level_ = -1;
    delay(2);
    if (!readBytes(i2c::kBatteryAdcAddress, 0, data, sizeof(data))) return battery_level_ = -1;
    const int16_t raw = static_cast<int16_t>((data[0] << 8) | data[1]);
    const int millivolts = raw / 4;
    if (millivolts < 2500 || millivolts > 4500) return battery_level_ = -1;
    static constexpr int ocv[] = {3300, 3482, 3623, 3663, 3687, 3710, 3745, 3800, 3864, 4040, 4180};
    if (millivolts >= ocv[10]) return battery_level_ = 100;
    battery_level_ = 0;
    for (int i = 0; i < 10; ++i)
    {
        if (millivolts >= ocv[i] && millivolts < ocv[i + 1])
        {
            battery_level_ = i * 10 + (millivolts - ocv[i]) * 10 / (ocv[i + 1] - ocv[i]);
            break;
        }
    }
    return battery_level_;
}

bool WioTrackerL2Board::isCharging()
{
    BusLock lock(s_i2c_mutex);
    uint8_t status = 0;
    return lock && readBytes(i2c::kUsbControllerAddress, 0x40, &status, 1) && (status & 0x80);
}

bool WioTrackerL2Board::initializeAudio()
{
    i2s_config_t config{};
    config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
    config.sample_rate = kAudioSampleRate;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    config.dma_buf_count = 4;
    config.dma_buf_len = 128;
    config.tx_desc_auto_clear = true;
    config.fixed_mclk = kAudioSampleRate * 256;
    config.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    if (i2s_driver_install(I2S_NUM_0, &config, 0, nullptr) != ESP_OK) return false;
    i2s_pin_config_t pins{};
    pins.mck_io_num = gpio::kAudioMclk;
    pins.bck_io_num = gpio::kAudioBclk;
    pins.ws_io_num = gpio::kAudioLrclk;
    pins.data_out_num = gpio::kAudioDataOut;
    pins.data_in_num = I2S_PIN_NO_CHANGE;
    if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK)
    {
        i2s_driver_uninstall(I2S_NUM_0);
        return false;
    }
    BusLock lock(s_i2c_mutex);
    if (!lock)
    {
        i2s_driver_uninstall(I2S_NUM_0);
        return false;
    }
    s_codec_pins.addI2C(audio_driver::PinFunction::CODEC, Wire);
    s_codec_pins.addI2S(audio_driver::PinFunction::CODEC, gpio::kAudioMclk, gpio::kAudioBclk,
                        gpio::kAudioLrclk, gpio::kAudioDataOut, -1);
    audio_driver::CodecConfig codec_config;
    codec_config.input_device = audio_driver::ADC_INPUT_NONE;
    codec_config.output_device = audio_driver::DAC_OUTPUT_ALL;
    codec_config.i2s.bits = audio_driver::BIT_LENGTH_16BITS;
    codec_config.i2s.rate = audio_driver::RATE_16K;
    const bool ready = s_codec.begin(codec_config, s_codec_pins) && s_codec.setVolume(75);
    Wire.setClock(i2c::kFrequencyHz);
    if (!ready) i2s_driver_uninstall(I2S_NUM_0);
    return ready;
}

bool WioTrackerL2Board::ensureAudioReady()
{
    if (audio_ready_)
    {
        return true;
    }

    BusLock lock(s_audio_mutex, 1000);

    if (!lock)
    {
        return false;
    }

    // Another caller may have initialized it while we were waiting.
    if (audio_ready_)
    {
        return true;
    }

    audio_ready_ = initializeAudio();

    if (!audio_ready_)
    {
        Serial.println("[WioL2] audio initialization failed");
    }

    return audio_ready_;
}

void WioTrackerL2Board::playMessageTone()
{
    if (tone_volume_ == 0 ||
        s_tone_pending.exchange(true))
    {
        return;
    }

    if (!ensureAudioReady())
    {
        s_tone_pending.store(false);
        return;
    }

    const BaseType_t created = xTaskCreate(
        [](void*)
        {
            WioTrackerL2Board::instance().playTone();
            s_tone_pending.store(false);
            vTaskDelete(nullptr);
        },
        "wio_alert",
        4096,
        nullptr,
        2,
        nullptr);

    if (created != pdPASS)
    {
        s_tone_pending.store(false);
    }
}

void WioTrackerL2Board::playTone()
{
    BusLock lock(s_audio_mutex, 0);
    if (!lock || !writeExpander(ExpanderPin::AudioAmplifierEnable, true)) return;
    delay(250);
    const int amplitude = 6000 * tone_volume_ / 100;
    for (unsigned chunk = 0; chunk < 20; ++chunk)
    {
        for (unsigned sample = 0; sample < 128; ++sample)
        {
            const float phase = 6.2831853F * 880.0F * (chunk * 128 + sample) / kAudioSampleRate;
            const int16_t value = static_cast<int16_t>(amplitude * std::sin(phase));
            s_tone_pcm[sample * 2] = s_tone_pcm[sample * 2 + 1] = value;
        }
        size_t written = 0;
        if (i2s_write(I2S_NUM_0, s_tone_pcm, sizeof(s_tone_pcm), &written, pdMS_TO_TICKS(100)) != ESP_OK ||
            written != sizeof(s_tone_pcm)) break;
    }
    delay(40);
    i2s_zero_dma_buffer(I2S_NUM_0);
    writeExpander(ExpanderPin::AudioAmplifierEnable, false);
}

bool WioTrackerL2Board::playCodec2Voice(const uint8_t* data, size_t size, uint8_t volume)
{
    if (!audio_ready_ || !data || size == 0 || size > 875 || size % 7 != 0) return false;
    if (!ensureAudioReady())
    {
        return false;
    }
    if (!::platform::esp::common::memory::admit("wio_voice", 0, 0, 96U * 1024U,
                                                48U * 1024U, 16U * 1024U, 256U * 1024U)) return false;
    BusLock lock(s_audio_mutex, 0);
    if (!lock) return false;
    auto* pcm = static_cast<int16_t*>(heap_caps_malloc(320 * 4 * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!pcm) return false;
    CODEC2* decoder = codec2_create(CODEC2_MODE_1300);
    bool ok = decoder && codec2_samples_per_frame(decoder) == 320 && codec2_bytes_per_frame(decoder) == 7;
    ok = ok && writeExpander(ExpanderPin::AudioAmplifierEnable, true);
    if (ok) delay(250);
    for (size_t offset = 0; ok && offset < size; offset += 7)
    {
        codec2_decode(decoder, pcm, const_cast<uint8_t*>(data + offset));
        // Codec2 is 8 kHz mono. Duplicate each sample into two stereo frames
        // to retain the codec's fixed 16 kHz clock and avoid I2C reconfiguration.
        for (size_t sample = 320; sample > 0; --sample)
        {
            const int16_t value = static_cast<int32_t>(pcm[sample - 1]) * std::min<uint8_t>(volume, 100) / 100;
            for (unsigned channel = 0; channel < 4; ++channel) pcm[(sample - 1) * 4 + channel] = value;
        }
        size_t written = 0;
        ok = i2s_write(I2S_NUM_0, pcm, 320 * 4 * sizeof(int16_t), &written, pdMS_TO_TICKS(100)) == ESP_OK &&
             written == 320 * 4 * sizeof(int16_t);
    }
    if (decoder) codec2_destroy(decoder);
    std::memset(pcm, 0, 320 * 4 * sizeof(int16_t));
    heap_caps_free(pcm);
    delay(40);
    i2s_zero_dma_buffer(I2S_NUM_0);
    writeExpander(ExpanderPin::AudioAmplifierEnable, false);
    return ok;
}

void WioTrackerL2Board::setMessageToneVolume(uint8_t level) { tone_volume_ = std::min<uint8_t>(level, 100); }

} // namespace boards::wio_tracker_l2

BoardBase& board = boards::wio_tracker_l2::WioTrackerL2Board::instance();

#endif
