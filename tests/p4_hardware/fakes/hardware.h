#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_CODEC_DEV_OK = 0;
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
inline const char* esp_err_to_name(int)
{
    return "fake";
}
namespace fake
{
inline int64_t now_us = 0;
inline bool power = true;
inline int step = 0, fail_step = -1, live = 0, channel_live = 0;
inline int sample_reads = 0;
inline int heading_devices = 0, deleted_tasks = 0;
inline size_t last_sample_size = 0;
inline std::array<uint8_t, 9> sample{1, 100, 0, 0, 0, 0, 0, 8, 0};
inline float gain = 0;
inline int output_volume = 0;
inline bool gain_ok = true;
inline bool mutex_ok = true;
inline int codec_close_result = 0, codec_open_result = 0;
inline bool i2c_ok = true;
inline int resets = 0;
inline std::vector<int16_t> written;
inline bool pass() { return ++step != fail_step; }
template <class T>
const T* create()
{
    if (!pass()) return nullptr;
    ++live;
    return new T{};
}
template <class T>
int destroy(const T* value)
{
    delete value;
    --live;
    return 0;
}
} // namespace fake
inline int64_t esp_timer_get_time() { return fake::now_us; }
using TaskHandle_t = void*;
constexpr int pdPASS = 1, pdTRUE = 1;
inline int pdMS_TO_TICKS(int value) { return value; }
inline void vTaskDelay(int ms) { fake::now_us += static_cast<int64_t>(ms) * 1000; }
inline void vTaskDelete(void*) { ++fake::deleted_tasks; }
inline int xTaskCreate(void (*)(void*), const char*, int, void*, int, TaskHandle_t*) { return pdPASS; }
struct StaticSemaphore_t
{
};
using SemaphoreHandle_t = StaticSemaphore_t*;
inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* value) { return value; }
inline int xSemaphoreTake(SemaphoreHandle_t, int) { return fake::mutex_ok ? pdTRUE : 0; }
inline void xSemaphoreGive(SemaphoreHandle_t) {}
using i2c_master_bus_handle_t = void*;
using i2c_master_dev_handle_t = void*;
constexpr int I2C_ADDR_BIT_LEN_7 = 0;
struct i2c_device_config_t
{
    int dev_addr_length;
    uint16_t device_address;
    int scl_speed_hz;
};
inline int i2c_master_bus_add_device(void*, const i2c_device_config_t* config, void** handle)
{
    *handle = reinterpret_cast<void*>(static_cast<uintptr_t>(config->device_address));
    ++fake::heading_devices;
    return 0;
}
inline int i2c_master_bus_rm_device(void*)
{
    --fake::heading_devices;
    return 0;
}
inline int i2c_master_probe(void*, uint16_t, int) { return fake::i2c_ok ? 0 : -1; }
inline int i2c_master_transmit(void*, const uint8_t* data, size_t size, int)
{
    if (!fake::i2c_ok) return -1;
    if (size == 2 && data[0] == 0 && data[1] == 0xAA) ++fake::resets;
    return 0;
}
inline int i2c_master_transmit_receive(void* handle, const uint8_t* reg, size_t,
                                       uint8_t* out, size_t size, int)
{
    if (!fake::i2c_ok) return -1;
    if (*reg == 0x3F)
    {
        *out = 8;
        return 0;
    }
    std::fill_n(out, size, 0);
    if (*reg == 0x10)
    {
        ++fake::sample_reads;
        fake::last_sample_size = size;
        std::copy_n(fake::sample.begin(), size, out);
    }
    else if (*reg == 0 && reinterpret_cast<uintptr_t>(handle) == 0x68) *out = 0xEA;
    else if (*reg == 0 && size == 2)
    {
        out[0] = 0x48;
        out[1] = 0x09;
    }
    return 0;
}
using gpio_num_t = int;
using i2s_port_t = int;
using i2s_chan_handle_t = int*;
constexpr int I2S_NUM_0 = 0, I2S_ROLE_MASTER = 0, I2S_MCLK_MULTIPLE_384 = 384;
constexpr int I2S_DATA_BIT_WIDTH_16BIT = 16, I2S_SLOT_MODE_STEREO = 2;
struct i2s_chan_config_t
{
    bool auto_clear = false;
};
#define I2S_CHANNEL_DEFAULT_CONFIG(...) \
    i2s_chan_config_t {}
