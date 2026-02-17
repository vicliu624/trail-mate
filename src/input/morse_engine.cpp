#include "morse_engine.h"

#include "freertos/task.h"
#include <Arduino.h>
#include <algorithm>

namespace
{
struct MorseMap
{
    const char* code;
    char ch;
};

constexpr MorseMap kMorseMap[] = {
    {".-", 'A'},
    {"-...", 'B'},
    {"-.-.", 'C'},
    {"-..", 'D'},
    {".", 'E'},
    {"..-.", 'F'},
    {"--.", 'G'},
    {"....", 'H'},
    {"..", 'I'},
    {".---", 'J'},
    {"-.-", 'K'},
    {".-..", 'L'},
    {"--", 'M'},
    {"-.", 'N'},
    {"---", 'O'},
    {".--.", 'P'},
    {"--.-", 'Q'},
    {".-.", 'R'},
    {"...", 'S'},
    {"-", 'T'},
    {"..-", 'U'},
    {"...-", 'V'},
    {".--", 'W'},
    {"-..-", 'X'},
    {"-.--", 'Y'},
    {"--..", 'Z'},
    {"-----", '0'},
    {".----", '1'},
    {"..---", '2'},
    {"...--", '3'},
    {"....-", '4'},
    {".....", '5'},
    {"-....", '6'},
    {"--...", '7'},
    {"---..", '8'},
    {"----.", '9'},
};

char decode_morse(const std::string& symbol)
{
    if (symbol.empty())
    {
        return '\0';
    }
    for (const auto& entry : kMorseMap)
    {
        if (symbol == entry.code)
        {
            return entry.ch;
        }
    }
    return '?';
}

static volatile uint32_t g_touch_suppress_until_ms = 0;
static volatile uint32_t g_touch_suppress_ms = 150;

static bool is_touch_suppressed()
{
    uint32_t until = g_touch_suppress_until_ms;
    if (until == 0)
    {
        return false;
    }
    uint32_t now = millis();
    return static_cast<int32_t>(until - now) > 0;
}
} // namespace

