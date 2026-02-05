#include "hostlink_service.h"

#include "../usb/usb_cdc_transport.h"
#include "hostlink_codec.h"
#include "hostlink_config_service.h"
#include "hostlink_types.h"

#include "../app/app_context.h"
#include "../board/BoardBase.h"
#include "../gps/gps_service_api.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <Arduino.h>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace hostlink
{

namespace
{
constexpr uint32_t kHandshakeTimeoutMs = 5000;
constexpr uint32_t kStatusIntervalMs = 1500;
constexpr uint32_t kGpsIntervalMs = 1000;
constexpr size_t kTxQueueSize = 12;

struct TxItem
{
    uint8_t* data = nullptr;
    size_t len = 0;
};

TaskHandle_t s_task = nullptr;
QueueHandle_t s_tx_queue = nullptr;
volatile bool s_stop = false;
Status s_status{};
uint16_t s_tx_seq = 1;

Decoder s_decoder(kMaxFrameLen);

uint32_t s_handshake_deadline = 0;
uint32_t s_last_status_ms = 0;
uint32_t s_last_gps_ms = 0;

void push_u16(std::vector<uint8_t>& out, uint16_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void push_u32(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void push_i32(std::vector<uint8_t>& out, int32_t v)
{
    push_u32(out, static_cast<uint32_t>(v));
}

void set_state(LinkState st)
{
    s_status.state = st;
}

uint16_t next_seq()
{
    if (++s_tx_seq == 0)
    {
        s_tx_seq = 1;
    }
    return s_tx_seq;
}

void send_raw(const std::vector<uint8_t>& frame)
{
    if (frame.empty())
    {
        return;
    }
    size_t sent = usb_cdc::write(frame.data(), frame.size());
    if (sent > 0)
    {
        s_status.tx_count++;
    }
}

void send_ack(uint16_t seq, ErrorCode code)
{
    uint8_t payload[1] = {static_cast<uint8_t>(code)};
    std::vector<uint8_t> frame;
    if (encode_frame(static_cast<uint8_t>(FrameType::Ack), seq, payload, sizeof(payload), frame))
    {
        send_raw(frame);
    }
}

void send_hello_ack(uint16_t seq)
{
    std::vector<uint8_t> payload;
    payload.reserve(32);
    const uint16_t proto = kProtocolVersion;
    const uint16_t max_len = static_cast<uint16_t>(kMaxFrameLen);
    const uint32_t caps = CapTxMsg | CapConfig | CapSetTime | CapStatus | CapLogs | CapGps | CapAppData;

    payload.push_back(static_cast<uint8_t>(proto & 0xFF));
    payload.push_back(static_cast<uint8_t>((proto >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>(max_len & 0xFF));
    payload.push_back(static_cast<uint8_t>((max_len >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>(caps & 0xFF));
    payload.push_back(static_cast<uint8_t>((caps >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>((caps >> 16) & 0xFF));
    payload.push_back(static_cast<uint8_t>((caps >> 24) & 0xFF));

    const char* model = "TrailMate";
    const char* fw = "dev";
    const uint8_t model_len = static_cast<uint8_t>(strlen(model));
    const uint8_t fw_len = static_cast<uint8_t>(strlen(fw));
    payload.push_back(model_len);
    payload.insert(payload.end(), model, model + model_len);
    payload.push_back(fw_len);
    payload.insert(payload.end(), fw, fw + fw_len);

    std::vector<uint8_t> frame;
    if (encode_frame(static_cast<uint8_t>(FrameType::HelloAck), seq,
                     payload.data(), payload.size(), frame))
    {
        send_raw(frame);
    }
}

bool send_status_event()
{
    std::vector<uint8_t> payload;
    if (!build_status_payload(payload,
                              static_cast<uint8_t>(s_status.state),
                              s_status.last_error))
    {
        return false;
    }
    return enqueue_event(static_cast<uint8_t>(FrameType::EvStatus),
                         payload.data(), payload.size(), false);
}

bool send_gps_event()
{
    gps::GpsState gps_state = gps::gps_get_data();
    std::vector<uint8_t> payload;
    payload.reserve(24);

    uint8_t flags = 0;
    if (gps_state.valid)
    {
        flags |= 0x01;
    }
    if (gps_state.has_alt)
    {
        flags |= 0x02;
    }
    if (gps_state.has_speed)
    {
        flags |= 0x04;
    }
    if (gps_state.has_course)
    {
        flags |= 0x08;
    }

    payload.push_back(flags);
    payload.push_back(gps_state.satellites);
    push_u32(payload, gps_state.age);

    int32_t lat_e7 = 0;
    int32_t lon_e7 = 0;
    if (gps_state.valid)
    {
        lat_e7 = static_cast<int32_t>(std::lround(gps_state.lat * 10000000.0));
        lon_e7 = static_cast<int32_t>(std::lround(gps_state.lng * 10000000.0));
    }
    push_i32(payload, lat_e7);
    push_i32(payload, lon_e7);

    int32_t alt_cm = 0;
    if (gps_state.has_alt)
    {
        alt_cm = static_cast<int32_t>(std::lround(gps_state.alt_m * 100.0));
    }
    push_i32(payload, alt_cm);

    uint16_t speed_cms = 0;
    if (gps_state.has_speed)
    {
        double speed = gps_state.speed_mps * 100.0;
        if (speed < 0.0)
        {
            speed = 0.0;
        }
        if (speed > 65535.0)
        {
            speed = 65535.0;
        }
        speed_cms = static_cast<uint16_t>(std::lround(speed));
    }
    push_u16(payload, speed_cms);

    uint16_t course_cdeg = 0;
    if (gps_state.has_course)
    {
        double course = gps_state.course_deg * 100.0;
        if (course < 0.0)
        {
            course = 0.0;
        }
        if (course > 35999.0)
        {
            course = 35999.0;
        }
        course_cdeg = static_cast<uint16_t>(std::lround(course));
    }
    push_u16(payload, course_cdeg);

    return enqueue_event(static_cast<uint8_t>(FrameType::EvGps),
                         payload.data(), payload.size(), false);
}

bool parse_u16(const uint8_t* data, size_t len, size_t& off, uint16_t& out)
{
    if (off + 2 > len)
    {
        return false;
    }
    out = static_cast<uint16_t>(data[off]) |
          (static_cast<uint16_t>(data[off + 1]) << 8);
    off += 2;
    return true;
}

bool parse_u32(const uint8_t* data, size_t len, size_t& off, uint32_t& out)
{
    if (off + 4 > len)
    {
        return false;
    }
    out = static_cast<uint32_t>(data[off]) |
          (static_cast<uint32_t>(data[off + 1]) << 8) |
          (static_cast<uint32_t>(data[off + 2]) << 16) |
          (static_cast<uint32_t>(data[off + 3]) << 24);
    off += 4;
    return true;
}

bool parse_u64(const uint8_t* data, size_t len, size_t& off, uint64_t& out)
{
    if (off + 8 > len)
    {
        return false;
    }
    out = 0;
    for (int i = 0; i < 8; ++i)
    {
        out |= static_cast<uint64_t>(data[off + i]) << (8 * i);
    }
    off += 8;
    return true;
}

ErrorCode handle_cmd_tx_msg(const Frame& frame)
{
    size_t off = 0;
    uint32_t to = 0;
    if (!parse_u32(frame.payload.data(), frame.payload.size(), off, to))
    {
        return ErrorCode::InvalidParam;
    }
    if (off >= frame.payload.size())
    {
        return ErrorCode::InvalidParam;
    }
    uint8_t channel = frame.payload[off++];
    if (off >= frame.payload.size())
    {
        return ErrorCode::InvalidParam;
    }
    uint8_t flags = frame.payload[off++];
    (void)flags;

    uint16_t text_len = 0;
    if (!parse_u16(frame.payload.data(), frame.payload.size(), off, text_len))
    {
        return ErrorCode::InvalidParam;
    }
    if (off + text_len > frame.payload.size())
    {
        return ErrorCode::InvalidParam;
    }

    std::string text(reinterpret_cast<const char*>(frame.payload.data() + off), text_len);
    auto& app = app::AppContext::getInstance();
    chat::ChannelId ch = static_cast<chat::ChannelId>(channel);
    chat::MessageId msg_id = app.getChatService().sendText(ch, text, to);
    if (msg_id == 0)
    {
        return ErrorCode::Busy;
    }
    return ErrorCode::Ok;
}

ErrorCode handle_cmd_get_config()
{
    send_status_event();
    return ErrorCode::Ok;
}

ErrorCode handle_cmd_get_gps()
{
    if (!send_gps_event())
    {
        return ErrorCode::Busy;
    }
    return ErrorCode::Ok;
}

ErrorCode handle_cmd_set_config(const Frame& frame)
{
    uint32_t err = 0;
    if (!apply_config(frame.payload.data(), frame.payload.size(), &err))
    {
        s_status.last_error = err;
        return ErrorCode::InvalidParam;
    }
    send_status_event();
    return ErrorCode::Ok;
}

ErrorCode handle_cmd_set_time(const Frame& frame)
{
    if (frame.payload.size() < 8)
    {
        return ErrorCode::InvalidParam;
    }
    size_t off = 0;
    uint64_t epoch = 0;
    if (!parse_u64(frame.payload.data(), frame.payload.size(), off, epoch))
    {
        return ErrorCode::InvalidParam;
    }
    if (!set_time_epoch(epoch))
    {
        return ErrorCode::Internal;
    }
    return ErrorCode::Ok;
}

void handle_frame(const Frame& frame)
{
    const bool ready = (s_status.state == LinkState::Ready);
    if (!ready && frame.type != static_cast<uint8_t>(FrameType::Hello))
    {
        send_ack(frame.seq, ErrorCode::NotInMode);
        return;
    }

    if (frame.type == static_cast<uint8_t>(FrameType::Hello))
    {
        send_hello_ack(frame.seq);
        set_state(LinkState::Ready);
        s_handshake_deadline = 0;
        return;
    }

    ErrorCode result = ErrorCode::Unsupported;
    switch (static_cast<FrameType>(frame.type))
    {
    case FrameType::CmdTxMsg:
        result = handle_cmd_tx_msg(frame);
        break;
    case FrameType::CmdGetConfig:
        result = handle_cmd_get_config();
        break;
    case FrameType::CmdSetConfig:
        result = handle_cmd_set_config(frame);
        break;
    case FrameType::CmdSetTime:
        result = handle_cmd_set_time(frame);
        break;
    case FrameType::CmdGetGps:
        result = handle_cmd_get_gps();
        break;
    default:
        result = ErrorCode::Unsupported;
        break;
    }
    if (result != ErrorCode::Ok)
    {
        s_status.last_error = static_cast<uint32_t>(result);
    }
    send_ack(frame.seq, result);
}

void hostlink_task(void* /*arg*/)
{
    usb_cdc::start();
    set_state(LinkState::Waiting);
    s_status.rx_count = 0;
    s_status.tx_count = 0;
    s_status.last_error = 0;
    s_handshake_deadline = 0;
    s_last_status_ms = millis();
    s_last_gps_ms = millis();
    s_decoder.reset();

    uint8_t rx_buf[128];

    while (!s_stop)
    {
        const bool connected = usb_cdc::is_connected();
        if (!connected)
        {
            set_state(LinkState::Waiting);
            s_handshake_deadline = 0;
            s_decoder.reset();
        }
        else
        {
            if (s_status.state == LinkState::Waiting)
            {
                set_state(LinkState::Handshaking);
                s_handshake_deadline = millis() + kHandshakeTimeoutMs;
            }
        }

        size_t n = usb_cdc::read(rx_buf, sizeof(rx_buf));
        if (n > 0)
        {
            s_status.rx_count++;
            s_decoder.push(rx_buf, n);
        }

        Frame frame;
        while (s_decoder.next(frame))
        {
            handle_frame(frame);
        }

        if (s_status.state == LinkState::Handshaking && s_handshake_deadline)
        {
            if (millis() > s_handshake_deadline)
            {
                set_state(LinkState::Waiting);
                s_handshake_deadline = 0;
            }
        }

        if (s_status.state == LinkState::Ready &&
            millis() - s_last_status_ms >= kStatusIntervalMs)
        {
            send_status_event();
            s_last_status_ms = millis();
        }
        if (s_status.state == LinkState::Ready &&
            millis() - s_last_gps_ms >= kGpsIntervalMs)
        {
            send_gps_event();
            s_last_gps_ms = millis();
        }

        TxItem item;
        while (s_status.state == LinkState::Ready &&
               s_tx_queue &&
               xQueueReceive(s_tx_queue, &item, 0) == pdPASS)
        {
            if (item.data && item.len)
            {
                usb_cdc::write(item.data, item.len);
                s_status.tx_count++;
            }
            if (item.data)
            {
                free(item.data);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    usb_cdc::stop();
    set_state(LinkState::Stopped);
    vTaskDelete(nullptr);
}

} // namespace

void start()
{
    if (s_task)
    {
        return;
    }
    s_stop = false;
    if (!s_tx_queue)
    {
        s_tx_queue = xQueueCreate(kTxQueueSize, sizeof(TxItem));
    }
    xTaskCreate(hostlink_task, "hostlink", 6 * 1024, nullptr, 5, &s_task);
}

void stop()
{
    s_stop = true;
    if (s_task)
    {
        vTaskDelete(s_task);
        s_task = nullptr;
    }
    if (s_tx_queue)
    {
        TxItem item;
        while (xQueueReceive(s_tx_queue, &item, 0) == pdPASS)
        {
            if (item.data)
            {
                free(item.data);
            }
        }
        vQueueDelete(s_tx_queue);
        s_tx_queue = nullptr;
    }
    usb_cdc::stop();
    set_state(LinkState::Stopped);
}

bool is_active()
{
    return s_task != nullptr;
}

Status get_status()
{
    return s_status;
}

bool enqueue_event(uint8_t type, const uint8_t* payload, size_t len, bool high_priority)
{
    if (!s_tx_queue || s_status.state != LinkState::Ready)
    {
        return false;
    }
    std::vector<uint8_t> frame;
    if (!encode_frame(type, next_seq(), payload, len, frame))
    {
        return false;
    }

    TxItem item;
    item.len = frame.size();
    item.data = static_cast<uint8_t*>(malloc(item.len));
    if (!item.data)
    {
        return false;
    }
    memcpy(item.data, frame.data(), item.len);

    if (xQueueSend(s_tx_queue, &item, 0) != pdPASS)
    {
        if (!high_priority)
        {
            free(item.data);
            return false;
        }
        TxItem drop;
        if (xQueueReceive(s_tx_queue, &drop, 0) == pdPASS)
        {
            if (drop.data)
            {
                free(drop.data);
            }
            if (xQueueSend(s_tx_queue, &item, 0) == pdPASS)
            {
                return true;
            }
        }
        free(item.data);
        return false;
    }
    return true;
}

} // namespace hostlink