struct i2s_clock
{
    int sample_rate = 16000;
    int mclk_multiple = 0;
};
#define I2S_STD_CLK_DEFAULT_CONFIG(...) \
    i2s_clock {}
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(...) 0
struct i2s_std_config_t
{
    i2s_clock clk_cfg;
    int slot_cfg;
    struct
    {
        int mclk, bclk, ws, dout, din;
        int invert_flags;
    } gpio_cfg;
};
inline int i2s_new_channel(const i2s_chan_config_t*, int** tx, int** rx)
{
    if (!fake::pass()) return -1;
    *tx = new int{};
    *rx = new int{};
    fake::channel_live += 2;
    return 0;
}
inline int i2s_channel_init_std_mode(int*, const i2s_std_config_t*) { return fake::pass() ? 0 : -1; }
inline int i2s_del_channel(int* channel)
{
    delete channel;
    --fake::channel_live;
    return 0;
}
struct audio_codec_data_if_t
{
};
struct audio_codec_ctrl_if_t
{
};
struct audio_codec_if_t
{
};
struct audio_codec_i2s_cfg_t
{
    uint8_t port;
    void* rx_handle;
    void* tx_handle;
};
struct audio_codec_i2c_cfg_t
{
    uint8_t port;
    int addr;
    void* bus_handle;
};
struct es8311_codec_cfg_t
{
    const audio_codec_ctrl_if_t* ctrl_if;
    int codec_mode, pa_pin;
    bool master_mode, use_mclk, digital_mic, no_dac_ref;
    int mclk_div;
};
constexpr int ES8311_CODEC_DEFAULT_ADDR = 0x30;
constexpr int ESP_CODEC_DEV_WORK_MODE_BOTH = 3, ESP_CODEC_DEV_TYPE_IN_OUT = 3;
struct codec_device
{
};
using esp_codec_dev_handle_t = codec_device*;
struct esp_codec_dev_cfg_t
{
    int dev_type;
    const audio_codec_if_t* codec_if;
    const audio_codec_data_if_t* data_if;
};
struct esp_codec_dev_sample_info_t
{
    int bits_per_sample, channel, sample_rate, mclk_multiple;
};
inline auto audio_codec_new_i2s_data(audio_codec_i2s_cfg_t*) { return fake::create<audio_codec_data_if_t>(); }
inline auto audio_codec_new_i2c_ctrl(audio_codec_i2c_cfg_t*) { return fake::create<audio_codec_ctrl_if_t>(); }
inline auto es8311_codec_new(es8311_codec_cfg_t*) { return fake::create<audio_codec_if_t>(); }
inline auto esp_codec_dev_new(esp_codec_dev_cfg_t*) { return const_cast<codec_device*>(fake::create<codec_device>()); }
inline void esp_codec_dev_delete(codec_device* value) { fake::destroy(value); }
inline int audio_codec_delete_codec_if(const audio_codec_if_t* value) { return fake::destroy(value); }
inline int audio_codec_delete_ctrl_if(const audio_codec_ctrl_if_t* value) { return fake::destroy(value); }
inline int audio_codec_delete_data_if(const audio_codec_data_if_t* value) { return fake::destroy(value); }
inline int esp_codec_dev_open(codec_device*, esp_codec_dev_sample_info_t*) { return fake::codec_open_result; }
inline int esp_codec_dev_close(codec_device*) { return fake::codec_close_result; }
inline int esp_codec_dev_set_in_gain(codec_device*, float gain)
{
    fake::gain = gain;
    return fake::gain_ok ? 0 : -1;
}
inline int esp_codec_dev_set_out_vol(codec_device*, int value)
{
    fake::output_volume = value;
    return 0;
}
inline int esp_codec_dev_set_out_mute(codec_device*, bool) { return 0; }
inline int esp_codec_dev_read(codec_device*, void* out, int bytes)
{
    auto* pcm = static_cast<int16_t*>(out);
    for (int i = 0; i < bytes / 4; ++i)
    {
        pcm[i * 2] = 100;
        pcm[i * 2 + 1] = -100;
    }
    return 0;
}
inline int esp_codec_dev_write(codec_device*, void* data, int bytes)
{
    auto* pcm = static_cast<int16_t*>(data);
    fake::written.insert(fake::written.end(), pcm, pcm + bytes / 2);
    return 0;
}
