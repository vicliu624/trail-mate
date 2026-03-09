#include "walkie/walkie_service.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if __has_include("esp_efuse.h")
#include "esp_efuse.h"
#endif

#if (defined(ARDUINO_T_LORA_PAGER) && defined(ARDUINO_LILYGO_LORA_SX1262) && defined(USING_AUDIO_CODEC)) || defined(TRAIL_MATE_ESP_BOARD_TAB5)

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform/esp/common/walkie_runtime.h"
#include <codec2.h>

namespace
{
constexpr uint32_t kSampleRate = 8000;
constexpr uint8_t kBitsPerSample = 16;
constexpr uint8_t kI2sChannels = 2;

constexpr float kFskBitRateKbps = 9.6f;
constexpr float kFskFreqDevKHz = 5.0f;
constexpr float kFskRxBwKHz = 156.2f;
constexpr uint16_t kFskPreambleLen = 16;
constexpr uint8_t kCodecFramesPerPacket = 5;
constexpr uint8_t kJitterMinPrebufferFrames = 10; // ~200ms (5 frames/packet, 20ms per frame)
constexpr uint8_t kJitterMaxPrebufferFrames = 15; // ~300ms
constexpr uint8_t kJitterMaxFrames = 25;          // ~500ms buffer
constexpr uint8_t kTxQueueMaxFrames = 20;         // ~400ms buffer
constexpr uint8_t kHeaderMagic0 = 'W';
constexpr uint8_t kHeaderMagic1 = 'T';
constexpr uint8_t kHeaderVersion = 2;
constexpr size_t kHeaderSize = 14;

constexpr uint8_t kDefaultVolume = 80;
constexpr float kDefaultGainDb = 36.0f;
constexpr uint32_t kMicLogIntervalMs = 1000;
constexpr float kTxPcmGain = 1.6f;
constexpr float kRxPcmGain = 2.0f;
constexpr int16_t kRxSilencePeak = 600;
constexpr uint8_t kRxQuietFramesToZero = 5;

constexpr uint32_t kStatusUpdateDecay = 3;
constexpr uint32_t kRxLogIntervalMs = 1000;
constexpr uint32_t kWalkieIrqTxDone = 0x0001;
constexpr uint32_t kWalkieIrqRxDone = 0x0002;
constexpr int kWalkieRadioOk = 0;

TaskHandle_t s_task = nullptr;
volatile bool s_active = false;
volatile bool s_stop_requested = false;
volatile bool s_ptt_pressed = false;
portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
walkie::Status s_status = {false, false, 0, 0, 0.0f};
char s_last_error[96] = {0};
uint8_t s_volume = kDefaultVolume;
platform::esp::common::walkie_runtime::Session s_runtime_session{};
constexpr uint32_t kWalkieTaskStack = 20 * 1024;

uint32_t now_ms()
{
    return static_cast<uint32_t>(pdTICKS_TO_MS(xTaskGetTickCount()));
}

uint32_t fallback_self_id()
{
#if __has_include("esp_efuse.h")
    uint8_t mac_bytes[6] = {};
    if (esp_efuse_mac_get_default(mac_bytes) == ESP_OK)
    {
        uint32_t node_id = (static_cast<uint32_t>(mac_bytes[2]) << 24) |
                           (static_cast<uint32_t>(mac_bytes[3]) << 16) |
                           (static_cast<uint32_t>(mac_bytes[4]) << 8) |
                           static_cast<uint32_t>(mac_bytes[5]);
        return node_id != 0 ? node_id : 1u;
    }
#endif
    return 1u;
}

uint32_t get_self_id()
{
    auto id = app::messagingFacade().getSelfNodeId();
    if (id != 0)
    {
        return static_cast<uint32_t>(id);
    }
    return fallback_self_id();
}

void update_status_tx(bool tx)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status.tx = tx;
    portEXIT_CRITICAL(&s_status_lock);
}

void update_status_levels(uint8_t tx_level, uint8_t rx_level)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status.tx_level = tx_level;
    s_status.rx_level = rx_level;
    portEXIT_CRITICAL(&s_status_lock);
}

