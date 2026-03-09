#pragma once

#include "driver/i2s.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdint.h>
#include <string>
#include <vector>

namespace input
{

class MorseEngine
{
  public:
    enum class CalibPhase
    {
        Dot,
        Dash,
        Done
    };

    struct Config
    {
        i2s_port_t i2s_port = I2S_NUM_0;
        int pin_sck = -1;
        int pin_data = -1;
        uint32_t sample_rate = 16000;
        uint32_t idle_send_ms = 3000;
        uint32_t refractory_ms = 80;
        uint32_t release_ms = 15;
        uint32_t touch_suppress_ms = 150;
        uint32_t dot_calib_target = 5;
        uint32_t dash_calib_target = 3;
        uint32_t min_dot_ms = 20;
        uint32_t max_dot_ms = 500;
        uint32_t dash_min_mult = 2;
        uint32_t dash_max_mult = 8;
        uint32_t char_gap_mult = 3;
        uint32_t word_gap_mult = 7;
        int32_t min_high = 300;
        int32_t min_low = 150;
        int32_t level_gate = 0;
        uint8_t dc_shift = 8;
        int32_t input_gain = 1;
        uint32_t log_interval_ms = 0;
        bool log_calib_only = true;
        int dma_buf_count = 4;
        int dma_buf_len = 256;
        uint32_t task_stack = 4 * 1024;
        UBaseType_t task_priority = 3;
    };

    struct Snapshot
    {
        int level = 0;
        bool calibrated = false;
        bool in_pulse = false;
        CalibPhase phase = CalibPhase::Dot;
        uint32_t calib_index = 0;
        uint32_t calib_total = 0;
        std::string status;
        std::string symbol;
        std::string text;
    };

    MorseEngine();
    ~MorseEngine();

    bool start(const Config& config);
    void stop();

    bool getSnapshot(Snapshot& out);
    bool consumeSend(std::string& text);
    static void notifyTouch(uint32_t suppress_ms = 0);

    bool isRunning() const
    {
        return running_;
    }

  private:
    void reset_state();
    void taskLoop();
    void setStatus(const char* status);
    void update_level(int32_t env);
    void processSamples(const int16_t* samples, size_t count);
    bool handlePulseEnd(uint32_t on_samples);
    void handleGap(uint32_t gap_samples);
    void finalizeSymbol(bool add_space);

    static void task_entry(void* arg);

    Config config_{};
    SemaphoreHandle_t lock_ = nullptr;
    TaskHandle_t task_ = nullptr;
    volatile bool running_ = false;

    uint64_t sample_cursor_ = 0;
    int32_t env_ = 0;
    int32_t noise_ = 0;
    int32_t dc_ = 0;
    int32_t max_env_ = 1;
    volatile int level_ = 0;
    volatile bool in_pulse_ = false;
    volatile bool calibrated_ = false;

    bool signal_on_ = false;
    uint64_t last_on_start_ = 0;
    uint64_t last_off_start_ = 0;
    uint64_t ignore_until_ = 0;
    uint64_t last_activity_ = 0;
    uint32_t dot_len_samples_ = 0;
    uint32_t dash_len_samples_ = 0;
    uint32_t dash_threshold_samples_ = 0;
    uint32_t dash_min_samples_ = 0;
    uint32_t dash_max_samples_ = 0;
    uint32_t refractory_samples_ = 0;
    uint32_t release_samples_ = 0;
    uint32_t low_run_samples_ = 0;
    uint32_t idle_samples_ = 0;
    uint32_t log_interval_samples_ = 0;
    uint64_t next_log_sample_ = 0;
    int32_t log_peak_ = 0;
    int32_t log_raw_min_ = 0;
    int32_t log_raw_max_ = 0;

    CalibPhase calib_phase_ = CalibPhase::Dot;
    std::vector<uint32_t> dot_calib_samples_;
    std::vector<uint32_t> dash_calib_samples_;
    size_t calib_count_ = 0;

    std::string current_symbol_;
    std::string decoded_text_;
    std::string status_ = "CALIB";
    std::string send_text_;
    volatile bool send_pending_ = false;
};

} // namespace input
