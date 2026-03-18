#include "boards/gat562_mesh_evb_pro/gat562_board.h"

#include "boards/gat562_mesh_evb_pro/board_profile.h"
#include "boards/gat562_mesh_evb_pro/settings_store.h"
#include "boards/gat562_mesh_evb_pro/sx1262_radio_packet_io.h"
#include "platform/nrf52/arduino_common/chat/infra/radio_packet_io.h"
#include "ui/mono_128x64/runtime.h"

#include ".pio/libdeps/gat562_mesh_evb_pro/Adafruit SSD1306/Adafruit_SSD1306.h"
#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <time.h>
#include <Wire.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace boards::gat562_mesh_evb_pro
{
namespace
{

bool readActiveLowPin(int pin, bool use_pullup)
{
    if (pin < 0)
    {
        return false;
    }
    pinMode(pin, use_pullup ? INPUT_PULLUP : INPUT);
    return digitalRead(pin) == LOW;
}

void writeLed(int pin, bool active_high, bool on)
{
    if (pin < 0)
    {
        return;
    }
    pinMode(pin, OUTPUT);
    digitalWrite(pin, (on == active_high) ? HIGH : LOW);
}

struct DebounceState
{
    bool stable = false;
    bool sampled = false;
    uint32_t changed_at_ms = 0;
};

struct InputRuntimeState
{
    uint32_t last_activity_ms = 0;
    BoardInputSnapshot snapshot{};
    DebounceState button_primary{};
    DebounceState button_secondary{};
    DebounceState joystick_up{};
    DebounceState joystick_down{};
    DebounceState joystick_left{};
    DebounceState joystick_right{};
    DebounceState joystick_press{};
} s_input;

struct GpsRuntimeState
{
    TinyGPSPlus parser{};
    ::gps::GpsState data{};
    ::gps::GnssStatus status{};
    uint32_t last_motion_ms = 0;
    uint32_t collection_interval_ms = 60000;
    uint32_t motion_idle_timeout_ms = 0;
    uint8_t power_strategy = 0;
    uint8_t gnss_mode = 0;
    uint8_t sat_mask = 0;
    uint8_t nmea_output_hz = 0;
    uint8_t nmea_sentence_mask = 0;
    uint8_t motion_sensor_id = 0;
    uint32_t epoch_base_s = 0;
    uint32_t epoch_base_ms = 0;
    uint32_t last_nmea_ms = 0;
    bool enabled = true;
    bool powered = false;
    bool initialized = false;
    bool time_synced = false;
    bool nmea_seen = false;
} s_gps;

constexpr uint32_t kMinValidEpochSeconds = 1700000000UL;

uint32_t readSystemEpochSeconds()
{
    const time_t now = ::time(nullptr);
    if (now < static_cast<time_t>(kMinValidEpochSeconds))
    {
        return 0;
    }
    return static_cast<uint32_t>(now);
}

void syncSystemClockFromEpoch(uint32_t epoch_s)
{
    (void)epoch_s;
}

uint8_t daysInMonth(int year, uint8_t month)
{
    static constexpr uint8_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 0 || month > 12)
    {
        return 31;
    }
    if (month != 2)
    {
        return kDays[month - 1];
    }
    const bool leap = ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
    return leap ? 29 : 28;
}

bool gpsDateTimeValid(int year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    if (year < 2020 || year > 2100)
    {
        return false;
    }
    if (month < 1 || month > 12)
    {
        return false;
    }
    const uint8_t max_day = daysInMonth(year, month);
    if (day < 1 || day > max_day)
    {
        return false;
    }
    if (hour >= 24 || minute >= 60 || second >= 60)
    {
        return false;
    }
    return true;
}

int64_t daysFromCivil(int year, unsigned month, unsigned day)
{
    year -= month <= 2 ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? static_cast<unsigned>(-3) : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

time_t gpsDateTimeToEpochUtc(int year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    const int64_t days = daysFromCivil(year, month, day);
    const int64_t sec_of_day =
        static_cast<int64_t>(hour) * 3600 + static_cast<int64_t>(minute) * 60 + static_cast<int64_t>(second);
    const int64_t epoch64 = days * 86400 + sec_of_day;
    if (epoch64 < 0 || epoch64 > static_cast<int64_t>(std::numeric_limits<time_t>::max()))
    {
        return static_cast<time_t>(-1);
    }
    return static_cast<time_t>(epoch64);
}

bool updateDebounced(bool sampled,
                     DebounceState& state,
                     uint16_t debounce_ms,
                     BoardInputKey key,
                     BoardInputEvent* out_event,
                     uint32_t now_ms)
{
    if (sampled != state.sampled)
    {
        state.sampled = sampled;
        state.changed_at_ms = now_ms;
    }

    if (state.stable == state.sampled)
    {
        return false;
    }

    if ((now_ms - state.changed_at_ms) < debounce_ms)
    {
        return false;
    }

    state.stable = state.sampled;
    if (out_event)
    {
        out_event->key = key;
        out_event->pressed = state.stable;
        out_event->timestamp_ms = now_ms;
    }
    if (state.stable)
    {
        s_input.last_activity_ms = now_ms;
    }
    return true;
}

void applyGpsTimeIfValid()
{
    if (!s_gps.parser.time.isValid() || !s_gps.parser.date.isValid())
    {
        return;
    }

    const uint16_t year = s_gps.parser.date.year();
    const uint8_t month = s_gps.parser.date.month();
    const uint8_t day = s_gps.parser.date.day();
    const uint8_t hour = s_gps.parser.time.hour();
    const uint8_t minute = s_gps.parser.time.minute();
    const uint8_t second = s_gps.parser.time.second();

    if (!gpsDateTimeValid(year, month, day, hour, minute, second))
    {
        return;
    }

    const time_t utc = gpsDateTimeToEpochUtc(year, month, day, hour, minute, second);
    if (utc < static_cast<time_t>(kMinValidEpochSeconds))
    {
        return;
    }

    const uint32_t utc_s = static_cast<uint32_t>(utc);
    if (s_gps.epoch_base_s == utc_s)
    {
        return;
    }
    s_gps.epoch_base_s = utc_s;
    s_gps.epoch_base_ms = millis();
    s_gps.time_synced = true;
    syncSystemClockFromEpoch(utc_s);
}

void refreshGpsFix()
{
    s_gps.data.valid = s_gps.parser.location.isValid();
    s_gps.data.lat = s_gps.parser.location.lat();
    s_gps.data.lng = s_gps.parser.location.lng();
    s_gps.data.has_alt = s_gps.parser.altitude.isValid();
    s_gps.data.alt_m = s_gps.data.has_alt ? s_gps.parser.altitude.meters() : 0.0;
    s_gps.data.has_speed = s_gps.parser.speed.isValid();
    s_gps.data.speed_mps = s_gps.data.has_speed ? (s_gps.parser.speed.kmph() / 3.6) : 0.0;
    s_gps.data.has_course = s_gps.parser.course.isValid();
    s_gps.data.course_deg = s_gps.data.has_course ? s_gps.parser.course.deg() : 0.0;
    s_gps.data.satellites = static_cast<uint8_t>(
        std::min<uint32_t>(s_gps.parser.satellites.isValid() ? s_gps.parser.satellites.value() : 0U, 255U));
    s_gps.data.age = s_gps.parser.location.isValid()
                         ? static_cast<uint32_t>(s_gps.parser.location.age())
                         : 0xFFFFFFFFUL;

    s_gps.status.sats_in_use = s_gps.data.satellites;
    s_gps.status.sats_in_view = s_gps.data.satellites;
    s_gps.status.hdop = s_gps.parser.hdop.isValid()
                            ? static_cast<float>(s_gps.parser.hdop.hdop())
                            : 0.0f;
    s_gps.status.fix = s_gps.data.valid
                           ? (s_gps.data.has_alt ? ::gps::GnssFix::FIX3D : ::gps::GnssFix::FIX2D)
                           : ::gps::GnssFix::NOFIX;

    if (s_gps.data.valid)
    {
        s_gps.last_motion_ms = millis();
    }
}

} // namespace

Gat562Board& Gat562Board::instance()
{
    static Gat562Board board_instance;
    return board_instance;
}

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
}

