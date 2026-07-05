#include "boards/t_impulse_plus/t_impulse_plus_board.h"

#include "boards/t_impulse_plus/board_profile.h"
#include "boards/t_impulse_plus/gps_runtime.h"
#include "boards/t_impulse_plus/input_runtime.h"
#include "boards/t_impulse_plus/settings_store.h"
#include "boards/t_impulse_plus/sx1262_radio_packet_io.h"
#include "platform/nrf52/arduino_common/chat/infra/radio_packet_io.h"

#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>

#include <algorithm>
#include <cstdio>

namespace boards::t_impulse_plus
{
namespace
{

void writePin(int pin, bool active_high, bool on)
{
    if (pin < 0)
    {
        return;
    }
    pinMode(pin, OUTPUT);
    digitalWrite(pin, (on == active_high) ? HIGH : LOW);
}

Adafruit_SSD1306& oledDisplay()
{
    const auto& profile = kBoardProfile.display;
    static Adafruit_SSD1306 display(profile.physical_width,
                                    profile.physical_height,
                                    &::boards::t_impulse_plus::TImpulsePlusBoard::instance().i2cWire(),
                                    profile.reset);
    return display;
}

int logicalToPhysicalX(int x)
{
    return kBoardProfile.display.visible_offset_x + x;
}

int logicalToPhysicalY(int y)
{
    return kBoardProfile.display.visible_offset_y + y;
}

} // namespace

TImpulsePlusBoard& TImpulsePlusBoard::instance()
{
    static TImpulsePlusBoard board_instance;
    return board_instance;
}

TImpulsePlusBoard::TImpulsePlusBoard()
    : gps_runtime_(new GpsRuntime()),
      input_runtime_(new InputRuntime())
{
}

TImpulsePlusBoard::~TImpulsePlusBoard() = default;

uint32_t TImpulsePlusBoard::begin(uint32_t disable_hw_init)
{
    (void)disable_hw_init;
    if (initialized_)
    {
        return 1U;
    }

    initializeBoardHardware();
    ensureI2cReady();
    message_tone_volume_ = ::boards::t_impulse_plus::settings_store::loadMessageToneVolume();
    initialized_ = true;
    Serial.printf("[t-impulse-plus] board begin ok\n");
    return 1U;
}

void TImpulsePlusBoard::initializeBoardHardware()
{
    enablePeripheralRail();

    const auto& profile = kBoardProfile;
    writePin(profile.leds.status, profile.leds.active_high, false);
    if (profile.input.function_touch >= 0)
    {
        pinMode(profile.input.function_touch, profile.input.use_pullup ? INPUT_PULLUP : INPUT);
    }
    if (profile.vibration_motor >= 0)
    {
        writePin(profile.vibration_motor, true, false);
    }
    if (profile.battery.measurement_enable >= 0)
    {
        writePin(profile.battery.measurement_enable,
                 profile.battery.measurement_enable_active_high,
                 false);
    }
}

void TImpulsePlusBoard::enablePeripheralRail()
{
    if (peripheral_rail_enabled_)
    {
        return;
    }

    if (kBoardProfile.peripheral_3v3_enable >= 0)
    {
        pinMode(kBoardProfile.peripheral_3v3_enable, OUTPUT);
        digitalWrite(kBoardProfile.peripheral_3v3_enable, HIGH);
        delay(10);
    }
    peripheral_rail_enabled_ = true;
}

void TImpulsePlusBoard::wakeUp()
{
    enablePeripheralRail();
    setStatusLed(false);
}

void TImpulsePlusBoard::handlePowerButton()
{
    vibrator();
}

void TImpulsePlusBoard::softwareShutdown()
{
    NVIC_SystemReset();
}

int TImpulsePlusBoard::getPowerTier() const
{
    const int level = const_cast<TImpulsePlusBoard*>(this)->readBatteryPercent();
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

void TImpulsePlusBoard::setBrightness(uint8_t level)
{
    brightness_ = static_cast<uint8_t>(
        std::clamp<int>(level, DEVICE_MIN_BRIGHTNESS_LEVEL, DEVICE_MAX_BRIGHTNESS_LEVEL));
}

uint8_t TImpulsePlusBoard::getBrightness()
{
    return brightness_;
}

bool TImpulsePlusBoard::hasKeyboard()
{
    return false;
}

void TImpulsePlusBoard::keyboardSetBrightness(uint8_t level)
{
    keyboard_brightness_ = level;
}

uint8_t TImpulsePlusBoard::keyboardGetBrightness()
{
    return keyboard_brightness_;
}

bool TImpulsePlusBoard::isRTCReady() const
{
    return gps_runtime_ ? gps_runtime_->isRtcReady() : false;
}

bool TImpulsePlusBoard::isCharging()
{
    return false;
}

int TImpulsePlusBoard::readBatteryPercent() const
{
    const auto& battery = kBoardProfile.battery;
    if (battery.adc_pin < 0)
    {
        return -1;
    }

    if (battery.measurement_enable >= 0)
    {
        writePin(battery.measurement_enable, battery.measurement_enable_active_high, true);
        delay(3);
    }

    analogReference(AR_INTERNAL_3_0);
    analogReadResolution(battery.adc_resolution_bits);
    const int raw = analogRead(battery.adc_pin);

    if (battery.measurement_enable >= 0)
    {
        writePin(battery.measurement_enable, battery.measurement_enable_active_high, false);
    }
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

int TImpulsePlusBoard::getBatteryLevel()
{
    return readBatteryPercent();
}

bool TImpulsePlusBoard::isSDReady() const
{
    return false;
}

bool TImpulsePlusBoard::isCardReady()
{
    return false;
}

bool TImpulsePlusBoard::isGPSReady() const
{
    return isGpsRuntimeReady();
}

void TImpulsePlusBoard::setVibration(bool on)
{
    writePin(kBoardProfile.vibration_motor, true, on);
}

void TImpulsePlusBoard::vibrator()
{
    setVibration(true);
    delay(30);
    setVibration(false);
}

void TImpulsePlusBoard::stopVibrator()
{
    setVibration(false);
}

void TImpulsePlusBoard::playMessageTone()
{
    if (message_tone_volume_ > 0)
    {
        vibrator();
    }
}

void TImpulsePlusBoard::setMessageToneVolume(uint8_t volume_percent)
{
    message_tone_volume_ = static_cast<uint8_t>(std::min<unsigned>(volume_percent, 100U));
    ::boards::t_impulse_plus::settings_store::queueSaveMessageToneVolume(message_tone_volume_);
}

uint8_t TImpulsePlusBoard::getMessageToneVolume() const
{
    return message_tone_volume_;
}

void TImpulsePlusBoard::setStatusLed(bool on)
{
    writePin(kBoardProfile.leds.status, kBoardProfile.leds.active_high, on);
}

bool TImpulsePlusBoard::pollInputSnapshot(BoardInputSnapshot* out_snapshot) const
{
    return input_runtime_ ? input_runtime_->pollSnapshot(out_snapshot) : false;
}

bool TImpulsePlusBoard::pollInputEvent(BoardInputEvent* out_event)
{
    return input_runtime_ ? input_runtime_->pollEvent(out_event) : false;
}

uint16_t TImpulsePlusBoard::inputDebounceMs() const
{
    return input_runtime_ ? input_runtime_->debounceMs() : kBoardProfile.input.debounce_ms;
}

uint32_t TImpulsePlusBoard::inputLongPressMs() const
{
    return kBoardProfile.input.long_press_ms;
}

bool TImpulsePlusBoard::ensureI2cReady()
{
    if (i2c_initialized_)
    {
        return true;
    }

    const auto& i2c = kBoardProfile.display.i2c;
    Wire.setPins(i2c.sda, i2c.scl);
    Wire.begin();
    Wire.setClock(400000);
    i2c_initialized_ = true;
    return true;
}

bool TImpulsePlusBoard::lockI2c(uint32_t timeout_ms)
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

void TImpulsePlusBoard::unlockI2c()
{
    noInterrupts();
    i2c_locked_ = false;
    interrupts();
}

TwoWire& TImpulsePlusBoard::i2cWire()
{
    (void)ensureI2cReady();
    return Wire;
}

bool TImpulsePlusBoard::beginDisplay()
{
    if (display_initialized_)
    {
        return display_online_;
    }

    display_initialized_ = true;
    I2cGuard guard(*this, 200);
    if (!guard)
    {
        Serial.printf("[t-impulse-plus][display] i2c lock failed\n");
        return false;
    }

    display_online_ = oledDisplay().begin(SSD1306_SWITCHCAPVCC, kBoardProfile.display.i2c.address, true, false);
    if (display_online_)
    {
        oledDisplay().clearDisplay();
        oledDisplay().setTextWrap(false);
        oledDisplay().display();
    }
    Serial.printf("[t-impulse-plus][display] begin %s addr=0x%02X logical=%dx%d offset=%d,%d\n",
                  display_online_ ? "ok" : "fail",
                  static_cast<unsigned>(kBoardProfile.display.i2c.address),
                  kBoardProfile.display.logical_width,
                  kBoardProfile.display.logical_height,
                  kBoardProfile.display.visible_offset_x,
                  kBoardProfile.display.visible_offset_y);
    return display_online_;
}

bool TImpulsePlusBoard::displayReady() const
{
    return display_online_;
}

int TImpulsePlusBoard::displayWidth() const
{
    return kBoardProfile.display.logical_width;
}

int TImpulsePlusBoard::displayHeight() const
{
    return kBoardProfile.display.logical_height;
}

void TImpulsePlusBoard::clearDisplay()
{
    if (!display_online_)
    {
        return;
    }
    I2cGuard guard(*this, 100);
    if (!guard)
    {
        return;
    }
    oledDisplay().clearDisplay();
}

void TImpulsePlusBoard::drawDisplayText(int x, int y, const char* text, uint8_t size, bool inverse)
{
    if (!display_online_ || !text)
    {
        return;
    }
    I2cGuard guard(*this, 100);
    if (!guard)
    {
        return;
    }
    const int px = logicalToPhysicalX(x);
    const int py = logicalToPhysicalY(y);
    if (inverse)
    {
        oledDisplay().fillRect(px, py, kBoardProfile.display.logical_width - x, 8 * size, SSD1306_WHITE);
        oledDisplay().setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    }
    else
    {
        oledDisplay().setTextColor(SSD1306_WHITE);
    }
    oledDisplay().setTextSize(size);
    oledDisplay().setCursor(px, py);
    oledDisplay().print(text);
    oledDisplay().setTextColor(SSD1306_WHITE);
}

void TImpulsePlusBoard::drawDisplayFrame()
{
    if (!display_online_)
    {
        return;
    }
    I2cGuard guard(*this, 100);
    if (!guard)
    {
        return;
    }
    oledDisplay().drawRect(kBoardProfile.display.visible_offset_x,
                           kBoardProfile.display.visible_offset_y,
                           kBoardProfile.display.logical_width,
                           kBoardProfile.display.logical_height,
                           SSD1306_WHITE);
}

void TImpulsePlusBoard::presentDisplay()
{
    if (!display_online_)
    {
        return;
    }
    I2cGuard guard(*this, 100);
    if (guard)
    {
        oledDisplay().display();
    }
}

TImpulsePlusBoard::I2cGuard::I2cGuard(TImpulsePlusBoard& board, uint32_t timeout_ms)
    : board_(&board),
      locked_(board.ensureI2cReady() && board.lockI2c(timeout_ms))
{
}

TImpulsePlusBoard::I2cGuard::~I2cGuard()
{
    if (board_ && locked_)
    {
        board_->unlockI2c();
    }
}

bool TImpulsePlusBoard::I2cGuard::locked() const
{
    return locked_;
}

TImpulsePlusBoard::I2cGuard::operator bool() const
{
    return locked_;
}

const char* TImpulsePlusBoard::defaultLongName() const
{
    return kBoardProfile.identity.long_name;
}

const char* TImpulsePlusBoard::defaultShortName() const
{
    return kBoardProfile.identity.short_name;
}

const char* TImpulsePlusBoard::defaultBleName() const
{
    return kBoardProfile.identity.ble_name;
}

bool TImpulsePlusBoard::prepareRadioHardware()
{
    if (radio_hw_ready_)
    {
        return true;
    }

    (void)begin();
    enablePeripheralRail();

    const auto& lora = kBoardProfile.lora;
    pinMode(lora.spi.cs, OUTPUT);
    digitalWrite(lora.spi.cs, HIGH);
    if (lora.rf_vc1 >= 0)
    {
        pinMode(lora.rf_vc1, OUTPUT);
        digitalWrite(lora.rf_vc1, LOW);
    }
    if (lora.rf_vc2 >= 0)
    {
        pinMode(lora.rf_vc2, OUTPUT);
        digitalWrite(lora.rf_vc2, HIGH);
    }

    radio_hw_ready_ = true;
    return true;
}

bool TImpulsePlusBoard::beginRadioIo()
{
    return ::boards::t_impulse_plus::sx1262RadioPacketIo().begin();
}

platform::nrf52::arduino_common::chat::infra::IRadioPacketIo* TImpulsePlusBoard::bindRadioIo()
{
    auto& io = ::boards::t_impulse_plus::sx1262RadioPacketIo();
    ::platform::nrf52::arduino_common::chat::infra::bindRadioPacketIo(&io);
    return &io;
}

void TImpulsePlusBoard::applyRadioConfig(chat::MeshProtocol protocol, const chat::MeshConfig& config)
{
    ::boards::t_impulse_plus::sx1262RadioPacketIo().applyConfig(protocol, config);
}

uint32_t TImpulsePlusBoard::activeLoraFrequencyHz() const
{
    return ::boards::t_impulse_plus::sx1262RadioPacketIo().appliedFrequencyHz();
}

bool TImpulsePlusBoard::formatLoraFrequencyMHz(uint32_t freq_hz, char* out, std::size_t out_len) const
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

bool TImpulsePlusBoard::startGpsRuntime(const app::AppConfig& config)
{
    return gps_runtime_ ? gps_runtime_->start(config) : false;
}

bool TImpulsePlusBoard::beginGps(const app::AppConfig& config)
{
    return gps_runtime_ ? gps_runtime_->begin(config) : false;
}

void TImpulsePlusBoard::applyGpsConfig(const app::AppConfig& config)
{
    if (gps_runtime_)
    {
        gps_runtime_->applyConfig(config);
    }
}

void TImpulsePlusBoard::tickGps()
{
    if (gps_runtime_)
    {
        gps_runtime_->tick();
    }
}

bool TImpulsePlusBoard::isGpsRuntimeReady() const
{
    return gps_runtime_ ? gps_runtime_->isReady() : false;
}

::gps::GpsState TImpulsePlusBoard::gpsData() const
{
    return gps_runtime_ ? gps_runtime_->data() : ::gps::GpsState{};
}

bool TImpulsePlusBoard::gpsEnabled() const
{
    return gps_runtime_ ? gps_runtime_->enabled() : false;
}

bool TImpulsePlusBoard::gpsPowered() const
{
    return gps_runtime_ ? gps_runtime_->powered() : false;
}

uint32_t TImpulsePlusBoard::gpsLastMotionMs() const
{
    return gps_runtime_ ? gps_runtime_->lastMotionMs() : 0;
}

bool TImpulsePlusBoard::gpsGnssSnapshot(::gps::GnssSatInfo* out,
                                        std::size_t max,
                                        std::size_t* out_count,
                                        ::gps::GnssStatus* status) const
{
    return gps_runtime_ ? gps_runtime_->gnssSnapshot(out, max, out_count, status) : false;
}

bool TImpulsePlusBoard::debugCheckGpsMemoryGuard(const char* reason)
{
    return gps_runtime_ ? gps_runtime_->debugCheckMemoryGuard(reason) : true;
}

void TImpulsePlusBoard::setGpsCollectionInterval(uint32_t interval_ms)
{
    if (gps_runtime_)
    {
        gps_runtime_->setCollectionInterval(interval_ms);
    }
}

void TImpulsePlusBoard::setGpsEnabled(bool enabled)
{
    if (gps_runtime_)
    {
        gps_runtime_->setEnabled(enabled);
    }
}

void TImpulsePlusBoard::setGpsPowerStrategy(uint8_t strategy)
{
    if (gps_runtime_)
    {
        gps_runtime_->setPowerStrategy(strategy);
    }
}

void TImpulsePlusBoard::setGpsConfig(uint8_t mode, uint8_t sat_mask)
{
    if (gps_runtime_)
    {
        gps_runtime_->setConfig(mode, sat_mask);
    }
}

void TImpulsePlusBoard::setGpsExternalNmeaConfig(uint8_t output_hz, uint8_t sentence_mask)
{
    if (gps_runtime_)
    {
        gps_runtime_->setExternalNmeaConfig(output_hz, sentence_mask);
    }
}

void TImpulsePlusBoard::setGpsMotionIdleTimeout(uint32_t timeout_ms)
{
    if (gps_runtime_)
    {
        gps_runtime_->setMotionIdleTimeout(timeout_ms);
    }
}

void TImpulsePlusBoard::setGpsMotionSensorId(uint8_t sensor_id)
{
    if (gps_runtime_)
    {
        gps_runtime_->setMotionSensorId(sensor_id);
    }
}

void TImpulsePlusBoard::suspendGps()
{
    if (gps_runtime_)
    {
        gps_runtime_->suspend();
    }
}

void TImpulsePlusBoard::resumeGps()
{
    if (gps_runtime_)
    {
        gps_runtime_->resume();
    }
}

void TImpulsePlusBoard::setCurrentEpochSeconds(uint32_t epoch_s)
{
    if (gps_runtime_)
    {
        gps_runtime_->setCurrentEpochSeconds(epoch_s);
    }
}

uint32_t TImpulsePlusBoard::currentEpochSeconds() const
{
    return gps_runtime_ ? gps_runtime_->currentEpochSeconds() : 0;
}

} // namespace boards::t_impulse_plus

BoardBase& board = ::boards::t_impulse_plus::TImpulsePlusBoard::instance();
