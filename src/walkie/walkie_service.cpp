#include "walkie_service.h"

#if defined(ARDUINO_LILYGO_LORA_SX1262)

#include "../app/app_context.h"
#include "../app/app_tasks.h"
#include "../board/TLoRaPagerBoard.h"
#include "../chat/infra/meshtastic/mt_region.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <Arduino.h>
#include <RadioLib.h>
#include <codec2.h>
#include <cstring>

namespace
{
constexpr uint32_t kSampleRate = 8000;
constexpr uint8_t kBitsPerSample = 16;
constexpr uint8_t kI2sChannels = 2;

constexpr float kFskBitRateKbps = 9.6f;
constexpr float kFskFreqDevKHz = 5.0f;
constexpr float kFskRxBwKHz = 156.2f;
constexpr uint16_t kFskPreambleLen = 16;
constexpr uint8_t kFskSyncWord[] = {0x2D, 0x01};

constexpr uint8_t kCodecFramesPerPacket = 5;
constexpr uint8_t kJitterMinPrebufferFrames = 10; // ~200ms (5 frames/packet, 20ms per frame)
constexpr uint8_t kJitterMaxPrebufferFrames = 15; // ~300ms
constexpr uint8_t kJitterMaxFrames = 25;          // ~500ms buffer
constexpr uint8_t kTxQueueMaxFrames = 20;          // ~400ms buffer
constexpr uint8_t kHeaderMagic0 = 'W';
constexpr uint8_t kHeaderMagic1 = 'T';
constexpr uint8_t kHeaderVersion = 2;
constexpr size_t kHeaderSize = 14;

constexpr uint8_t kDefaultVolume = 80;
constexpr float kDefaultGainDb = 36.0f;
constexpr uint8_t kVolumeStep = 5;
constexpr uint32_t kMicLogIntervalMs = 1000;
constexpr float kTxPcmGain = 1.6f;
constexpr float kRxPcmGain = 2.0f;

constexpr uint32_t kStatusUpdateDecay = 3;
constexpr uint32_t kRxLogIntervalMs = 1000;

TaskHandle_t s_task = nullptr;
volatile bool s_active = false;
volatile bool s_stop_requested = false;
volatile bool s_ptt_pressed = false;
portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
walkie::Status s_status = {false, false, 0, 0, 0.0f};
char s_last_error[96] = {0};
uint8_t s_volume = kDefaultVolume;
constexpr uint32_t kWalkieTaskStack = 20 * 1024;

uint32_t get_self_id()
{
    auto id = app::AppContext::getInstance().getSelfNodeId();
    if (id != 0)
    {
        return static_cast<uint32_t>(id);
    }
    uint64_t mac = ESP.getEfuseMac();
    return static_cast<uint32_t>(mac & 0xFFFFFFFFu);
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

bool configure_fsk(TLoRaPagerBoard& board, float freq_mhz, int8_t tx_power)
{
    if (!board.isRadioOnline())
    {
        set_error("Radio offline");
        return false;
    }

    if (!board.lock(pdMS_TO_TICKS(200)))
    {
        return false;
    }

    board.radio_.standby();
    int state = board.radio_.beginFSK(freq_mhz,
                                      kFskBitRateKbps,
                                      kFskFreqDevKHz,
                                      kFskRxBwKHz,
                                      tx_power,
                                      kFskPreambleLen,
                                      1.6f);
    if (state != RADIOLIB_ERR_NONE)
    {
        Serial.printf("[WALKIE] beginFSK failed state=%d\n", state);
        char buf[64];
        snprintf(buf, sizeof(buf), "beginFSK fail %d", state);
        set_error(buf);
        board.unlock();
        return false;
    }

    uint8_t sync[sizeof(kFskSyncWord)];
    memcpy(sync, kFskSyncWord, sizeof(sync));
    state = board.radio_.setSyncWord(sync, sizeof(sync));
    if (state != RADIOLIB_ERR_NONE)
    {
        Serial.printf("[WALKIE] setSyncWord failed state=%d\n", state);
        char buf[64];
        snprintf(buf, sizeof(buf), "setSync fail %d", state);
        set_error(buf);
        board.unlock();
        return false;
    }

    state = board.radio_.setCRC(2);
    if (state != RADIOLIB_ERR_NONE)
    {
        Serial.printf("[WALKIE] setCRC failed state=%d\n", state);
        char buf[64];
        snprintf(buf, sizeof(buf), "setCRC fail %d", state);
        set_error(buf);
        board.unlock();
        return false;
    }

    state = board.radio_.setPreambleLength(kFskPreambleLen);
    if (state != RADIOLIB_ERR_NONE)
    {
        Serial.printf("[WALKIE] setPreamble failed state=%d\n", state);
        char buf[64];
        snprintf(buf, sizeof(buf), "setPre fail %d", state);
        set_error(buf);
        board.unlock();
        return false;
    }

    board.unlock();
    return true;
}

void walkie_task(void*)
{
    TLoRaPagerBoard* board = TLoRaPagerBoard::getInstance();
    if (!board)
    {
        update_status_active(false);
        s_active = false;
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    CODEC2* codec2_state = codec2_create(CODEC2_MODE_3200);
    if (!codec2_state)
    {
        update_status_active(false);
        s_active = false;
        s_task = nullptr;
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
        update_status_active(false);
        s_active = false;
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    uint16_t seq = 0;
    uint16_t session_id = static_cast<uint16_t>(millis() & 0xFFFF);
    uint16_t tx_frame_counter = 0;
    uint32_t self_id = get_self_id();
    bool tx_mode = false;
    bool rx_started = false;
    uint8_t tx_level = 0;
    uint8_t rx_level = 0;
    uint32_t tx_read_ok = 0;
    uint32_t tx_read_fail = 0;
    int last_read_state = 0;
    uint32_t last_mic_log_ms = millis();
    uint32_t last_audio_log_ms = millis();
    int16_t last_tx_peak = 0;
    int16_t last_rx_peak = 0;
    uint32_t rx_pkts = 0;
    uint32_t rx_bad = 0;
    int last_rx_len = 0;
    int last_rx_state = 0;
    uint32_t last_rx_log_ms = millis();
    const uint32_t frame_interval_ms =
        static_cast<uint32_t>((samples_per_frame * 1000u) / kSampleRate);
    uint32_t last_play_ms = millis();
    uint32_t last_rx_frame_ms = 0;
    bool rx_play_active = false;
    size_t rx_target_prebuffer = kJitterMinPrebufferFrames;
    uint32_t rx_underruns = 0;
    uint32_t rx_good_windows = 0;
    uint32_t last_adapt_ms = millis();
    size_t rx_frame_count = 0;
    size_t rx_frame_head = 0;
    size_t rx_frame_tail = 0;
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
            last_adapt_ms = millis();
            tx_frame_count = 0;
            tx_frame_head = 0;
            tx_frame_tail = 0;
            tx_in_flight = false;
            if (tx_mode)
            {
                board->radio_.standby();
                session_id = static_cast<uint16_t>(session_id + 1);
                seq = 0;
                tx_frame_counter = 0;
            }
        }

        if (tx_mode)
        {
            int read_state = board->codec.read(reinterpret_cast<uint8_t*>(pcm_in_i2s),
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
                    uint32_t flags = board->getRadioIrqFlags();
                    if (flags & RADIOLIB_SX126X_IRQ_TX_DONE)
                    {
                        board->clearRadioIrqFlags(flags);
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
                    if (board->lock(pdMS_TO_TICKS(50)))
                    {
                        int tx_state = board->radio_.startTransmit(packet_buf, packet_size);
                        board->unlock();
                        if (tx_state == RADIOLIB_ERR_NONE)
                        {
                            tx_in_flight = true;
                        }
                        else
                        {
                            Serial.printf("[WALKIE] startTransmit failed state=%d\n", tx_state);
                        }
                    }
                    else
                    {
                        Serial.printf("[WALKIE] startTransmit lock failed\n");
                    }
                }
                update_status_levels(tx_level, rx_level);
            }
            else
            {
                tx_read_fail++;
            }

            uint32_t now_ms = millis();
            if (now_ms - last_mic_log_ms >= kMicLogIntervalMs)
            {
                Serial.printf("[WALKIE] mic read ok=%lu fail=%lu last=%d\n",
                              static_cast<unsigned long>(tx_read_ok),
                              static_cast<unsigned long>(tx_read_fail),
                              last_read_state);
                tx_read_ok = 0;
                tx_read_fail = 0;
                last_mic_log_ms = now_ms;
            }
            if (now_ms - last_audio_log_ms >= 1000)
            {
                Serial.printf("[WALKIE] tx lvl=%u peak=%d q=%u inflight=%d\n",
                              static_cast<unsigned>(tx_level),
                              static_cast<int>(last_tx_peak),
                              static_cast<unsigned>(tx_frame_count),
                              tx_in_flight ? 1 : 0);
                last_audio_log_ms = now_ms;
            }
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }

        if (!rx_started)
        {
            board->startRadioReceive();
            rx_started = true;
        }

        uint32_t irq = board->getRadioIrqFlags();
        if (irq & RADIOLIB_SX126X_IRQ_RX_DONE)
        {
            int len = board->getRadioPacketLength(true);
            last_rx_len = len;
            if (len > static_cast<int>(kHeaderSize) && len <= static_cast<int>(packet_size))
            {
                int read_state = board->readRadioData(packet_buf, len);
                last_rx_state = read_state;
                if (read_state == RADIOLIB_ERR_NONE)
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
                                last_adapt_ms = millis();
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
                                    last_rx_frame_ms = millis();
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
            board->clearRadioIrqFlags(irq);
            board->startRadioReceive();
        }
        else if (irq)
        {
            board->clearRadioIrqFlags(irq);
        }

        uint32_t now_ms = millis();
        if (now_ms - last_play_ms >= frame_interval_ms)
        {
            last_play_ms = now_ms;
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
                    last_rx_frame_ms = now_ms;
                }
                else
                {
                    if ((now_ms - last_rx_frame_ms) > (frame_interval_ms * 3))
                    {
                        rx_play_active = false;
                    }
                    memcpy(pcm_out, last_pcm_out, samples_per_frame * sizeof(int16_t));
                    rx_underruns++;
                }

                for (int j = 0; j < samples_per_frame; ++j)
                {
                    int32_t sample = static_cast<int32_t>(pcm_out[j] * kRxPcmGain);
                    if (sample > 32767) sample = 32767;
                    if (sample < -32768) sample = -32768;
                    pcm_out_i2s[j * 2] = static_cast<int16_t>(sample);
                    pcm_out_i2s[j * 2 + 1] = static_cast<int16_t>(sample);
                }
                rx_level = compute_level(pcm_out, samples_per_frame, rx_level);
                board->codec.write(reinterpret_cast<uint8_t*>(pcm_out_i2s),
                                   i2s_samples_per_frame * sizeof(int16_t));
                update_status_levels(tx_level, rx_level);
            }
            else
            {
                board->codec.write(reinterpret_cast<uint8_t*>(silence_i2s),
                                   i2s_samples_per_frame * sizeof(int16_t));
            }
        }

        if (now_ms - last_adapt_ms >= 1000)
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
            last_adapt_ms = now_ms;
        }

        if (now_ms - last_rx_log_ms >= kRxLogIntervalMs)
        {
            if (rx_pkts != 0 || rx_bad != 0)
            {
                Serial.printf("[WALKIE] rx ok=%lu bad=%lu last_len=%d state=%d\n",
                              static_cast<unsigned long>(rx_pkts),
                              static_cast<unsigned long>(rx_bad),
                              last_rx_len,
                              last_rx_state);
            }
            rx_pkts = 0;
            rx_bad = 0;
            last_rx_len = 0;
            last_rx_state = 0;
            last_rx_log_ms = now_ms;
        }
        if (now_ms - last_audio_log_ms >= 1000)
        {
            Serial.printf("[WALKIE] rx lvl=%u peak=%d buf=%u pre=%u underrun=%lu\n",
                          static_cast<unsigned>(rx_level),
                          static_cast<int>(last_rx_peak),
                          static_cast<unsigned>(rx_frame_count),
                          static_cast<unsigned>(rx_target_prebuffer),
                          static_cast<unsigned long>(rx_underruns));
            last_audio_log_ms = now_ms;
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

    board->codec.close();

    update_status_active(false);
    s_active = false;
    s_task = nullptr;
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

    set_error(nullptr);

    TLoRaPagerBoard* board = TLoRaPagerBoard::getInstance();
    if (!board || !board->isRadioOnline())
    {
        set_error("Radio not ready");
        Serial.printf("[WALKIE] start failed: radio not ready\n");
        return false;
    }
    if (!(board->getDevicesProbe() & HW_CODEC_ONLINE))
    {
        set_error("Codec not ready");
        Serial.printf("[WALKIE] start failed: codec not ready\n");
        return false;
    }

    Serial.printf("[WALKIE] pause radio tasks\n");
    app::AppTasks::pauseRadioTasks();

    auto& config = app::AppContext::getInstance().getConfig();
    float freq_mhz = chat::meshtastic::estimateFrequencyMhz(config.mesh_config.region,
                                                            config.mesh_config.modem_preset);
    if (freq_mhz <= 0.0f)
    {
        freq_mhz = 915.0f;
    }
    update_status_freq(freq_mhz);

    Serial.printf("[WALKIE] config freq=%.3f br=%.1f dev=%.1f rxBw=%.1f preamble=%u pwr=%d\n",
                  freq_mhz, kFskBitRateKbps, kFskFreqDevKHz, kFskRxBwKHz,
                  kFskPreambleLen, config.mesh_config.tx_power);

    if (!configure_fsk(*board, freq_mhz, config.mesh_config.tx_power))
    {
        app::AppContext::getInstance().applyMeshConfig();
        app::AppTasks::resumeRadioTasks();
        if (s_last_error[0] == '\0')
        {
            set_error("FSK config failed");
        }
        return false;
    }

    if (board->codec.open(kBitsPerSample, kI2sChannels, kSampleRate) != 0)
    {
        set_error("Codec open failed");
        Serial.printf("[WALKIE] codec open failed\n");
        app::AppContext::getInstance().applyMeshConfig();
        app::AppTasks::resumeRadioTasks();
        return false;
    }
    s_volume = kDefaultVolume;
    board->codec.setVolume(s_volume);
    board->codec.setGain(kDefaultGainDb);
    board->codec.setMute(false);

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
        board->codec.close();
        set_error("Task create failed");
        Serial.printf("[WALKIE] task create failed\n");
        app::AppContext::getInstance().applyMeshConfig();
        app::AppTasks::resumeRadioTasks();
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

    app::AppContext::getInstance().applyMeshConfig();
    app::AppTasks::resumeRadioTasks();
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
    TLoRaPagerBoard* board = TLoRaPagerBoard::getInstance();
    if (board)
    {
        board->codec.setVolume(s_volume);
    }
    Serial.printf("[WALKIE] volume=%u\n", static_cast<unsigned>(s_volume));
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
    Serial.printf("[WALKIE] PTT key state=%d\n", state);
    if (state == KEYBOARD_PRESSED)
    {
        set_ptt(true);
        update_status_tx(true);
    }
    else if (state == KEYBOARD_RELEASED)
    {
        set_ptt(false);
        update_status_tx(false);
    }
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