namespace input
{

MorseEngine::MorseEngine()
{
    lock_ = xSemaphoreCreateMutex();
}

MorseEngine::~MorseEngine()
{
    stop();
    if (lock_)
    {
        vSemaphoreDelete(lock_);
        lock_ = nullptr;
    }
}

bool MorseEngine::start(const Config& config)
{
    if (running_)
    {
        return true;
    }
    if (config.pin_sck < 0 || config.pin_data < 0 || config.sample_rate == 0)
    {
        return false;
    }
    config_ = config;
    if (config_.dot_calib_target == 0)
    {
        config_.dot_calib_target = 1;
    }
    if (config_.touch_suppress_ms == 0)
    {
        config_.touch_suppress_ms = 150;
    }
    g_touch_suppress_ms = config_.touch_suppress_ms;
    if (config_.dc_shift < 4)
    {
        config_.dc_shift = 4;
    }
    if (config_.dc_shift > 12)
    {
        config_.dc_shift = 12;
    }
    if (config_.input_gain < 1)
    {
        config_.input_gain = 1;
    }
    if (config_.dash_max_mult < config_.dash_min_mult)
    {
        config_.dash_max_mult = config_.dash_min_mult;
    }
    if (config_.dma_buf_count <= 0)
    {
        config_.dma_buf_count = 4;
    }
    if (config_.dma_buf_len <= 0)
    {
        config_.dma_buf_len = 256;
    }

    reset_state();

    i2s_config_t cfg = {};
    cfg.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
    cfg.sample_rate = config_.sample_rate;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_PCM_SHORT;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = config_.dma_buf_count;
    cfg.dma_buf_len = config_.dma_buf_len;
    cfg.use_apll = true;
    cfg.tx_desc_auto_clear = false;
    cfg.fixed_mclk = 0;

    if (i2s_driver_install(config_.i2s_port, &cfg, 0, nullptr) != ESP_OK)
    {
        return false;
    }

    i2s_pin_config_t pins = {};
    pins.mck_io_num = I2S_PIN_NO_CHANGE;
    pins.bck_io_num = I2S_PIN_NO_CHANGE;
    pins.ws_io_num = config_.pin_sck;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = config_.pin_data;
    if (i2s_set_pin(config_.i2s_port, &pins) != ESP_OK)
    {
        i2s_driver_uninstall(config_.i2s_port);
        return false;
    }

#if SOC_I2S_SUPPORTS_PDM_RX
    i2s_set_pdm_rx_down_sample(config_.i2s_port, I2S_PDM_DSR_8S);
#endif

    running_ = true;
    BaseType_t ok = xTaskCreate(task_entry, "morse_mic", config_.task_stack, this, config_.task_priority, &task_);
    if (ok != pdPASS)
    {
        running_ = false;
        i2s_driver_uninstall(config_.i2s_port);
        task_ = nullptr;
        return false;
    }
    return true;
}

void MorseEngine::stop()
{
    if (!running_)
    {
        return;
    }
    running_ = false;
    for (int i = 0; i < 20; ++i)
    {
        if (!task_)
        {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (task_)
    {
        vTaskDelete(task_);
        task_ = nullptr;
    }
    i2s_driver_uninstall(config_.i2s_port);
}

bool MorseEngine::getSnapshot(Snapshot& out)
{
    out.level = level_;
    out.calibrated = calibrated_;
    out.in_pulse = in_pulse_;
    out.phase = calib_phase_;
    out.calib_index = static_cast<uint32_t>(calib_count_);
    if (calib_phase_ == CalibPhase::Dash)
    {
        out.calib_total = static_cast<uint32_t>(dash_calib_samples_.size());
    }
    else if (calib_phase_ == CalibPhase::Dot)
    {
        out.calib_total = static_cast<uint32_t>(dot_calib_samples_.size());
    }
    else
    {
        out.calib_total = 0;
    }
    if (!lock_ || xSemaphoreTake(lock_, 0) != pdTRUE)
    {
        return false;
    }
    out.status = status_;
    out.symbol = current_symbol_;
    out.text = decoded_text_;
    xSemaphoreGive(lock_);
    return true;
}

bool MorseEngine::consumeSend(std::string& text)
{
    if (!send_pending_)
    {
        return false;
    }
    if (!lock_ || xSemaphoreTake(lock_, 0) != pdTRUE)
    {
        return false;
    }
    text = send_text_;
    send_text_.clear();
    send_pending_ = false;
    xSemaphoreGive(lock_);
    return !text.empty();
}

void MorseEngine::task_entry(void* arg)
{
    auto* self = static_cast<MorseEngine*>(arg);
    if (!self)
    {
        vTaskDelete(nullptr);
        return;
    }
    self->taskLoop();
    self->task_ = nullptr;
    vTaskDelete(nullptr);
}

void MorseEngine::taskLoop()
{
    std::vector<int16_t> buffer;
    buffer.resize(static_cast<size_t>(config_.dma_buf_len));
    while (running_)
    {
        size_t bytes_read = 0;
        esp_err_t err = i2s_read(config_.i2s_port, buffer.data(), buffer.size() * sizeof(int16_t), &bytes_read,
                                 pdMS_TO_TICKS(50));
        if (err != ESP_OK || bytes_read == 0)
        {
            continue;
        }
        size_t samples_read = bytes_read / sizeof(int16_t);
        processSamples(buffer.data(), samples_read);
    }
}

void MorseEngine::notifyTouch(uint32_t suppress_ms)
{
    if (suppress_ms == 0)
    {
        suppress_ms = g_touch_suppress_ms;
    }
    if (suppress_ms == 0)
    {
        return;
    }
    uint32_t until = millis() + suppress_ms;
    uint32_t prev = g_touch_suppress_until_ms;
    if (static_cast<int32_t>(until - prev) > 0)
    {
        g_touch_suppress_until_ms = until;
    }
}

void MorseEngine::reset_state()
{
    sample_cursor_ = 0;
    env_ = 0;
    noise_ = 0;
    dc_ = 0;
    max_env_ = 1;
    level_ = 0;
    signal_on_ = false;
    in_pulse_ = false;
    calibrated_ = false;
    last_on_start_ = 0;
    last_off_start_ = 0;
    ignore_until_ = 0;
    last_activity_ = 0;
    dot_len_samples_ = 0;
    dash_len_samples_ = 0;
    dash_threshold_samples_ = 0;
    dash_min_samples_ = 0;
    dash_max_samples_ = 0;
    refractory_samples_ = (config_.sample_rate * config_.refractory_ms) / 1000;
    release_samples_ = (config_.sample_rate * config_.release_ms) / 1000;
    if (release_samples_ < 1)
    {
        release_samples_ = 1;
    }
    low_run_samples_ = 0;
    idle_samples_ = (config_.sample_rate * config_.idle_send_ms) / 1000;
    log_interval_samples_ = (config_.sample_rate * config_.log_interval_ms) / 1000;
    next_log_sample_ = log_interval_samples_;
    log_peak_ = 0;
    log_raw_min_ = 32767;
    log_raw_max_ = -32768;
    calib_phase_ = CalibPhase::Dot;
    calib_count_ = 0;
    dot_calib_samples_.assign(config_.dot_calib_target, 0);
    dash_calib_samples_.assign(config_.dash_calib_target, 0);
    send_pending_ = false;
    if (lock_)
    {
        if (xSemaphoreTake(lock_, pdMS_TO_TICKS(20)) == pdTRUE)
        {
            status_ = std::string("CALIB DOT 0/") + std::to_string(config_.dot_calib_target);
            current_symbol_.clear();
            decoded_text_.clear();
            send_text_.clear();
            xSemaphoreGive(lock_);
        }
    }
}

void MorseEngine::setStatus(const char* status)
{
    if (!lock_ || !status)
    {
        return;
    }
    if (xSemaphoreTake(lock_, 0) != pdTRUE)
    {
        return;
    }
    status_ = status;
    xSemaphoreGive(lock_);
}

void MorseEngine::update_level(int32_t env)
{
    int32_t signal = env - noise_;
    if (signal < 0)
    {
        signal = 0;
    }
    int32_t floor = config_.level_gate;
    if (floor <= 0)
    {
        floor = config_.min_low / 2;
        if (floor < 10)
        {
            floor = 10;
        }
    }
    if (signal < floor)
    {
        signal = 0;
    }
    if (signal > max_env_)
    {
        max_env_ = signal;
    }
    else
    {
        max_env_ -= max_env_ >> 6;
        if (max_env_ < 100)
        {
            max_env_ = 100;
        }
    }
    int lvl = static_cast<int>((signal * 100) / max_env_);
    if (lvl > 100)
    {
        lvl = 100;
    }
    if (lvl < 0)
    {
        lvl = 0;
    }
    level_ = lvl;
}

void MorseEngine::processSamples(const int16_t* samples, size_t count)
{
    int32_t last_th_high = 0;
    int32_t last_th_low = 0;
    bool suppressed = is_touch_suppressed();
    for (size_t i = 0; i < count; ++i)
    {
        ++sample_cursor_;
        int32_t raw = samples[i];
        dc_ += (raw - dc_) >> config_.dc_shift;
        int32_t v = raw - dc_;
        if (v < log_raw_min_)
        {
            log_raw_min_ = v;
        }
        if (v > log_raw_max_)
        {
            log_raw_max_ = v;
        }
        if (v < 0)
        {
            v = -v;
        }
        if (config_.input_gain > 1)
        {
            v *= config_.input_gain;
            if (v > 32767)
            {
                v = 32767;
            }
        }
        if (v > log_peak_)
        {
            log_peak_ = v;
        }
        env_ += (v - env_) >> 7;
        if (suppressed)
        {
            if (signal_on_)
            {
                signal_on_ = false;
                in_pulse_ = false;
                last_off_start_ = sample_cursor_;
                ignore_until_ = sample_cursor_ + refractory_samples_;
                low_run_samples_ = 0;
            }
            noise_ += (env_ - noise_) >> 6;
            continue;
        }
        if (signal_on_)
        {
            noise_ += (env_ - noise_) >> 10;
        }
        else
        {
            noise_ += (env_ - noise_) >> 7;
        }

        int32_t high_offset = config_.min_high;
        int32_t low_offset = config_.min_low;
        if (!calibrated_)
        {
            high_offset = config_.min_high / 2;
            low_offset = config_.min_low / 2;
            if (high_offset < 60)
            {
                high_offset = 60;
            }
            if (low_offset < 30)
            {
                low_offset = 30;
            }
        }
        int32_t th_high = noise_ + high_offset;
        int32_t th_low = noise_ + low_offset;
        if (noise_ > high_offset)
        {
            th_high = noise_ + (noise_ >> 1);
        }
        if (noise_ > low_offset)
        {
            th_low = noise_ + (noise_ >> 2);
        }
        last_th_high = th_high;
        last_th_low = th_low;

        if (!signal_on_)
        {
            if (sample_cursor_ >= ignore_until_ && env_ >= th_high)
            {
                signal_on_ = true;
                in_pulse_ = true;
                uint64_t gap = sample_cursor_ - last_off_start_;
                if (calibrated_)
                {
                    handleGap(static_cast<uint32_t>(gap));
                    setStatus("ON");
                }
                last_on_start_ = sample_cursor_;
                // Only update activity on accepted pulses to allow symbol finalization.
            }
        }
        else
        {
            if (env_ <= th_low)
            {
                if (low_run_samples_ < release_samples_)
                {
                    low_run_samples_++;
                }
                if (low_run_samples_ >= release_samples_)
                {
                    signal_on_ = false;
                    in_pulse_ = false;
                    uint32_t on_dur = static_cast<uint32_t>(sample_cursor_ - last_on_start_);
                    bool accepted = handlePulseEnd(on_dur);
                    last_off_start_ = sample_cursor_;
                    ignore_until_ = sample_cursor_ + refractory_samples_;
                    if (accepted)
                    {
                        last_activity_ = sample_cursor_;
                    }
                    low_run_samples_ = 0;
                    if (calibrated_)
                    {
                        setStatus("GAP");
                    }
                }
            }
            else
            {
                low_run_samples_ = 0;
            }
        }

        if (!signal_on_ && calibrated_ && !send_pending_)
        {
            if (sample_cursor_ - last_activity_ >= idle_samples_)
            {
                finalizeSymbol(false);
                if (lock_ && xSemaphoreTake(lock_, 0) == pdTRUE)
                {
                    send_text_ = decoded_text_;
                    while (!send_text_.empty() && send_text_.back() == ' ')
                    {
                        send_text_.pop_back();
                    }
                    send_pending_ = !send_text_.empty();
                    xSemaphoreGive(lock_);
                }
                last_activity_ = sample_cursor_;
            }
        }

        if (log_interval_samples_ > 0 && sample_cursor_ >= next_log_sample_)
        {
            if (!config_.log_calib_only || !calibrated_)
            {
                const char* phase = "DONE";
                if (calib_phase_ == CalibPhase::Dot)
                {
                    phase = "DOT";
                }
                else if (calib_phase_ == CalibPhase::Dash)
                {
                    phase = "DASH";
                }
                Serial.printf("[Morse] env=%ld noise=%ld th_hi=%ld th_lo=%ld lvl=%d peak=%ld raw_min=%ld raw_max=%ld gain=%ld on=%d cal=%d phase=%s\n",
                              static_cast<long>(env_),
                              static_cast<long>(noise_),
                              static_cast<long>(last_th_high),
                              static_cast<long>(last_th_low),
                              level_,
                              static_cast<long>(log_peak_),
                              static_cast<long>(log_raw_min_),
                              static_cast<long>(log_raw_max_),
                              static_cast<long>(config_.input_gain),
                              signal_on_ ? 1 : 0,
                              calibrated_ ? 1 : 0,
                              phase);
            }
            next_log_sample_ = sample_cursor_ + log_interval_samples_;
            log_peak_ = 0;
            log_raw_min_ = 32767;
            log_raw_max_ = -32768;
        }
    }
    if (suppressed)
    {
        level_ = 0;
    }
    else
    {
        update_level(env_);
    }
}

bool MorseEngine::handlePulseEnd(uint32_t on_samples)
{
    if (!calibrated_)
    {
        uint32_t min_samples = (config_.sample_rate * config_.min_dot_ms) / 1000;
        uint32_t max_samples = (config_.sample_rate * config_.max_dot_ms) / 1000;
        if (calib_phase_ == CalibPhase::Dot)
        {
            if (on_samples >= min_samples && on_samples <= max_samples &&
                calib_count_ < dot_calib_samples_.size())
            {
                dot_calib_samples_[calib_count_++] = on_samples;
                if (config_.log_interval_ms > 0)
                {
                    uint32_t ms = (on_samples * 1000U) / config_.sample_rate;
                    Serial.printf("[Morse] calib dot: %lu samples (%lu ms)\n",
                                  static_cast<unsigned long>(on_samples),
                                  static_cast<unsigned long>(ms));
                }
                if (lock_ && xSemaphoreTake(lock_, 0) == pdTRUE)
                {
                    status_ = std::string("CALIB DOT ") + std::to_string(calib_count_) + "/" +
                              std::to_string(config_.dot_calib_target);
                    xSemaphoreGive(lock_);
                }
                if (calib_count_ == dot_calib_samples_.size())
                {
                    std::vector<uint32_t> sorted = dot_calib_samples_;
                    std::sort(sorted.begin(), sorted.end());
                    dot_len_samples_ = sorted[sorted.size() / 2];
                    dash_min_samples_ = dot_len_samples_ * config_.dash_min_mult;
                    dash_max_samples_ = dot_len_samples_ * config_.dash_max_mult;
                    if (dash_calib_samples_.empty())
                    {
                        dash_threshold_samples_ = dot_len_samples_ * 2;
                        calibrated_ = true;
                        calib_phase_ = CalibPhase::Done;
                        setStatus("LISTEN");
                        last_off_start_ = sample_cursor_;
                        if (config_.log_interval_ms > 0)
                        {
                            Serial.printf("[Morse] calib done: dot=%lu samples (%lu ms) dash_th=%lu samples\n",
                                          static_cast<unsigned long>(dot_len_samples_),
                                          static_cast<unsigned long>((dot_len_samples_ * 1000U) / config_.sample_rate),
                                          static_cast<unsigned long>(dash_threshold_samples_));
                        }
                    }
                    else
                    {
                        calib_phase_ = CalibPhase::Dash;
                        calib_count_ = 0;
                        if (lock_ && xSemaphoreTake(lock_, 0) == pdTRUE)
                        {
                            status_ = std::string("CALIB DASH 0/") +
                                      std::to_string(config_.dash_calib_target);
                            xSemaphoreGive(lock_);
                        }
                    }
                }
            }
        }
        else if (calib_phase_ == CalibPhase::Dash)
        {
            if (dash_min_samples_ == 0)
            {
                dash_min_samples_ = dot_len_samples_ * config_.dash_min_mult;
                dash_max_samples_ = dot_len_samples_ * config_.dash_max_mult;
            }
            if (on_samples >= dash_min_samples_ && on_samples <= dash_max_samples_ &&
                calib_count_ < dash_calib_samples_.size())
            {
                dash_calib_samples_[calib_count_++] = on_samples;
                if (config_.log_interval_ms > 0)
                {
                    uint32_t ms = (on_samples * 1000U) / config_.sample_rate;
                    Serial.printf("[Morse] calib dash: %lu samples (%lu ms)\n",
                                  static_cast<unsigned long>(on_samples),
                                  static_cast<unsigned long>(ms));
                }
                if (lock_ && xSemaphoreTake(lock_, 0) == pdTRUE)
                {
                    status_ = std::string("CALIB DASH ") + std::to_string(calib_count_) + "/" +
                              std::to_string(config_.dash_calib_target);
                    xSemaphoreGive(lock_);
                }
                if (calib_count_ == dash_calib_samples_.size())
                {
                    std::vector<uint32_t> sorted = dash_calib_samples_;
                    std::sort(sorted.begin(), sorted.end());
                    dash_len_samples_ = sorted[sorted.size() / 2];
                    dash_threshold_samples_ = (dot_len_samples_ + dash_len_samples_) / 2;
                    calibrated_ = true;
                    calib_phase_ = CalibPhase::Done;
                    setStatus("LISTEN");
                    last_off_start_ = sample_cursor_;
                    if (config_.log_interval_ms > 0)
                    {
                        Serial.printf("[Morse] calib done: dot=%lu samples (%lu ms) dash=%lu samples (%lu ms) th=%lu samples\n",
                                      static_cast<unsigned long>(dot_len_samples_),
                                      static_cast<unsigned long>((dot_len_samples_ * 1000U) / config_.sample_rate),
                                      static_cast<unsigned long>(dash_len_samples_),
                                      static_cast<unsigned long>((dash_len_samples_ * 1000U) / config_.sample_rate),
                                      static_cast<unsigned long>(dash_threshold_samples_));
                    }
                }
            }
        }
        return true;
    }

    if (dot_len_samples_ == 0)
    {
        return false;
    }
    uint32_t min_floor = (config_.sample_rate * config_.min_dot_ms) / 2000;
    if (min_floor < 5)
    {
        min_floor = 5;
    }
    uint32_t min_valid = (dot_len_samples_ * 4) / 10;
    if (min_valid < min_floor)
    {
        min_valid = min_floor;
    }
    if (on_samples < min_valid)
    {
        return false;
    }
    uint32_t threshold = dash_threshold_samples_ ? dash_threshold_samples_ : (dot_len_samples_ * 2);
    bool is_dash = on_samples >= threshold;
    if (lock_ && xSemaphoreTake(lock_, 0) == pdTRUE)
    {
        if (current_symbol_.size() < 12)
        {
            current_symbol_.push_back(is_dash ? '-' : '.');
        }
        xSemaphoreGive(lock_);
    }
    return true;
}

void MorseEngine::handleGap(uint32_t gap_samples)
{
    if (!calibrated_ || dot_len_samples_ == 0)
    {
        return;
    }
    uint32_t char_gap = dot_len_samples_ * config_.char_gap_mult;
    uint32_t word_gap = dot_len_samples_ * config_.word_gap_mult;
    if (gap_samples >= word_gap)
    {
        finalizeSymbol(true);
    }
    else if (gap_samples >= char_gap)
    {
        finalizeSymbol(false);
    }
}

void MorseEngine::finalizeSymbol(bool add_space)
{
    if (!lock_ || xSemaphoreTake(lock_, 0) != pdTRUE)
    {
        return;
    }
    if (!current_symbol_.empty())
    {
        char decoded = decode_morse(current_symbol_);
        if (decoded != '\0')
        {
            if (decoded_text_.size() < 120)
            {
                decoded_text_.push_back(decoded);
            }
        }
        current_symbol_.clear();
    }
    if (add_space && !decoded_text_.empty())
    {
        if (decoded_text_.back() != ' ' && decoded_text_.size() < 120)
        {
            decoded_text_.push_back(' ');
        }
    }
    xSemaphoreGive(lock_);
}

} // namespace input