void update_status_active(bool active)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status.active = active;
    portEXIT_CRITICAL(&s_status_lock);
}

void update_status_freq(float freq_mhz)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status.freq_mhz = freq_mhz;
    portEXIT_CRITICAL(&s_status_lock);
}

void set_error(const char* message)
{
    if (!message)
    {
        s_last_error[0] = '\0';
        return;
    }
    snprintf(s_last_error, sizeof(s_last_error), "%s", message);
}

uint8_t clamp_volume(int value)
{
    if (value < 0)
    {
        return 0;
    }
    if (value > 100)
    {
        return 100;
    }
    return static_cast<uint8_t>(value);
}

uint8_t compute_level(const int16_t* samples, int count, uint8_t prev)
{
    int32_t peak = 0;
    for (int i = 0; i < count; ++i)
    {
        int32_t v = samples[i];
        if (v < 0)
        {
            v = -v;
        }
        if (v > peak)
        {
            peak = v;
        }
    }
    uint32_t level = static_cast<uint32_t>((peak * 100) / 32767);
    if (level > 100)
    {
        level = 100;
    }
    uint8_t next = static_cast<uint8_t>(level);
    return static_cast<uint8_t>((prev * kStatusUpdateDecay + next) / (kStatusUpdateDecay + 1));
}

int16_t compute_peak(const int16_t* samples, int count)
{
    int32_t peak = 0;
    for (int i = 0; i < count; ++i)
    {
        int32_t v = samples[i];
        if (v < 0)
        {
            v = -v;
        }
        if (v > peak)
        {
            peak = v;
        }
    }
    if (peak > 32767)
    {
        peak = 32767;
    }
    return static_cast<int16_t>(peak);
}