void Gat562Board::handlePowerButton()
{
}

void Gat562Board::softwareShutdown()
{
    NVIC_SystemReset();
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
    return s_gps.time_synced && currentEpochSeconds() >= kMinValidEpochSeconds;
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
}

void Gat562Board::playMessageTone()
{
    pulseNotificationLed(25);
}

void Gat562Board::setMessageToneVolume(uint8_t volume_percent)
{
    message_tone_volume_ = volume_percent;
    (void)::boards::gat562_mesh_evb_pro::settings_store::saveMessageToneVolume(volume_percent);
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
    if (!out_snapshot)
    {
        return false;
    }

    const auto& inputs = kBoardProfile.inputs;
    BoardInputSnapshot snapshot{};
    snapshot.button_primary = readActiveLowPin(inputs.button_primary, inputs.buttons_need_pullup);
    snapshot.button_secondary = readActiveLowPin(inputs.button_secondary, inputs.buttons_need_pullup);
    snapshot.joystick_up = readActiveLowPin(inputs.joystick_up, inputs.joystick_need_pullup);
    snapshot.joystick_down = readActiveLowPin(inputs.joystick_down, inputs.joystick_need_pullup);
    snapshot.joystick_left = readActiveLowPin(inputs.joystick_left, inputs.joystick_need_pullup);
    snapshot.joystick_right = readActiveLowPin(inputs.joystick_right, inputs.joystick_need_pullup);
    snapshot.joystick_press = readActiveLowPin(inputs.joystick_press, inputs.joystick_need_pullup);
    snapshot.any_activity = snapshot.button_primary || snapshot.button_secondary ||
                            snapshot.joystick_up || snapshot.joystick_down ||
                            snapshot.joystick_left || snapshot.joystick_right ||
                            snapshot.joystick_press;

    *out_snapshot = snapshot;
    return snapshot.any_activity;
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
    return kBoardProfile.inputs.debounce_ms;
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
    if (out_event)
    {
        *out_event = BoardInputEvent{};
    }

    BoardInputSnapshot current{};
    (void)pollInputSnapshot(&current);
    s_input.snapshot = current;

    const uint32_t now_ms = millis();
    const uint16_t debounce_ms = inputDebounceMs();

    return updateDebounced(current.button_primary, s_input.button_primary, debounce_ms,
                           BoardInputKey::PrimaryButton, out_event, now_ms) ||
           updateDebounced(current.button_secondary, s_input.button_secondary, debounce_ms,
                           BoardInputKey::SecondaryButton, out_event, now_ms) ||
           updateDebounced(current.joystick_up, s_input.joystick_up, debounce_ms,
                           BoardInputKey::JoystickUp, out_event, now_ms) ||
           updateDebounced(current.joystick_down, s_input.joystick_down, debounce_ms,
                           BoardInputKey::JoystickDown, out_event, now_ms) ||
           updateDebounced(current.joystick_left, s_input.joystick_left, debounce_ms,
                           BoardInputKey::JoystickLeft, out_event, now_ms) ||
           updateDebounced(current.joystick_right, s_input.joystick_right, debounce_ms,
                           BoardInputKey::JoystickRight, out_event, now_ms) ||
           updateDebounced(current.joystick_press, s_input.joystick_press, debounce_ms,
                           BoardInputKey::JoystickPress, out_event, now_ms);
}

