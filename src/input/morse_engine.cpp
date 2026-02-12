#include "morse_engine.h"

#include "freertos/task.h"
#include <algorithm>

namespace
{
struct MorseMap
{
    const char* code;
    char ch;
};

constexpr MorseMap kMorseMap[] = {
    {".-", 'A'},   {"-...", 'B'}, {"-.-.", 'C'}, {"-..", 'D'},  {".", 'E'},    {"..-.", 'F'},
    {"--.", 'G'},  {"....", 'H'}, {"..", 'I'},   {".---", 'J'}, {"-.-", 'K'},  {".-..", 'L'},
    {"--", 'M'},   {"-.", 'N'},   {"---", 'O'},  {".--.", 'P'}, {"--.-", 'Q'}, {".-.", 'R'},
    {"...", 'S'},  {"-", 'T'},    {"..-", 'U'},  {"...-", 'V'}, {".--", 'W'},  {"-..-", 'X'},
    {"-.--", 'Y'}, {"--..", 'Z'},
    {"-----", '0'}, {".----", '1'}, {"..---", '2'}, {"...--", '3'}, {"....-", '4'},
    {".....", '5'}, {"-....", '6'}, {"--...", '7'}, {"---..", '8'}, {"----.", '9'},
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
    cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = config_.dma_buf_count;
    cfg.dma_buf_len = config_.dma_buf_len;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = false;
    cfg.fixed_mclk = 0;

    if (i2s_driver_install(config_.i2s_port, &cfg, 0, nullptr) != ESP_OK)
    {
        return false;
    }

    i2s_pin_config_t pins = {};
    pins.mck_io_num = I2S_PIN_NO_CHANGE;
    pins.bck_io_num = config_.pin_sck;
    pins.ws_io_num = I2S_PIN_NO_CHANGE;
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

void MorseEngine::reset_state()
{
    sample_cursor_ = 0;
    env_ = 0;
    noise_ = 0;
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
    idle_samples_ = (config_.sample_rate * config_.idle_send_ms) / 1000;
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
    if (env > max_env_)
    {
        max_env_ = env;
    }
    else
    {
        max_env_ -= max_env_ >> 6;
        if (max_env_ < 500)
        {
            max_env_ = 500;
        }
    }
    int lvl = static_cast<int>((env * 100) / max_env_);
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
    for (size_t i = 0; i < count; ++i)
    {
        ++sample_cursor_;
        int32_t v = samples[i];
        if (v < 0)
        {
            v = -v;
        }
        env_ += (v - env_) >> 7;
        if (!signal_on_)
        {
            noise_ += (env_ - noise_) >> 10;
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
                last_activity_ = sample_cursor_;
            }
        }
        else
        {
            if (env_ <= th_low)
            {
                signal_on_ = false;
                in_pulse_ = false;
                uint32_t on_dur = static_cast<uint32_t>(sample_cursor_ - last_on_start_);
                handlePulseEnd(on_dur);
                last_off_start_ = sample_cursor_;
                ignore_until_ = sample_cursor_ + refractory_samples_;
                last_activity_ = sample_cursor_;
                if (calibrated_)
                {
                    setStatus("GAP");
                }
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
    }
    update_level(env_);
}

void MorseEngine::handlePulseEnd(uint32_t on_samples)
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
                }
            }
        }
        return;
    }

    if (dot_len_samples_ == 0)
    {
        return;
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
}

void MorseEngine::handleGap(uint32_t gap_samples)
{
    if (!calibrated_ || dot_len_samples_ == 0)
    {
        return;
    }
    uint32_t char_gap = dot_len_samples_ * 2;
    uint32_t word_gap = dot_len_samples_ * 5;
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