void write_u32_le(uint8_t* out, uint32_t val)
{
    out[0] = static_cast<uint8_t>(val & 0xFF);
    out[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
    out[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
    out[3] = static_cast<uint8_t>((val >> 24) & 0xFF);
}

uint32_t read_u32_le(const uint8_t* in)
{
    return static_cast<uint32_t>(in[0]) |
           (static_cast<uint32_t>(in[1]) << 8) |
           (static_cast<uint32_t>(in[2]) << 16) |
           (static_cast<uint32_t>(in[3]) << 24);
}

void write_u16_le(uint8_t* out, uint16_t val)
{
    out[0] = static_cast<uint8_t>(val & 0xFF);
    out[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
}

uint16_t read_u16_le(const uint8_t* in)
{
    return static_cast<uint16_t>(in[0]) |
           (static_cast<uint16_t>(in[1]) << 8);
}

namespace walkie_runtime = platform::esp::common::walkie_runtime;

bool has_runtime_session()
{
    return walkie_runtime::isValid(s_runtime_session);
}

void cleanup_runtime_session(bool apply_mesh_config)
{
    if (!has_runtime_session())
    {
        return;
    }

    if (apply_mesh_config)
    {
        walkie_runtime::restoreLora(&s_runtime_session);
        app::configFacade().applyMeshConfig();
    }

    walkie_runtime::release(&s_runtime_session);
}

void shutdown_walkie_task()
{
    cleanup_runtime_session(true);
    update_status_active(false);
    s_active = false;
    s_task = nullptr;
}

void walkie_task(void*)
{
    if (!has_runtime_session())
    {
        shutdown_walkie_task();
        vTaskDelete(nullptr);
        return;
    }

    CODEC2* codec2_state = codec2_create(CODEC2_MODE_3200);
    if (!codec2_state)
    {
        shutdown_walkie_task();
        vTaskDelete(nullptr);
        return;
    }
    codec2_set_lpc_post_filter(codec2_state, 1, 0, 0.8f, 0.2f);

    const int samples_per_frame = codec2_samples_per_frame(codec2_state);
    const int bytes_per_frame = codec2_bytes_per_frame(codec2_state);
    const size_t payload_size = bytes_per_frame * kCodecFramesPerPacket;
    const size_t packet_size = kHeaderSize + payload_size;

    const int i2s_samples_per_frame = samples_per_frame * kI2sChannels;
    auto* pcm_in_i2s = static_cast<int16_t*>(malloc(i2s_samples_per_frame * sizeof(int16_t)));
    auto* pcm_out_i2s = static_cast<int16_t*>(malloc(i2s_samples_per_frame * sizeof(int16_t)));
    auto* pcm_in = static_cast<int16_t*>(malloc(samples_per_frame * sizeof(int16_t)));
    auto* pcm_out = static_cast<int16_t*>(malloc(samples_per_frame * sizeof(int16_t)));
    auto* codec_buf = static_cast<uint8_t*>(malloc(bytes_per_frame));
    auto* packet_buf = static_cast<uint8_t*>(malloc(packet_size));
    auto* rx_frame_buf = static_cast<uint8_t*>(malloc(bytes_per_frame * kJitterMaxFrames));
    auto* tx_frame_buf = static_cast<uint8_t*>(malloc(bytes_per_frame * kTxQueueMaxFrames));
    auto* last_pcm_out = static_cast<int16_t*>(calloc(samples_per_frame, sizeof(int16_t)));
    auto* silence_i2s = static_cast<int16_t*>(calloc(i2s_samples_per_frame, sizeof(int16_t)));

    if (!pcm_in_i2s || !pcm_out_i2s || !pcm_in || !pcm_out || !codec_buf || !packet_buf ||
        !rx_frame_buf || !tx_frame_buf || !last_pcm_out || !silence_i2s)
    {
        free(pcm_in_i2s);
        free(pcm_out_i2s);
        free(pcm_in);
        free(pcm_out);
        free(codec_buf);
        free(packet_buf);
        free(rx_frame_buf);
        free(tx_frame_buf);
        free(last_pcm_out);
        free(silence_i2s);
        codec2_destroy(codec2_state);
        shutdown_walkie_task();
        vTaskDelete(nullptr);
        return;
    }

    uint16_t seq = 0;
    uint16_t session_id = static_cast<uint16_t>(now_ms() & 0xFFFF);
    uint16_t tx_frame_counter = 0;
    uint32_t self_id = get_self_id();
    bool tx_mode = false;
    bool rx_started = false;
    uint8_t tx_level = 0;
    uint8_t rx_level = 0;
    uint32_t tx_read_ok = 0;
    uint32_t tx_read_fail = 0;
    int last_read_state = 0;
    uint32_t last_mic_log_ms = now_ms();
    uint32_t last_audio_log_ms = now_ms();
    int16_t last_tx_peak = 0;
    int16_t last_rx_peak = 0;
    uint32_t rx_pkts = 0;
    uint32_t rx_bad = 0;
    int last_rx_len = 0;
    int last_rx_state = 0;
    uint32_t last_rx_log_ms = now_ms();
    const uint32_t frame_interval_ms =
        static_cast<uint32_t>((samples_per_frame * 1000u) / kSampleRate);
    uint32_t last_play_ms = now_ms();
    uint32_t last_rx_frame_ms = 0;
    bool rx_play_active = false;
    size_t rx_target_prebuffer = kJitterMinPrebufferFrames;
    uint32_t rx_underruns = 0;
    uint32_t rx_good_windows = 0;
    uint32_t last_adapt_ms = now_ms();
    size_t rx_frame_count = 0;
    size_t rx_frame_head = 0;
    size_t rx_frame_tail = 0;
    uint8_t rx_quiet_frames = 0;
    uint32_t rx_src_id = 0;
    uint16_t rx_session_id = 0;
    uint16_t rx_expected_frame = 0;
    size_t tx_frame_count = 0;
    size_t tx_frame_head = 0;
    size_t tx_frame_tail = 0;
    bool tx_in_flight = false;

    while (!s_stop_requested)
    {
        bool want_tx = s_ptt_pressed;
        if (want_tx != tx_mode)
        {
            tx_mode = want_tx;
            update_status_tx(tx_mode);
            rx_started = false;
            rx_play_active = false;
            rx_frame_count = 0;
            rx_frame_head = 0;
            rx_frame_tail = 0;
            rx_src_id = 0;
            rx_session_id = 0;
            rx_expected_frame = 0;
            rx_target_prebuffer = kJitterMinPrebufferFrames;
            rx_underruns = 0;
            rx_good_windows = 0;
            last_adapt_ms = now_ms();
            rx_quiet_frames = 0;
            tx_frame_count = 0;
            tx_frame_head = 0;
            tx_frame_tail = 0;
            tx_in_flight = false;
            if (tx_mode)
            {
                walkie_runtime::standby(&s_runtime_session);
                session_id = static_cast<uint16_t>(session_id + 1);
                seq = 0;
                tx_frame_counter = 0;
            }
        }

        if (tx_mode)
        {
            int read_state = walkie_runtime::codecRead(&s_runtime_session, reinterpret_cast<uint8_t*>(pcm_in_i2s),
                                                       i2s_samples_per_frame * sizeof(int16_t));
            last_read_state = read_state;
            if (read_state == 0)
            {
                tx_read_ok++;
                for (int i = 0; i < samples_per_frame; ++i)
                {
                    int32_t l = pcm_in_i2s[i * 2];
                    int32_t r = pcm_in_i2s[i * 2 + 1];
                    int32_t mix = (l + r) / 2;
                    mix = static_cast<int32_t>(mix * kTxPcmGain);
                    if (mix > 32767) mix = 32767;
                    if (mix < -32768) mix = -32768;
                    pcm_in[i] = static_cast<int16_t>(mix);
                }
                last_tx_peak = compute_peak(pcm_in, samples_per_frame);
                tx_level = compute_level(pcm_in, samples_per_frame, tx_level);
                codec2_encode(codec2_state, codec_buf, pcm_in);
                if (tx_frame_count < kTxQueueMaxFrames)
                {
                    uint8_t* dst = tx_frame_buf + (tx_frame_tail * bytes_per_frame);
                    memcpy(dst, codec_buf, bytes_per_frame);
                    tx_frame_tail = (tx_frame_tail + 1) % kTxQueueMaxFrames;
                    tx_frame_count++;
                }
                else
                {
                    tx_frame_head = (tx_frame_head + 1) % kTxQueueMaxFrames;
                    uint8_t* dst = tx_frame_buf + (tx_frame_tail * bytes_per_frame);
                    memcpy(dst, codec_buf, bytes_per_frame);
                    tx_frame_tail = (tx_frame_tail + 1) % kTxQueueMaxFrames;
                }
                tx_frame_counter = static_cast<uint16_t>(tx_frame_counter + 1);

                if (tx_in_flight)
                {
                    uint32_t flags = walkie_runtime::getRadioIrqFlags(&s_runtime_session);
                    if (flags & kWalkieIrqTxDone)
                    {
                        walkie_runtime::clearRadioIrqFlags(&s_runtime_session, flags);
                        tx_in_flight = false;
                    }
                }

                if (!tx_in_flight && tx_frame_count >= kCodecFramesPerPacket)
                {
                    packet_buf[0] = kHeaderMagic0;
                    packet_buf[1] = kHeaderMagic1;
                    packet_buf[2] = kHeaderVersion;
                    packet_buf[3] = 0x01;
                    write_u32_le(&packet_buf[4], self_id);
                    write_u16_le(&packet_buf[8], session_id);
                    write_u16_le(&packet_buf[10], seq++);
                    uint16_t frame0_index =
                        static_cast<uint16_t>(tx_frame_counter - tx_frame_count);
                    write_u16_le(&packet_buf[12], frame0_index);
                    for (size_t i = 0; i < kCodecFramesPerPacket; ++i)
                    {
                        const uint8_t* src = tx_frame_buf + (tx_frame_head * bytes_per_frame);
                        memcpy(packet_buf + kHeaderSize + (i * bytes_per_frame),
                               src,
                               bytes_per_frame);
                        tx_frame_head = (tx_frame_head + 1) % kTxQueueMaxFrames;
                        tx_frame_count--;
                    }
                    int tx_state = walkie_runtime::startTransmit(&s_runtime_session, packet_buf, packet_size);
                    if (tx_state == kWalkieRadioOk)
                    {
                        tx_in_flight = true;
                    }
                    else
                    {
                        std::printf("[WALKIE] startTransmit failed state=%d\n", tx_state);
                    }
                }
                update_status_levels(tx_level, rx_level);
            }
            else
            {
                tx_read_fail++;
            }

            uint32_t current_ms = now_ms();
            if (current_ms - last_mic_log_ms >= kMicLogIntervalMs)
            {
                std::printf("[WALKIE] mic read ok=%lu fail=%lu last=%d\n",
                              static_cast<unsigned long>(tx_read_ok),
                              static_cast<unsigned long>(tx_read_fail),
                              last_read_state);
                tx_read_ok = 0;
                tx_read_fail = 0;
                last_mic_log_ms = current_ms;
            }
            if (current_ms - last_audio_log_ms >= 1000)
            {
                std::printf("[WALKIE] tx lvl=%u peak=%d q=%u inflight=%d\n",
                              static_cast<unsigned>(tx_level),
                              static_cast<int>(last_tx_peak),
                              static_cast<unsigned>(tx_frame_count),
                              tx_in_flight ? 1 : 0);
                last_audio_log_ms = current_ms;
            }
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }

        if (!rx_started)
        {
            walkie_runtime::startReceive(&s_runtime_session);
            rx_started = true;
        }

        uint32_t irq = walkie_runtime::getRadioIrqFlags(&s_runtime_session);
        if (irq & kWalkieIrqRxDone)
        {
            int len = walkie_runtime::getPacketLength(&s_runtime_session, true);
            last_rx_len = len;
            if (len > static_cast<int>(kHeaderSize) && len <= static_cast<int>(packet_size))
            {
                int read_state = walkie_runtime::readRadioData(&s_runtime_session, packet_buf, len);
                last_rx_state = read_state;
                if (read_state == kWalkieRadioOk)
                {
                    if (packet_buf[0] == kHeaderMagic0 &&
                        packet_buf[1] == kHeaderMagic1 &&
                        packet_buf[2] == kHeaderVersion)
                    {
                        uint32_t src = read_u32_le(&packet_buf[4]);
                        uint16_t pkt_session = read_u16_le(&packet_buf[8]);
                        uint16_t pkt_seq = read_u16_le(&packet_buf[10]);
                        (void)pkt_seq;
                        uint16_t pkt_frame0 = read_u16_le(&packet_buf[12]);
                        if (src != self_id)
                        {
                            if (rx_src_id != src || rx_session_id != pkt_session)
                            {
                                rx_src_id = src;
                                rx_session_id = pkt_session;
                                rx_expected_frame = static_cast<uint16_t>(pkt_frame0 + kCodecFramesPerPacket);
                                rx_frame_count = 0;
                                rx_frame_head = 0;
                                rx_frame_tail = 0;
                                rx_play_active = false;
                                rx_target_prebuffer = kJitterMinPrebufferFrames;
                                rx_underruns = 0;
                                rx_good_windows = 0;
                                last_adapt_ms = now_ms();
                                rx_quiet_frames = 0;
                            }
                            else
                            {
                                uint16_t diff =
                                    static_cast<uint16_t>(pkt_frame0 - rx_expected_frame);
                                if (diff > 0x8000)
                                {
                                    rx_bad++;
                                    goto rx_done;
                                }
                                if (diff > 0)
                                {
                                    rx_bad++;
                                }
                                rx_expected_frame = static_cast<uint16_t>(pkt_frame0 + kCodecFramesPerPacket);
                            }

                            rx_pkts++;
                            size_t payload_len = static_cast<size_t>(len) - kHeaderSize;
                            size_t frame_count = payload_len / bytes_per_frame;
                            for (size_t i = 0; i < frame_count; ++i)
                            {
                                if (rx_frame_count < kJitterMaxFrames)
                                {
                                    uint8_t* dst = rx_frame_buf + (rx_frame_tail * bytes_per_frame);
                                    memcpy(dst,
                                           packet_buf + kHeaderSize + (i * bytes_per_frame),
                                           bytes_per_frame);
                                    rx_frame_tail = (rx_frame_tail + 1) % kJitterMaxFrames;
                                    rx_frame_count++;
                                    last_rx_frame_ms = now_ms();
                                }
                                else
                                {
                                    rx_bad++;
                                }
                            }
                        }
                        else
                        {
                            rx_bad++;
                        }
                    }
                    else
                    {
                        rx_bad++;
                    }
                }
                else
                {
                    rx_bad++;
                }
            }
            else
            {
                rx_bad++;
            }
        rx_done:
            walkie_runtime::clearRadioIrqFlags(&s_runtime_session, irq);
            walkie_runtime::startReceive(&s_runtime_session);
        }
        else if (irq)
        {
            walkie_runtime::clearRadioIrqFlags(&s_runtime_session, irq);
        }

        uint32_t current_ms = now_ms();
        if (current_ms - last_play_ms >= frame_interval_ms)
        {
            last_play_ms = current_ms;
            if (!rx_play_active)
            {
                if (rx_frame_count >= rx_target_prebuffer)
                {
                    rx_play_active = true;
                }
            }
            if (rx_play_active)
            {
                if (rx_frame_count > 0)
                {
                    const uint8_t* frame = rx_frame_buf + (rx_frame_head * bytes_per_frame);
                    codec2_decode(codec2_state, pcm_out, frame);
                    memcpy(last_pcm_out, pcm_out, samples_per_frame * sizeof(int16_t));
                    last_rx_peak = compute_peak(pcm_out, samples_per_frame);
                    rx_frame_head = (rx_frame_head + 1) % kJitterMaxFrames;
                    rx_frame_count--;
                    last_rx_frame_ms = now_ms();
                }
                else
                {
                    if ((current_ms - last_rx_frame_ms) > (frame_interval_ms * 3))
                    {
                        rx_play_active = false;
                    }
                    for (int j = 0; j < samples_per_frame; ++j)
                    {
                        int32_t sample = (static_cast<int32_t>(last_pcm_out[j]) * 4) / 5;
                        last_pcm_out[j] = static_cast<int16_t>(sample);
                        pcm_out[j] = last_pcm_out[j];
                    }
                    last_rx_peak = compute_peak(pcm_out, samples_per_frame);
                    rx_underruns++;
                }

                if (last_rx_peak < kRxSilencePeak)
                {
                    if (rx_quiet_frames < 255)
                    {
                        rx_quiet_frames++;
                    }
                }
                else
                {
                    rx_quiet_frames = 0;
                }
                if (rx_quiet_frames >= kRxQuietFramesToZero)
                {
                    memset(pcm_out, 0, samples_per_frame * sizeof(int16_t));
                    memset(last_pcm_out, 0, samples_per_frame * sizeof(int16_t));
                    last_rx_peak = 0;
                    rx_level = 0;
                }
                else
                {
                    rx_level = compute_level(pcm_out, samples_per_frame, rx_level);
                }

                for (int j = 0; j < samples_per_frame; ++j)
                {
                    int32_t sample = static_cast<int32_t>(pcm_out[j] * kRxPcmGain);
                    if (sample > 32767) sample = 32767;
                    if (sample < -32768) sample = -32768;
                    pcm_out_i2s[j * 2] = static_cast<int16_t>(sample);
                    pcm_out_i2s[j * 2 + 1] = static_cast<int16_t>(sample);
                }
                walkie_runtime::codecWrite(&s_runtime_session, reinterpret_cast<uint8_t*>(pcm_out_i2s),
                                           i2s_samples_per_frame * sizeof(int16_t));
                update_status_levels(tx_level, rx_level);
            }
            else
            {
                walkie_runtime::codecWrite(&s_runtime_session, reinterpret_cast<uint8_t*>(silence_i2s),
                                           i2s_samples_per_frame * sizeof(int16_t));
                rx_quiet_frames = 0;
                rx_level = static_cast<uint8_t>(
                    (rx_level * kStatusUpdateDecay) / (kStatusUpdateDecay + 1));
                update_status_levels(tx_level, rx_level);
            }
        }

        if (current_ms - last_adapt_ms >= 1000)
        {
            if (rx_underruns > 1)
            {
                rx_target_prebuffer = kJitterMaxPrebufferFrames;
                rx_good_windows = 0;
            }
            else if (rx_underruns == 0)
            {
                rx_good_windows++;
                if (rx_good_windows >= 3)
                {
                    rx_target_prebuffer = kJitterMinPrebufferFrames;
                }
            }
            else
            {
                rx_good_windows = 0;
            }
            rx_underruns = 0;
            last_adapt_ms = current_ms;
        }

        if (current_ms - last_rx_log_ms >= kRxLogIntervalMs)
        {
            if (rx_pkts != 0 || rx_bad != 0)
            {
                std::printf("[WALKIE] rx ok=%lu bad=%lu last_len=%d state=%d\n",
                              static_cast<unsigned long>(rx_pkts),
                              static_cast<unsigned long>(rx_bad),
                              last_rx_len,
                              last_rx_state);
            }
            rx_pkts = 0;
            rx_bad = 0;
            last_rx_len = 0;
            last_rx_state = 0;
            last_rx_log_ms = current_ms;
        }
        if (current_ms - last_audio_log_ms >= 1000)
        {
            std::printf("[WALKIE] rx lvl=%u peak=%d buf=%u pre=%u underrun=%lu\n",
                          static_cast<unsigned>(rx_level),
                          static_cast<int>(last_rx_peak),
                          static_cast<unsigned>(rx_frame_count),
                          static_cast<unsigned>(rx_target_prebuffer),
                          static_cast<unsigned long>(rx_underruns));
            last_audio_log_ms = current_ms;
        }

        vTaskDelay(pdMS_TO_TICKS(2));
    }

    free(rx_frame_buf);
    free(tx_frame_buf);
    free(last_pcm_out);
    free(silence_i2s);
    free(pcm_in_i2s);
    free(pcm_out_i2s);
    free(pcm_in);
    free(pcm_out);
    free(codec_buf);
    free(packet_buf);
    codec2_destroy(codec2_state);

    shutdown_walkie_task();
    vTaskDelete(nullptr);
}
} // namespace

namespace walkie
{

bool start()
{
    if (s_active)
    {
        return true;
    }

    cleanup_runtime_session(true);
    set_error(nullptr);

    if (!walkie_runtime::isSupported())
    {
        set_error("Walkie not supported");
        return false;
    }

    if (!walkie_runtime::tryAcquire(&s_runtime_session) || !walkie_runtime::isRadioReady(s_runtime_session))
    {
        set_error("Radio not ready");
        std::printf("[WALKIE] start failed: radio not ready\n");
        cleanup_runtime_session(false);
        return false;
    }
    if (!walkie_runtime::isCodecReady(s_runtime_session))
    {
        set_error("Codec not ready");
        std::printf("[WALKIE] start failed: codec not ready\n");
        cleanup_runtime_session(false);
        return false;
    }

    std::printf("[WALKIE] acquire walkie runtime\n");

    auto& config = app::configFacade().getConfig();
    float freq_mhz = chat::meshtastic::estimateFrequencyMhz(config.meshtastic_config.region,
                                                            config.meshtastic_config.modem_preset);
    if (freq_mhz <= 0.0f)
    {
        freq_mhz = 915.0f;
    }
    update_status_freq(freq_mhz);

    std::printf("[WALKIE] config freq=%.3f br=%.1f dev=%.1f rxBw=%.1f preamble=%u pwr=%d\n",
                  freq_mhz, kFskBitRateKbps, kFskFreqDevKHz, kFskRxBwKHz,
                  kFskPreambleLen, config.meshtastic_config.tx_power);

    char runtime_error[64] = {0};
    if (!walkie_runtime::configureFsk(&s_runtime_session,
                                      freq_mhz,
                                      config.meshtastic_config.tx_power,
                                      runtime_error,
                                      sizeof(runtime_error)))
    {
        if (runtime_error[0] != '\0')
        {
            set_error(runtime_error);
        }
        else if (s_last_error[0] == '\0')
        {
            set_error("FSK config failed");
        }
        cleanup_runtime_session(true);
        return false;
    }

    if (walkie_runtime::codecOpen(&s_runtime_session, kBitsPerSample, kI2sChannels, kSampleRate) != 0)
    {
        set_error("Codec open failed");
        std::printf("[WALKIE] codec open failed\n");
        cleanup_runtime_session(true);
        return false;
    }
    s_volume = kDefaultVolume;
    walkie_runtime::codecSetVolume(&s_runtime_session, s_volume);
    walkie_runtime::codecSetGain(&s_runtime_session, kDefaultGainDb);
    walkie_runtime::codecSetMute(&s_runtime_session, false);

    s_stop_requested = false;
    s_ptt_pressed = false;
    s_active = true;
    update_status_active(true);
    update_status_tx(false);
    update_status_levels(0, 0);
    update_status_freq(freq_mhz);

    BaseType_t result = xTaskCreate(
        walkie_task,
        "walkie_audio",
        kWalkieTaskStack,
        nullptr,
        7,
        &s_task);
    if (result != pdPASS)
    {
        set_error("Task create failed");
        std::printf("[WALKIE] task create failed\n");
        cleanup_runtime_session(true);
        update_status_active(false);
        s_active = false;
        s_task = nullptr;
        return false;
    }

    return true;
}

void stop()
{
    if (!s_active)
    {
        cleanup_runtime_session(true);
        return;
    }

    s_stop_requested = true;
    s_ptt_pressed = false;

    for (int i = 0; i < 30; ++i)
    {
        if (!s_task)
        {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (s_task)
    {
        std::printf("[WALKIE] stop timed out, forcing runtime cleanup\n");
        cleanup_runtime_session(true);
        update_status_active(false);
        s_active = false;
        s_task = nullptr;
        return;
    }

    cleanup_runtime_session(true);
}

bool is_active()
{
    return s_active;
}

void set_ptt(bool pressed)
{
    if (!s_active)
    {
        return;
    }
    s_ptt_pressed = pressed;
}

void adjust_volume(int delta)
{
    if (!s_active)
    {
        return;
    }
    s_volume = clamp_volume(static_cast<int>(s_volume) + delta);
    walkie_runtime::codecSetVolume(&s_runtime_session, s_volume);
    std::printf("[WALKIE] volume=%u\n", static_cast<unsigned>(s_volume));
}

int get_volume()
{
    return s_volume;
}

void on_key_event(char key, int state)
{
    if (!s_active)
    {
        return;
    }
    if (key != ' ')
    {
        return;
    }
    std::printf("[WALKIE] PTT key state=%d\n", state);
    const bool pressed = state != 0;
    set_ptt(pressed);
    update_status_tx(pressed);
}

Status get_status()
{
    Status out{};
    portENTER_CRITICAL(&s_status_lock);
    out = s_status;
    portEXIT_CRITICAL(&s_status_lock);
    return out;
}

const char* get_last_error()
{
    return s_last_error;
}

} // namespace walkie

#else

namespace walkie
{

bool start()
{
    return false;
}

void stop()
{
}

bool is_active()
{
    return false;
}

void set_ptt(bool)
{
}

void adjust_volume(int)
{
}

int get_volume()
{
    return 0;
}

void on_key_event(char, int)
{
}

Status get_status()
{
    return {false, false, 0, 0, 0.0f};
}

const char* get_last_error()
{
    return "Walkie not supported";
}

} // namespace walkie

#endif