namespace
{

class Ssd1306MonoDisplay final : public ::ui::mono_128x64::MonoDisplay
{
  public:
    Ssd1306MonoDisplay()
        : display_(SCREEN_WIDTH,
                   SCREEN_HEIGHT,
                   &::boards::gat562_mesh_evb_pro::Gat562Board::instance().i2cWire(),
                   -1)
    {
    }

    bool begin() override;
    int width() const override { return SCREEN_WIDTH; }
    int height() const override { return SCREEN_HEIGHT; }
    int charWidth(::ui::mono_128x64::FontSize size) const override
    {
        return size == ::ui::mono_128x64::FontSize::Large ? 12 : 6;
    }
    int lineHeight(::ui::mono_128x64::FontSize size) const override
    {
        return size == ::ui::mono_128x64::FontSize::Large ? 16 : 8;
    }
    void clear() override
    {
        if (online_)
        {
            display_.clearDisplay();
        }
    }
    void drawText(int x, int y, const char* text, ::ui::mono_128x64::FontSize size, bool inverse = false) override
    {
        if (!online_ || !text)
        {
            return;
        }
        display_.setTextSize(size == ::ui::mono_128x64::FontSize::Large ? 2 : 1);
        display_.setTextColor(inverse ? SSD1306_BLACK : SSD1306_WHITE,
                              inverse ? SSD1306_WHITE : SSD1306_BLACK);
        display_.setCursor(x, y);
        display_.print(text);
        display_.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
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
    if (!beginGps(config))
    {
        return false;
    }
    applyGpsConfig(config);
    return true;
}

bool Gat562Board::beginGps(const app::AppConfig& config)
{
    (void)config;
    if (!s_gps.initialized)
    {
        const auto& profile = kBoardProfile;
        if (profile.gps.uart.aux >= 0)
        {
            pinMode(profile.gps.uart.aux, OUTPUT);
            digitalWrite(profile.gps.uart.aux, HIGH);
        }
        Serial1.setPins(profile.gps.uart.rx, profile.gps.uart.tx);
        Serial1.begin(profile.gps.baud_rate);
        s_gps.initialized = true;
        s_gps.powered = true;
    }
    return true;
}

void Gat562Board::applyGpsConfig(const app::AppConfig& config)
{
    s_gps.collection_interval_ms = config.gps_interval_ms;
    s_gps.power_strategy = config.gps_strategy;
    s_gps.gnss_mode = config.gps_mode;
    s_gps.sat_mask = config.gps_sat_mask;
    s_gps.nmea_output_hz = config.privacy_nmea_output;
    s_gps.nmea_sentence_mask = config.privacy_nmea_sentence;
    s_gps.motion_idle_timeout_ms = config.motion_config.idle_timeout_ms;
    s_gps.motion_sensor_id = config.motion_config.sensor_id;
    s_gps.enabled = true;
}

void Gat562Board::tickGps()
{
    if (!s_gps.initialized || !s_gps.enabled)
    {
        return;
    }

    while (Serial1.available() > 0)
    {
        s_gps.nmea_seen = true;
        s_gps.last_nmea_ms = millis();
        s_gps.parser.encode(static_cast<char>(Serial1.read()));
    }
    applyGpsTimeIfValid();
    refreshGpsFix();
}

bool Gat562Board::isGpsRuntimeReady() const
{
    return s_gps.initialized && s_gps.powered;
}

::gps::GpsState Gat562Board::gpsData() const
{
    return s_gps.data;
}

bool Gat562Board::gpsEnabled() const
{
    return s_gps.enabled;
}

bool Gat562Board::gpsPowered() const
{
    return s_gps.powered;
}

uint32_t Gat562Board::gpsLastMotionMs() const
{
    return s_gps.last_motion_ms;
}

bool Gat562Board::gpsGnssSnapshot(::gps::GnssSatInfo* out,
                                  std::size_t max,
                                  std::size_t* out_count,
                                  ::gps::GnssStatus* status) const
{
    if (out_count)
    {
        *out_count = 0;
    }
    if (status)
    {
        *status = s_gps.status;
    }
    if (out && max > 0 && s_gps.data.valid)
    {
        out[0].id = 0;
        out[0].sys = ::gps::GnssSystem::GPS;
        out[0].snr = -1;
        out[0].used = true;
        if (out_count)
        {
            *out_count = 1;
        }
        return true;
    }
    return s_gps.data.valid;
}

void Gat562Board::setGpsCollectionInterval(uint32_t interval_ms) { s_gps.collection_interval_ms = interval_ms; }
void Gat562Board::setGpsPowerStrategy(uint8_t strategy) { s_gps.power_strategy = strategy; }
void Gat562Board::setGpsConfig(uint8_t mode, uint8_t sat_mask)
{
    s_gps.gnss_mode = mode;
    s_gps.sat_mask = sat_mask;
}
void Gat562Board::setGpsNmeaConfig(uint8_t output_hz, uint8_t sentence_mask)
{
    s_gps.nmea_output_hz = output_hz;
    s_gps.nmea_sentence_mask = sentence_mask;
}
void Gat562Board::setGpsMotionIdleTimeout(uint32_t timeout_ms) { s_gps.motion_idle_timeout_ms = timeout_ms; }
void Gat562Board::setGpsMotionSensorId(uint8_t sensor_id) { s_gps.motion_sensor_id = sensor_id; }
void Gat562Board::suspendGps() { s_gps.enabled = false; }
void Gat562Board::resumeGps() { s_gps.enabled = true; }
void Gat562Board::setCurrentEpochSeconds(uint32_t epoch_s)
{
    if (epoch_s < kMinValidEpochSeconds)
    {
        return;
    }

    s_gps.epoch_base_s = epoch_s;
    s_gps.epoch_base_ms = millis();
    s_gps.time_synced = true;
    syncSystemClockFromEpoch(epoch_s);
}

uint32_t Gat562Board::currentEpochSeconds() const
{
    const uint32_t system_epoch_s = readSystemEpochSeconds();
    if (system_epoch_s >= kMinValidEpochSeconds)
    {
        return system_epoch_s;
    }

    if (!s_gps.time_synced || s_gps.epoch_base_s == 0)
    {
        return 0;
    }
    const uint32_t elapsed_s = (millis() - s_gps.epoch_base_ms) / 1000U;
    return s_gps.epoch_base_s + elapsed_s;
}

} // namespace boards::gat562_mesh_evb_pro

BoardBase& board = ::boards::gat562_mesh_evb_pro::Gat562Board::instance();
