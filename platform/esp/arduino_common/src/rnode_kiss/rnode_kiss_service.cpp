#include "platform/esp/arduino_common/rnode_kiss/rnode_kiss_service.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "app/app_facades.h"
#include "hostlink/hostlink_session.h"
#include "platform/esp/arduino_common/chat/infra/rnode/rnode_adapter.h"
#include "usb/usb_cdc_transport.h"

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace rnode_kiss
{
namespace
{
constexpr uint8_t kFend = 0xC0;
constexpr uint8_t kFesc = 0xDB;
constexpr uint8_t kTfend = 0xDC;
constexpr uint8_t kTfesc = 0xDD;

constexpr uint8_t kCmdData = 0x00;
constexpr uint8_t kCmdFrequency = 0x01;
constexpr uint8_t kCmdBandwidth = 0x02;
constexpr uint8_t kCmdTxPower = 0x03;
constexpr uint8_t kCmdSf = 0x04;
constexpr uint8_t kCmdCr = 0x05;
constexpr uint8_t kCmdRadioState = 0x06;
constexpr uint8_t kCmdDetect = 0x08;
constexpr uint8_t kCmdLeave = 0x0A;
constexpr uint8_t kCmdStALock = 0x0B;
constexpr uint8_t kCmdLtALock = 0x0C;
constexpr uint8_t kCmdReady = 0x0F;
constexpr uint8_t kCmdStatRx = 0x21;
constexpr uint8_t kCmdStatTx = 0x22;
constexpr uint8_t kCmdStatRssi = 0x23;
constexpr uint8_t kCmdStatSnr = 0x24;
constexpr uint8_t kCmdRandom = 0x40;
constexpr uint8_t kCmdFbExt = 0x41;
constexpr uint8_t kCmdBoard = 0x47;
constexpr uint8_t kCmdPlatform = 0x48;
constexpr uint8_t kCmdMcu = 0x49;
constexpr uint8_t kCmdFwVersion = 0x50;
constexpr uint8_t kCmdError = 0x90;

constexpr uint8_t kDetectReq = 0x73;
constexpr uint8_t kDetectResp = 0x46;

constexpr uint8_t kRadioStateOff = 0x00;
constexpr uint8_t kRadioStateOn = 0x01;
constexpr uint8_t kRadioStateAsk = 0xFF;

constexpr uint8_t kPlatformEsp32 = 0x80;
constexpr uint8_t kMcuEsp32 = 0x81;
constexpr uint8_t kBoardGenericEsp32 = 0x35;

constexpr uint8_t kErrorTxFailed = 0x02;
constexpr uint8_t kFwVersionMajor = 1;
constexpr uint8_t kFwVersionMinor = 52;
constexpr uint32_t kTaskPollMs = 20;
constexpr size_t kMaxFrameSize = 600;
constexpr size_t kUsbReadChunk = 96;

struct KissParser
{
    bool in_frame = false;
    bool escape = false;
    uint8_t buffer[kMaxFrameSize] = {};
    size_t length = 0;

    void reset()
    {
        in_frame = false;
        escape = false;
        length = 0;
    }
};

TaskHandle_t s_task = nullptr;
volatile bool s_stop = false;
hostlink::SessionRuntime s_session{};
uint8_t s_radio_state = kRadioStateOn;
uint16_t s_short_airtime_limit = 0;
uint16_t s_long_airtime_limit = 0;
bool s_host_seen = false;
uint32_t s_radio_rx_count = 0;
uint32_t s_radio_tx_count = 0;

void set_state(hostlink::LinkState state)
{
    hostlink::set_link_state(s_session, state);
}

chat::rnode::RNodeAdapter* get_backend()
{
    chat::IMeshAdapter* mesh = app::messagingFacade().getMeshAdapter();
    if (!mesh)
    {
        return nullptr;
    }

    chat::IMeshAdapter* backend = mesh->backendForProtocol(chat::MeshProtocol::RNode);
    return backend ? static_cast<chat::rnode::RNodeAdapter*>(backend) : nullptr;
}

bool apply_live_config()
{
    chat::rnode::RNodeAdapter* backend = get_backend();
    if (!backend)
    {
        return false;
    }

    backend->applyConfig(app::appFacade().getConfig().rnode_config);
    return true;
}

void note_rx()
{
    hostlink::note_rx(s_session);
}

void note_tx()
{
    hostlink::note_tx(s_session);
}

void note_error(uint32_t code)
{
    hostlink::note_error(s_session, code);
    set_state(hostlink::LinkState::Error);
}

void write_escaped(std::vector<uint8_t>& frame, const uint8_t* data, size_t len)
{
    if (!data || len == 0)
    {
        return;
    }

    for (size_t i = 0; i < len; ++i)
    {
        const uint8_t byte = data[i];
        if (byte == kFend)
        {
            frame.push_back(kFesc);
            frame.push_back(kTfend);
        }
        else if (byte == kFesc)
        {
            frame.push_back(kFesc);
            frame.push_back(kTfesc);
        }
        else
        {
            frame.push_back(byte);
        }
    }
}

bool send_frame(uint8_t command, const uint8_t* payload, size_t len)
{
    if (!usb_cdc::get_status().started)
    {
        return false;
    }

    std::vector<uint8_t> frame;
    frame.reserve(len + 4);
    frame.push_back(kFend);
    frame.push_back(command);
    write_escaped(frame, payload, len);
    frame.push_back(kFend);

    if (usb_cdc::write(frame.data(), frame.size()) != frame.size())
    {
        note_error(kErrorTxFailed);
        return false;
    }

    note_tx();
    return true;
}

bool send_u32(uint8_t command, uint32_t value)
{
    const uint8_t payload[4] = {
        static_cast<uint8_t>((value >> 24) & 0xFF),
        static_cast<uint8_t>((value >> 16) & 0xFF),
        static_cast<uint8_t>((value >> 8) & 0xFF),
        static_cast<uint8_t>(value & 0xFF),
    };
    return send_frame(command, payload, sizeof(payload));
}

bool send_u16(uint8_t command, uint16_t value)
{
    const uint8_t payload[2] = {
        static_cast<uint8_t>((value >> 8) & 0xFF),
        static_cast<uint8_t>(value & 0xFF),
    };
    return send_frame(command, payload, sizeof(payload));
}

bool send_u8(uint8_t command, uint8_t value)
{
    return send_frame(command, &value, 1);
}

int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return value;
}

void send_ready()
{
    const uint8_t ready = 0x01;
    (void)send_frame(kCmdReady, &ready, 1);
}

void reply_current_config(uint8_t command)
{
    const chat::MeshConfig& cfg = app::appFacade().getConfig().rnode_config;
    switch (command)
    {
    case kCmdFrequency:
        (void)send_u32(kCmdFrequency,
                       static_cast<uint32_t>(std::lround(cfg.override_frequency_mhz * 1000000.0f)));
        break;
    case kCmdBandwidth:
        (void)send_u32(kCmdBandwidth,
                       static_cast<uint32_t>(std::lround(cfg.bandwidth_khz * 1000.0f)));
        break;
    case kCmdTxPower:
        (void)send_u8(kCmdTxPower, static_cast<uint8_t>(cfg.tx_power));
        break;
    case kCmdSf:
        (void)send_u8(kCmdSf, cfg.spread_factor);
        break;
    case kCmdCr:
        (void)send_u8(kCmdCr, cfg.coding_rate);
        break;
    case kCmdRadioState:
        (void)send_u8(kCmdRadioState, s_radio_state);
        break;
    case kCmdStALock:
        (void)send_u16(kCmdStALock, s_short_airtime_limit);
        break;
    case kCmdLtALock:
        (void)send_u16(kCmdLtALock, s_long_airtime_limit);
        break;
    case kCmdFwVersion:
    {
        const uint8_t version[2] = {kFwVersionMajor, kFwVersionMinor};
        (void)send_frame(kCmdFwVersion, version, sizeof(version));
        break;
    }
    case kCmdPlatform:
        (void)send_u8(kCmdPlatform, kPlatformEsp32);
        break;
    case kCmdMcu:
        (void)send_u8(kCmdMcu, kMcuEsp32);
        break;
    case kCmdBoard:
        (void)send_u8(kCmdBoard, kBoardGenericEsp32);
        break;
    case kCmdStatRx:
        (void)send_u32(kCmdStatRx, s_radio_rx_count);
        break;
    case kCmdStatTx:
        (void)send_u32(kCmdStatTx, s_radio_tx_count);
        break;
    case kCmdRandom:
        (void)send_u8(kCmdRandom, static_cast<uint8_t>(esp_random() & 0xFF));
        break;
    default:
        break;
    }
}

uint32_t decode_u32(const uint8_t* payload, size_t len)
{
    if (!payload || len < 4)
    {
        return 0;
    }
    return (static_cast<uint32_t>(payload[0]) << 24) |
           (static_cast<uint32_t>(payload[1]) << 16) |
           (static_cast<uint32_t>(payload[2]) << 8) |
           static_cast<uint32_t>(payload[3]);
}

void send_last_rx_stats(chat::rnode::RNodeAdapter& backend)
{
    const float rssi = backend.lastRxRssi();
    const float snr = backend.lastRxSnr();

    int rssi_encoded = static_cast<int>(std::lround(rssi + 157.0f));
    rssi_encoded = clamp_int(rssi_encoded, 0, 255);

    int snr_encoded = static_cast<int>(std::lround(snr * 4.0f));
    snr_encoded = clamp_int(snr_encoded, -128, 127);

    (void)send_u8(kCmdStatRssi, static_cast<uint8_t>(rssi_encoded));
    (void)send_u8(kCmdStatSnr, static_cast<uint8_t>(static_cast<int8_t>(snr_encoded)));
}

void process_command(uint8_t command, const uint8_t* payload, size_t len)
{
    s_host_seen = true;
    if (s_session.status.state != hostlink::LinkState::Error)
    {
        set_state(hostlink::LinkState::Ready);
    }
    note_rx();

    app::IAppFacade& app_ctx = app::appFacade();
    chat::MeshConfig& cfg = app_ctx.getConfig().rnode_config;
    chat::rnode::RNodeAdapter* backend = get_backend();

    switch (command)
    {
    case kCmdDetect:
        if (len == 0 || payload[0] == kDetectReq)
        {
            (void)send_u8(kCmdDetect, kDetectResp);
        }
        break;
    case kCmdFwVersion:
    case kCmdPlatform:
    case kCmdMcu:
    case kCmdBoard:
    case kCmdStatRx:
    case kCmdStatTx:
    case kCmdRandom:
        reply_current_config(command);
        break;
    case kCmdFrequency:
        if (len >= 4)
        {
            cfg.override_frequency_mhz = static_cast<float>(decode_u32(payload, len)) / 1000000.0f;
            if (s_radio_state == kRadioStateOn)
            {
                (void)apply_live_config();
            }
        }
        reply_current_config(kCmdFrequency);
        break;
    case kCmdBandwidth:
        if (len >= 4)
        {
            cfg.bandwidth_khz = static_cast<float>(decode_u32(payload, len)) / 1000.0f;
            if (s_radio_state == kRadioStateOn)
            {
                (void)apply_live_config();
            }
        }
        reply_current_config(kCmdBandwidth);
        break;
    case kCmdTxPower:
        if (len >= 1)
        {
            cfg.tx_power = static_cast<int8_t>(payload[0]);
            if (s_radio_state == kRadioStateOn)
            {
                (void)apply_live_config();
            }
        }
        reply_current_config(kCmdTxPower);
        break;
    case kCmdSf:
        if (len >= 1)
        {
            cfg.spread_factor = payload[0];
            if (s_radio_state == kRadioStateOn)
            {
                (void)apply_live_config();
            }
        }
        reply_current_config(kCmdSf);
        break;
    case kCmdCr:
        if (len >= 1)
        {
            cfg.coding_rate = payload[0];
            if (s_radio_state == kRadioStateOn)
            {
                (void)apply_live_config();
            }
        }
        reply_current_config(kCmdCr);
        break;
    case kCmdStALock:
        if (len >= 2)
        {
            s_short_airtime_limit = static_cast<uint16_t>((payload[0] << 8) | payload[1]);
        }
        reply_current_config(kCmdStALock);
        break;
    case kCmdLtALock:
        if (len >= 2)
        {
            s_long_airtime_limit = static_cast<uint16_t>((payload[0] << 8) | payload[1]);
        }
        reply_current_config(kCmdLtALock);
        break;
    case kCmdRadioState:
        if (len >= 1 && payload[0] != kRadioStateAsk)
        {
            s_radio_state = (payload[0] == kRadioStateOff) ? kRadioStateOff : kRadioStateOn;
            if (s_radio_state == kRadioStateOn)
            {
                (void)apply_live_config();
            }
        }
        reply_current_config(kCmdRadioState);
        send_ready();
        break;
    case kCmdFbExt:
        send_ready();
        break;
    case kCmdLeave:
        s_host_seen = false;
        if (s_session.status.state != hostlink::LinkState::Error)
        {
            set_state(hostlink::LinkState::Connected);
        }
        break;
    case kCmdData:
        if (s_radio_state != kRadioStateOn || !backend || !payload || len == 0)
        {
            (void)send_u8(kCmdError, kErrorTxFailed);
            break;
        }
        if (backend->sendAppData(chat::ChannelId::PRIMARY, 0, payload, len))
        {
            s_radio_tx_count++;
            (void)send_u32(kCmdStatTx, s_radio_tx_count);
            send_ready();
        }
        else
        {
            (void)send_u8(kCmdError, kErrorTxFailed);
        }
        break;
    default:
        break;
    }
}

void feed_parser(KissParser& parser, uint8_t byte)
{
    if (byte == kFend)
    {
        if (parser.in_frame && parser.length > 0)
        {
            const uint8_t command = parser.buffer[0];
            const uint8_t* payload = (parser.length > 1) ? &parser.buffer[1] : nullptr;
            const size_t payload_len = (parser.length > 1) ? (parser.length - 1) : 0;
            process_command(command, payload, payload_len);
        }

        parser.in_frame = true;
        parser.escape = false;
        parser.length = 0;
        return;
    }

    if (!parser.in_frame)
    {
        return;
    }

    if (parser.escape)
    {
        if (byte == kTfend)
        {
            byte = kFend;
        }
        else if (byte == kTfesc)
        {
            byte = kFesc;
        }
        parser.escape = false;
    }
    else if (byte == kFesc)
    {
        parser.escape = true;
        return;
    }

    if (parser.length < sizeof(parser.buffer))
    {
        parser.buffer[parser.length++] = byte;
    }
}

void pump_host_rx(KissParser& parser)
{
    uint8_t buffer[kUsbReadChunk] = {};
    const size_t len = usb_cdc::read(buffer, sizeof(buffer));
    for (size_t i = 0; i < len; ++i)
    {
        feed_parser(parser, buffer[i]);
    }
}

void pump_radio_rx()
{
    if (s_radio_state != kRadioStateOn)
    {
        return;
    }

    chat::rnode::RNodeAdapter* backend = get_backend();
    if (!backend)
    {
        return;
    }

    uint8_t packet[chat::rnode::kRNodeMaxPayloadSize] = {};
    size_t packet_len = 0;
    if (!backend->pollIncomingRawPacket(packet, packet_len, sizeof(packet)) || packet_len == 0)
    {
        return;
    }

    send_last_rx_stats(*backend);
    if (send_frame(kCmdData, packet, packet_len))
    {
        s_radio_rx_count++;
        (void)send_u32(kCmdStatRx, s_radio_rx_count);
    }
}

void reset_runtime()
{
    hostlink::reset_session(s_session, 0);
    s_radio_state = kRadioStateOn;
    s_short_airtime_limit = 0;
    s_long_airtime_limit = 0;
    s_host_seen = false;
    s_radio_rx_count = 0;
    s_radio_tx_count = 0;
}

void rnode_task(void* /*arg*/)
{
    KissParser parser{};
    reset_runtime();
    set_state(hostlink::LinkState::Waiting);
    (void)usb_cdc::start();

    while (!s_stop)
    {
        if (!usb_cdc::is_connected())
        {
            parser.reset();
            s_host_seen = false;
            if (s_session.status.state != hostlink::LinkState::Waiting)
            {
                set_state(hostlink::LinkState::Waiting);
            }
            vTaskDelay(pdMS_TO_TICKS(kTaskPollMs));
            continue;
        }

        if (!s_host_seen && s_session.status.state != hostlink::LinkState::Connected)
        {
            set_state(hostlink::LinkState::Connected);
        }

        pump_host_rx(parser);
        pump_radio_rx();
        vTaskDelay(pdMS_TO_TICKS(kTaskPollMs));
    }

    hostlink::stop_session(s_session);
    usb_cdc::stop();
    s_task = nullptr;
    vTaskDelete(nullptr);
}

} // namespace

void start()
{
    if (s_task != nullptr)
    {
        return;
    }

    s_stop = false;
    xTaskCreate(rnode_task, "rnode_kiss", 6 * 1024, nullptr, 5, &s_task);
}

void stop()
{
    if (s_task == nullptr)
    {
        return;
    }

    s_stop = true;
    for (int attempts = 0; attempts < 25 && s_task != nullptr; ++attempts)
    {
        vTaskDelay(pdMS_TO_TICKS(kTaskPollMs));
    }

    if (s_task != nullptr)
    {
        vTaskDelete(s_task);
        s_task = nullptr;
        hostlink::stop_session(s_session);
        usb_cdc::stop();
    }
}

bool is_active()
{
    return s_task != nullptr;
}

hostlink::Status get_status()
{
    return s_session.status;
}

} // namespace rnode_kiss
