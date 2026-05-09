#include "boards/gat562_mesh_evb_pro/gat562_board.h"

#include "boards/gat562_mesh_evb_pro/board_profile.h"
#include "boards/gat562_mesh_evb_pro/gps_runtime.h"
#include "boards/gat562_mesh_evb_pro/input_runtime.h"
#include "boards/gat562_mesh_evb_pro/settings_store.h"
#include "boards/gat562_mesh_evb_pro/sx1262_radio_packet_io.h"
#include "platform/nrf52/arduino_common/chat/infra/radio_packet_io.h"
#include "ui/mono_128x64/runtime.h"

#include ".pio/libdeps/gat562_mesh_evb_pro/Adafruit SSD1306/Adafruit_SSD1306.h"
#include <Arduino.h>
#include <Wire.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace boards::gat562_mesh_evb_pro
{
namespace
{

void writeLed(int pin, bool active_high, bool on)
{
    if (pin < 0)
    {
        return;
    }
    pinMode(pin, OUTPUT);
    digitalWrite(pin, (on == active_high) ? HIGH : LOW);
}

} // namespace

Gat562Board& Gat562Board::instance()
{
    static Gat562Board board_instance;
    return board_instance;
}

Gat562Board::Gat562Board()
    : gps_runtime_(new GpsRuntime()),
      input_runtime_(new InputRuntime())
{
}

Gat562Board::~Gat562Board() = default;

uint32_t Gat562Board::begin(uint32_t disable_hw_init)
{
    (void)disable_hw_init;
    if (initialized_)
    {
        return 1U;
    }

    initializeBoardHardware();
    ensureI2cReady();
    message_tone_volume_ = ::boards::gat562_mesh_evb_pro::settings_store::loadMessageToneVolume();
    initialized_ = true;
    return 1U;
}

void Gat562Board::initializeBoardHardware()
{
    const auto& profile = kBoardProfile;
    enablePeripheralRail();

    writeLed(profile.leds.status, profile.leds.active_high, false);
    if (!profile.leds.notification_shares_status)
    {
        writeLed(profile.leds.notification, profile.leds.active_high, false);
    }

    const auto setup_input = [](int pin, bool pullup)
    {
        if (pin >= 0)
        {
            pinMode(pin, pullup ? INPUT_PULLUP : INPUT);
        }
    };

    setup_input(profile.inputs.button_primary, profile.inputs.buttons_need_pullup);
    setup_input(profile.inputs.button_secondary, profile.inputs.buttons_need_pullup);
    setup_input(profile.inputs.joystick_up, profile.inputs.joystick_need_pullup);
    setup_input(profile.inputs.joystick_down, profile.inputs.joystick_need_pullup);
    setup_input(profile.inputs.joystick_left, profile.inputs.joystick_need_pullup);
    setup_input(profile.inputs.joystick_right, profile.inputs.joystick_need_pullup);
    setup_input(profile.inputs.joystick_press, profile.inputs.joystick_need_pullup);
}

void Gat562Board::enablePeripheralRail()
{
    if (peripheral_rail_enabled_)
    {
        return;
    }

    const auto& profile = kBoardProfile;
    if (profile.peripheral_3v3_enable >= 0)
    {
        pinMode(profile.peripheral_3v3_enable, OUTPUT);
        digitalWrite(profile.peripheral_3v3_enable, HIGH);
    }
    peripheral_rail_enabled_ = true;
}

void Gat562Board::wakeUp()
{
    enablePeripheralRail();
    setStatusLed(false);
}

void Gat562Board::handlePowerButton()
{
    pulseNotificationLed(40);
}

void Gat562Board::softwareShutdown()
{
    NVIC_SystemReset();
}

int Gat562Board::getPowerTier() const
{
    const int level = const_cast<Gat562Board*>(this)->readBatteryPercent();
    if (level < 0)
    {
        return 0;
    }
    if (level <= 10)
    {
        return 2;
    }
    if (level <= 20)
    {
        return 1;
    }
    return 0;
}

void Gat562Board::setBrightness(uint8_t level)
{
    brightness_ = static_cast<uint8_t>(
        std::clamp<int>(level, DEVICE_MIN_BRIGHTNESS_LEVEL, DEVICE_MAX_BRIGHTNESS_LEVEL));
}

uint8_t Gat562Board::getBrightness()
{
    return brightness_;
}

bool Gat562Board::hasKeyboard()
{
    return false;
}

void Gat562Board::keyboardSetBrightness(uint8_t level)
{
    keyboard_brightness_ = level;
}

uint8_t Gat562Board::keyboardGetBrightness()
{
    return keyboard_brightness_;
}

bool Gat562Board::isRTCReady() const
{
    return gps_runtime_ ? gps_runtime_->isRtcReady() : false;
}

bool Gat562Board::isCharging()
{
    return false;
}

int Gat562Board::readBatteryPercent() const
{
    const auto& battery = kBoardProfile.battery;
    if (battery.adc_pin < 0)
    {
        return -1;
    }

    analogReference(AR_INTERNAL_3_0);
    analogReadResolution(battery.adc_resolution_bits);
    const int raw = analogRead(battery.adc_pin);
    if (raw <= 0)
    {
        return -1;
    }

    const float max_raw = static_cast<float>((1UL << battery.adc_resolution_bits) - 1UL);
    const float voltage = (static_cast<float>(raw) / max_raw) * battery.aref_voltage * battery.adc_multiplier;
    const float ratio = (voltage - 3.30f) / (4.20f - 3.30f);
    const float clamped = ratio < 0.0f ? 0.0f : (ratio > 1.0f ? 1.0f : ratio);
    return static_cast<int>(clamped * 100.0f + 0.5f);
}

int Gat562Board::getBatteryLevel()
{
    return readBatteryPercent();
}

bool Gat562Board::isSDReady() const
{
    return false;
}

bool Gat562Board::isCardReady()
{
    return false;
}

bool Gat562Board::isGPSReady() const
{
    return isGpsRuntimeReady();
}

void Gat562Board::vibrator()
{
    pulseNotificationLed(20);
}

void Gat562Board::stopVibrator()
{
    const auto& leds = kBoardProfile.leds;
    const int pin = leds.notification_shares_status ? leds.status : leds.notification;
    writeLed(pin, leds.active_high, false);
}

void Gat562Board::playMessageTone()
{
    pulseNotificationLed(25);

    if (message_tone_volume_ == 0)
    {
        return;
    }

    const int buzzer_pin = kBoardProfile.buzzer.pin;
    if (buzzer_pin < 0)
    {
        return;
    }

    pinMode(buzzer_pin, OUTPUT);
    digitalWrite(buzzer_pin, kBoardProfile.buzzer.active_high ? LOW : HIGH);

    struct ToneStep
    {
        unsigned frequency_hz;
        uint16_t duration_ms;
        uint16_t gap_ms;
    };

    static constexpr ToneStep kMessageTone[] = {
        {1760U, 70U, 25U},
        {2093U, 110U, 0U},
    };

    for (const ToneStep& step : kMessageTone)
    {
        tone(static_cast<uint8_t>(buzzer_pin), step.frequency_hz, step.duration_ms);
        delay(step.duration_ms);
        noTone(static_cast<uint8_t>(buzzer_pin));
        if (step.gap_ms > 0)
        {
            delay(step.gap_ms);
        }
    }

    digitalWrite(buzzer_pin, kBoardProfile.buzzer.active_high ? LOW : HIGH);
}

void Gat562Board::setMessageToneVolume(uint8_t volume_percent)
{
    message_tone_volume_ = volume_percent;
    ::boards::gat562_mesh_evb_pro::settings_store::queueSaveMessageToneVolume(volume_percent);
}

uint8_t Gat562Board::getMessageToneVolume() const
{
    return message_tone_volume_;
}

void Gat562Board::setStatusLed(bool on)
{
    const auto& leds = kBoardProfile.leds;
    writeLed(leds.status, leds.active_high, on);
}

void Gat562Board::pulseNotificationLed(uint32_t pulse_ms)
{
    const auto& leds = kBoardProfile.leds;
    const int pin = leds.notification_shares_status ? leds.status : leds.notification;
    if (pin < 0)
    {
        return;
    }

    writeLed(pin, leds.active_high, true);
    delay(pulse_ms);
    writeLed(pin, leds.active_high, false);
}

bool Gat562Board::pollInputSnapshot(BoardInputSnapshot* out_snapshot) const
{
    return input_runtime_ ? input_runtime_->pollSnapshot(out_snapshot) : false;
}

bool Gat562Board::formatLoraFrequencyMHz(uint32_t freq_hz, char* out, std::size_t out_len) const
{
    if (!out || out_len == 0 || freq_hz == 0)
    {
        return false;
    }

    const uint32_t mhz = freq_hz / 1000000UL;
    const uint32_t khz = (freq_hz % 1000000UL) / 1000UL;
    std::snprintf(out, out_len, "%lu.%03luMHz",
                  static_cast<unsigned long>(mhz),
                  static_cast<unsigned long>(khz));
    return true;
}

uint16_t Gat562Board::inputDebounceMs() const
{
    return input_runtime_ ? input_runtime_->debounceMs() : kBoardProfile.inputs.debounce_ms;
}

bool Gat562Board::ensureI2cReady()
{
    if (i2c_initialized_)
    {
        return true;
    }

    const auto& profile = kBoardProfile;
    Wire.setPins(profile.oled_i2c.sda, profile.oled_i2c.scl);
    Wire.begin();
    Wire.setClock(400000);
    i2c_initialized_ = true;
    return true;
}

bool Gat562Board::lockI2c(uint32_t timeout_ms)
{
    const uint32_t start_ms = millis();
    while (true)
    {
        noInterrupts();
        if (!i2c_locked_)
        {
            i2c_locked_ = true;
            interrupts();
            return true;
        }
        interrupts();

        if ((millis() - start_ms) >= timeout_ms)
        {
            return false;
        }
        delay(1);
    }
}

void Gat562Board::unlockI2c()
{
    noInterrupts();
    i2c_locked_ = false;
    interrupts();
}

TwoWire& Gat562Board::i2cWire()
{
    (void)ensureI2cReady();
    return Wire;
}

bool Gat562Board::pollInputEvent(BoardInputEvent* out_event)
{
    return input_runtime_ ? input_runtime_->pollEvent(out_event) : false;
}

namespace
{
constexpr int kMonoScreenWidth = 128;
constexpr int kMonoScreenHeight = 64;

class Ssd1306MonoDisplay final : public ::ui::mono_128x64::MonoDisplay
{
  public:
    Ssd1306MonoDisplay()
        : display_(kMonoScreenWidth,
                   kMonoScreenHeight,
                   &::boards::gat562_mesh_evb_pro::Gat562Board::instance().i2cWire(),
                   -1)
    {
    }

    bool begin() override;
    int width() const override { return kMonoScreenWidth; }
    int height() const override { return kMonoScreenHeight; }
    void clear() override
    {
        if (online_)
        {
            display_.clearDisplay();
        }
    }
    void drawPixel(int x, int y, bool on) override
    {
        if (online_)
        {
            display_.drawPixel(x, y, on ? SSD1306_WHITE : SSD1306_BLACK);
        }
    }
    void drawHLine(int x, int y, int w) override
    {
        if (online_)
        {
            display_.drawFastHLine(x, y, w, SSD1306_WHITE);
        }
    }
    void fillRect(int x, int y, int w, int h, bool on) override
    {
        if (online_)
        {
            display_.fillRect(x, y, w, h, on ? SSD1306_WHITE : SSD1306_BLACK);
        }
    }
    void present() override
    {
        if (!online_)
        {
            return;
        }
        auto& board = ::boards::gat562_mesh_evb_pro::Gat562Board::instance();
        Gat562Board::I2cGuard guard(board, 100);
        if (guard)
        {
            display_.display();
        }
    }

  private:
    Adafruit_SSD1306 display_;
    bool initialized_ = false;
    bool online_ = false;
};

bool Ssd1306MonoDisplay::begin()
{
    if (initialized_)
    {
        return online_;
    }
    initialized_ = true;

    const auto& profile = ::boards::gat562_mesh_evb_pro::kBoardProfile;
    auto& board = ::boards::gat562_mesh_evb_pro::Gat562Board::instance();
    Gat562Board::I2cGuard guard(board, 200);
    if (!guard)
    {
        return false;
    }
    online_ = display_.begin(SSD1306_SWITCHCAPVCC, profile.oled_i2c.address, true, false);
    if (online_)
    {
        display_.clearDisplay();
        display_.setTextWrap(false);
        // Mono UI text is rendered by ui::mono_128x64::TextRenderer. Keep the
        // panel implementation at raw-pixel level so NRF never falls back to
        // Adafruit_GFX's built-in ASCII font path.
        display_.display();
    }
    return online_;
}

} // namespace

::ui::mono_128x64::MonoDisplay& Gat562Board::monoDisplay()
{
    static Ssd1306MonoDisplay display;
    return display;
}

Gat562Board::I2cGuard::I2cGuard(Gat562Board& board, uint32_t timeout_ms)
    : board_(&board),
      locked_(board.ensureI2cReady() && board.lockI2c(timeout_ms))
{
}

Gat562Board::I2cGuard::~I2cGuard()
{
    if (board_ && locked_)
    {
        board_->unlockI2c();
    }
}

bool Gat562Board::I2cGuard::locked() const
{
    return locked_;
}

Gat562Board::I2cGuard::operator bool() const
{
    return locked_;
}

const char* Gat562Board::defaultLongName() const
{
    return kBoardProfile.identity.long_name;
}

const char* Gat562Board::defaultShortName() const
{
    return kBoardProfile.identity.short_name;
}

const char* Gat562Board::defaultBleName() const
{
    return kBoardProfile.identity.ble_name;
}

bool Gat562Board::prepareRadioHardware()
{
    if (radio_hw_ready_)
    {
        return true;
    }

    (void)begin();
    enablePeripheralRail();

    const auto& profile = kBoardProfile;
    if (profile.lora.power_en >= 0)
    {
        pinMode(profile.lora.power_en, OUTPUT);
        digitalWrite(profile.lora.power_en, HIGH);
        delay(5);
    }

    SPI.begin();
    radio_hw_ready_ = true;
    return true;
}

bool Gat562Board::beginRadioIo()
{
    return ::boards::gat562_mesh_evb_pro::sx1262RadioPacketIo().begin();
}

platform::nrf52::arduino_common::chat::infra::IRadioPacketIo* Gat562Board::bindRadioIo()
{
    auto& io = ::boards::gat562_mesh_evb_pro::sx1262RadioPacketIo();
    ::platform::nrf52::arduino_common::chat::infra::bindRadioPacketIo(&io);
    return &io;
}

void Gat562Board::applyRadioConfig(chat::MeshProtocol protocol, const chat::MeshConfig& config)
{
    ::boards::gat562_mesh_evb_pro::sx1262RadioPacketIo().applyConfig(protocol, config);
}

uint32_t Gat562Board::activeLoraFrequencyHz() const
{
    return ::boards::gat562_mesh_evb_pro::sx1262RadioPacketIo().appliedFrequencyHz();
}

bool Gat562Board::startGpsRuntime(const app::AppConfig& config)
{
    return gps_runtime_ ? gps_runtime_->start(config) : false;
}

bool Gat562Board::beginGps(const app::AppConfig& config)
{
    return gps_runtime_ ? gps_runtime_->begin(config) : false;
}

void Gat562Board::applyGpsConfig(const app::AppConfig& config)
{
    if (gps_runtime_)
    {
        gps_runtime_->applyConfig(config);
    }
}

void Gat562Board::tickGps()
{
    if (gps_runtime_)
    {
        gps_runtime_->tick();
    }
}

bool Gat562Board::isGpsRuntimeReady() const
{
    return gps_runtime_ ? gps_runtime_->isReady() : false;
}

::gps::GpsState Gat562Board::gpsData() const
{
    return gps_runtime_ ? gps_runtime_->data() : ::gps::GpsState{};
}

bool Gat562Board::gpsEnabled() const
{
    return gps_runtime_ ? gps_runtime_->enabled() : false;
}

bool Gat562Board::gpsPowered() const
{
    return gps_runtime_ ? gps_runtime_->powered() : false;
}

uint32_t Gat562Board::gpsLastMotionMs() const
{
    return gps_runtime_ ? gps_runtime_->lastMotionMs() : 0;
}

bool Gat562Board::gpsGnssSnapshot(::gps::GnssSatInfo* out,
                                  std::size_t max,
                                  std::size_t* out_count,
                                  ::gps::GnssStatus* status) const
{
    return gps_runtime_ ? gps_runtime_->gnssSnapshot(out, max, out_count, status) : false;
}

void Gat562Board::setGpsCollectionInterval(uint32_t interval_ms)
{
    if (gps_runtime_)
    {
        gps_runtime_->setCollectionInterval(interval_ms);
    }
}
void Gat562Board::setGpsEnabled(bool enabled)
{
    if (gps_runtime_)
    {
        gps_runtime_->setEnabled(enabled);
    }
}
void Gat562Board::setGpsPowerStrategy(uint8_t strategy)
{
    if (gps_runtime_)
    {
        gps_runtime_->setPowerStrategy(strategy);
    }
}
void Gat562Board::setGpsConfig(uint8_t mode, uint8_t sat_mask)
{
    if (gps_runtime_)
    {
        gps_runtime_->setConfig(mode, sat_mask);
    }
}
void Gat562Board::setGpsExternalNmeaConfig(uint8_t output_hz, uint8_t sentence_mask)
{
    if (gps_runtime_)
    {
        gps_runtime_->setExternalNmeaConfig(output_hz, sentence_mask);
    }
}
void Gat562Board::setGpsMotionIdleTimeout(uint32_t timeout_ms)
{
    if (gps_runtime_)
    {
        gps_runtime_->setMotionIdleTimeout(timeout_ms);
    }
}
void Gat562Board::setGpsMotionSensorId(uint8_t sensor_id)
{
    if (gps_runtime_)
    {
        gps_runtime_->setMotionSensorId(sensor_id);
    }
}
void Gat562Board::suspendGps()
{
    if (gps_runtime_)
    {
        gps_runtime_->suspend();
    }
}

void Gat562Board::resumeGps()
{
    if (gps_runtime_)
    {
        gps_runtime_->resume();
    }
}
void Gat562Board::setCurrentEpochSeconds(uint32_t epoch_s)
{
    if (gps_runtime_)
    {
        gps_runtime_->setCurrentEpochSeconds(epoch_s);
    }
}

uint32_t Gat562Board::currentEpochSeconds() const
{
    return gps_runtime_ ? gps_runtime_->currentEpochSeconds() : 0;
}

} // namespace boards::gat562_mesh_evb_pro

BoardBase& board = ::boards::gat562_mesh_evb_pro::Gat562Board::instance();
