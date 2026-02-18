#include "hostlink_service.h"

#include "../usb/usb_cdc_transport.h"
#include "hostlink_bridge_radio.h"
#include "hostlink_codec.h"
#include "hostlink_config_service.h"
#include "hostlink_types.h"

#include "../app/app_context.h"
#include "../board/BoardBase.h"
#include "../gps/gps_service_api.h"
#include "../team/protocol/team_chat.h"
#include "../team/protocol/team_mgmt.h"
#include "../team/protocol/team_portnum.h"
#include "../team/usecase/team_controller.h"

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
constexpr size_t kCmdQueueSize = 12;
constexpr uint8_t kCmdTxAppFlagWantResponse = 1 << 0;
constexpr uint8_t kCmdTxAppFlagTeamMgmtPlain = 1 << 1;

struct TxItem
{
    uint8_t* data = nullptr;
    size_t len = 0;
};

enum class PendingCommandType : uint8_t
{
    TxMsg = 1,
    TxAppData = 2,
};

struct PendingCommand
{
    PendingCommandType type = PendingCommandType::TxMsg;
    uint32_t to = 0;
    uint32_t portnum = 0;
    uint8_t channel = 0;
    uint8_t flags = 0;
    uint16_t payload_len = 0;
    uint8_t payload[kMaxFrameLen] = {};
};

TaskHandle_t s_task = nullptr;
QueueHandle_t s_tx_queue = nullptr;
QueueHandle_t s_cmd_queue = nullptr;
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
    const uint32_t caps =
        CapTxMsg | CapConfig | CapSetTime | CapStatus | CapLogs | CapGps |
        CapAppData | CapTeamState | CapAprsGateway | CapTxAppData;

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

bool send_status_event(bool include_config)
{
    std::vector<uint8_t> payload;
    if (!build_status_payload(payload,
                              static_cast<uint8_t>(s_status.state),
                              s_status.last_error,
                              include_config))
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

bool try_parse_cmd_tx_app_data_legacy(const Frame& frame, PendingCommand* command)
{
    if (!command)
    {
        return false;
    }

    size_t off = 0;
    uint32_t portnum = 0;
    uint32_t to = 0;
    uint16_t payload_len = 0;
    if (!parse_u32(frame.payload.data(), frame.payload.size(), off, portnum))
    {
        return false;
    }
    if (!parse_u32(frame.payload.data(), frame.payload.size(), off, to))
    {
        return false;
    }
    if (off + 2 > frame.payload.size())
    {
        return false;
    }
    uint8_t channel = frame.payload[off++];
    uint8_t flags = frame.payload[off++];
    if (!parse_u16(frame.payload.data(), frame.payload.size(), off, payload_len))
    {
        return false;
    }
    if (off + payload_len != frame.payload.size())
    {
        return false;
    }
    if (channel >= static_cast<uint8_t>(chat::ChannelId::MAX_CHANNELS))
    {
        return false;
    }
    if (payload_len > kMaxFrameLen)
    {
        return false;
    }

    command->type = PendingCommandType::TxAppData;
    command->to = to;
    command->portnum = portnum;
    command->channel = channel;
    command->flags = flags;
    command->payload_len = payload_len;
    if (payload_len > 0)
    {
        memcpy(command->payload, frame.payload.data() + off, payload_len);
    }
    return true;
}

bool try_parse_cmd_tx_app_data_extended(const Frame& frame,
                                        bool include_timestamp_field,
                                        PendingCommand* command)
{
    if (!command)
    {
        return false;
    }

    size_t off = 0;
    uint32_t portnum = 0;
    uint32_t from = 0;
    uint32_t to = 0;
    uint32_t team_key_id = 0;
    uint32_t timestamp_s = 0;
    uint32_t total_len = 0;
    uint32_t chunk_offset = 0;
    uint16_t chunk_len = 0;
    if (!parse_u32(frame.payload.data(), frame.payload.size(), off, portnum))
    {
        return false;
    }
    if (!parse_u32(frame.payload.data(), frame.payload.size(), off, from))
    {
        return false;
    }
    if (!parse_u32(frame.payload.data(), frame.payload.size(), off, to))
    {
        return false;
    }
    if (off + 2 > frame.payload.size())
    {
        return false;
    }
    uint8_t channel = frame.payload[off++];
    uint8_t flags = frame.payload[off++];
    if (off + team::proto::kTeamIdSize > frame.payload.size())
    {
        return false;
    }
    off += team::proto::kTeamIdSize;
    if (!parse_u32(frame.payload.data(), frame.payload.size(), off, team_key_id))
    {
        return false;
    }
    if (include_timestamp_field &&
        !parse_u32(frame.payload.data(), frame.payload.size(), off, timestamp_s))
    {
        return false;
    }
    if (!parse_u32(frame.payload.data(), frame.payload.size(), off, total_len) ||
        !parse_u32(frame.payload.data(), frame.payload.size(), off, chunk_offset) ||
        !parse_u16(frame.payload.data(), frame.payload.size(), off, chunk_len))
    {
        return false;
    }
    if (off + chunk_len != frame.payload.size())
    {
        return false;
    }
    if (channel >= static_cast<uint8_t>(chat::ChannelId::MAX_CHANNELS))
    {
        return false;
    }
    if (chunk_len > kMaxFrameLen)
    {
        return false;
    }
    // Current command queue carries one payload per command; require full payload in one frame.
    if (chunk_offset != 0 || total_len != chunk_len)
    {
        return false;
    }

    (void)from;
    (void)team_key_id;
    (void)timestamp_s;

    command->type = PendingCommandType::TxAppData;
    command->to = to;
    command->portnum = portnum;
    command->channel = channel;
    command->flags = flags;
    command->payload_len = chunk_len;
    if (chunk_len > 0)
    {
        memcpy(command->payload, frame.payload.data() + off, chunk_len);
    }
    return true;
}

ErrorCode map_team_send_error(team::TeamService::SendError err)
{
    switch (err)
    {
    case team::TeamService::SendError::None:
        return ErrorCode::Ok;
    case team::TeamService::SendError::KeysNotReady:
        return ErrorCode::NotInMode;
    case team::TeamService::SendError::MeshSendFail:
        return ErrorCode::Busy;
    case team::TeamService::SendError::EncodeFail:
    case team::TeamService::SendError::EncryptFail:
    default:
        return ErrorCode::Internal;
    }
}

ErrorCode map_team_send_result(bool ok, team::TeamController* controller)
{
    if (ok)
    {
        return ErrorCode::Ok;
    }
    if (!controller)
    {
        return ErrorCode::Internal;
    }
    return map_team_send_error(controller->getLastSendError());
}

ErrorCode send_team_mgmt_wire(team::TeamController* controller,
                              const uint8_t* payload, size_t payload_len,
                              chat::ChannelId channel, chat::NodeId to,
                              bool want_response, bool prefer_plain)
{
    if (!controller || !payload)
    {
        return ErrorCode::Internal;
    }

    uint8_t version = 0;
    team::proto::TeamMgmtType type = team::proto::TeamMgmtType::Status;
    std::vector<uint8_t> mgmt_payload;
    if (!team::proto::decodeTeamMgmtMessage(payload, payload_len,
                                            &version, &type, mgmt_payload))
    {
        return ErrorCode::InvalidParam;
    }
    if (version != team::proto::kTeamMgmtVersion)
    {
        return ErrorCode::InvalidParam;
    }

    bool ok = false;
    switch (type)
    {
    case team::proto::TeamMgmtType::Kick:
    {
        team::proto::TeamKick msg;
        if (!team::proto::decodeTeamKick(mgmt_payload.data(), mgmt_payload.size(), &msg))
        {
            return ErrorCode::InvalidParam;
        }
        ok = controller->onKick(msg, channel, to, want_response);
        break;
    }
    case team::proto::TeamMgmtType::TransferLeader:
    {
        team::proto::TeamTransferLeader msg;
        if (!team::proto::decodeTeamTransferLeader(mgmt_payload.data(), mgmt_payload.size(), &msg))
        {
            return ErrorCode::InvalidParam;
        }
        ok = controller->onTransferLeader(msg, channel, to, want_response);
        break;
    }
    case team::proto::TeamMgmtType::KeyDist:
    {
        team::proto::TeamKeyDist msg;
        if (!team::proto::decodeTeamKeyDist(mgmt_payload.data(), mgmt_payload.size(), &msg))
        {
            return ErrorCode::InvalidParam;
        }
        ok = prefer_plain
                 ? controller->onKeyDistPlain(msg, channel, to, want_response)
                 : controller->onKeyDist(msg, channel, to, want_response);
        break;
    }
    case team::proto::TeamMgmtType::Status:
    {
        team::proto::TeamStatus msg;
        if (!team::proto::decodeTeamStatus(mgmt_payload.data(), mgmt_payload.size(), &msg))
        {
            return ErrorCode::InvalidParam;
        }
        ok = prefer_plain
                 ? controller->onStatusPlain(msg, channel, to, want_response)
                 : controller->onStatus(msg, channel, to, want_response);
        break;
    }
    default:
        return ErrorCode::Unsupported;
    }

    return map_team_send_result(ok, controller);
}

bool enqueue_pending_command(const PendingCommand& command)
{
    if (!s_cmd_queue)
    {
        return false;
    }
    PendingCommand copy = command;
    return xQueueSend(s_cmd_queue, &copy, 0) == pdPASS;
}

ErrorCode execute_cmd_tx_msg(const PendingCommand& command)
{
    auto& app = app::AppContext::getInstance();
    chat::ChannelId ch = static_cast<chat::ChannelId>(command.channel);
    std::string text(reinterpret_cast<const char*>(command.payload), command.payload_len);
    chat::MessageId msg_id = app.getChatService().sendText(ch, text, command.to);
    if (msg_id == 0)
    {
        return ErrorCode::Busy;
    }
    return ErrorCode::Ok;
}

ErrorCode execute_cmd_tx_app_data(const PendingCommand& command)
{
    const bool want_response = (command.flags & kCmdTxAppFlagWantResponse) != 0;
    const bool prefer_plain_mgmt = (command.flags & kCmdTxAppFlagTeamMgmtPlain) != 0;
    chat::ChannelId ch = static_cast<chat::ChannelId>(command.channel);
    const uint8_t* payload = command.payload;
    const uint16_t payload_len = command.payload_len;

    auto& app = app::AppContext::getInstance();
    team::TeamController* controller = app.getTeamController();
    switch (command.portnum)
    {
    case team::proto::TEAM_MGMT_APP:
        return send_team_mgmt_wire(controller, payload, payload_len, ch, command.to,
                                   want_response, prefer_plain_mgmt);
    case team::proto::TEAM_POSITION_APP:
    {
        if (!controller)
        {
            return ErrorCode::Internal;
        }
        std::vector<uint8_t> app_payload(payload, payload + payload_len);
        return map_team_send_result(controller->onPosition(app_payload, ch, command.to, want_response),
                                    controller);
    }
    case team::proto::TEAM_WAYPOINT_APP:
    {
        if (!controller)
        {
            return ErrorCode::Internal;
        }
        std::vector<uint8_t> app_payload(payload, payload + payload_len);
        return map_team_send_result(controller->onWaypoint(app_payload, ch, command.to, want_response),
                                    controller);
    }
    case team::proto::TEAM_TRACK_APP:
    {
        if (!controller)
        {
            return ErrorCode::Internal;
        }
        std::vector<uint8_t> app_payload(payload, payload + payload_len);
        return map_team_send_result(controller->onTrack(app_payload, ch, command.to, want_response),
                                    controller);
    }
    case team::proto::TEAM_CHAT_APP:
    {
        if (!controller)
        {
            return ErrorCode::Internal;
        }
        team::proto::TeamChatMessage msg;
        if (!team::proto::decodeTeamChatMessage(payload, payload_len, &msg) ||
            msg.header.version != team::proto::kTeamChatVersion)
        {
            return ErrorCode::InvalidParam;
        }
        return map_team_send_result(controller->onChat(msg, ch, command.to, want_response), controller);
    }
    default:
        break;
    }

    chat::IMeshAdapter* mesh = app.getMeshAdapter();
    if (!mesh)
    {
        return ErrorCode::Internal;
    }
    if (!mesh->sendAppData(ch, command.portnum, payload, payload_len, command.to, want_response))
    {
        return ErrorCode::Busy;
    }
    return ErrorCode::Ok;
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
    if (channel >= static_cast<uint8_t>(chat::ChannelId::MAX_CHANNELS))
    {
        return ErrorCode::InvalidParam;
    }

    PendingCommand command{};
    command.type = PendingCommandType::TxMsg;
    command.to = to;
    command.channel = channel;
    command.flags = flags;
    command.payload_len = text_len;
    memcpy(command.payload, frame.payload.data() + off, text_len);

    if (!enqueue_pending_command(command))
    {
        return ErrorCode::Busy;
    }
    return ErrorCode::Ok;
}

ErrorCode handle_cmd_tx_app_data(const Frame& frame)
{
    PendingCommand command{};
    if (!try_parse_cmd_tx_app_data_legacy(frame, &command) &&
        !try_parse_cmd_tx_app_data_extended(frame, false, &command) &&
        !try_parse_cmd_tx_app_data_extended(frame, true, &command))
    {
        return ErrorCode::InvalidParam;
    }

    if (!enqueue_pending_command(command))
    {
        return ErrorCode::Busy;
    }
    return ErrorCode::Ok;
}

ErrorCode handle_cmd_get_config()
{
    send_status_event(true);
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
    send_status_event(true);
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
        hostlink::bridge::on_link_ready();
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
    case FrameType::CmdTxAppData:
        result = handle_cmd_tx_app_data(frame);
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
            if (s_cmd_queue)
            {
                xQueueReset(s_cmd_queue);
            }
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
            send_status_event(false);
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
    if (!s_cmd_queue)
    {
        s_cmd_queue = xQueueCreate(kCmdQueueSize, sizeof(PendingCommand));
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
    if (s_cmd_queue)
    {
        xQueueReset(s_cmd_queue);
        vQueueDelete(s_cmd_queue);
        s_cmd_queue = nullptr;
    }
    usb_cdc::stop();
    set_state(LinkState::Stopped);
}

void process_pending_commands()
{
    if (!s_cmd_queue)
    {
        return;
    }
    if (s_status.state != LinkState::Ready)
    {
        xQueueReset(s_cmd_queue);
        return;
    }

    PendingCommand command{};
    while (xQueueReceive(s_cmd_queue, &command, 0) == pdPASS)
    {
        ErrorCode result = ErrorCode::Unsupported;
        switch (command.type)
        {
        case PendingCommandType::TxMsg:
            result = execute_cmd_tx_msg(command);
            break;
        case PendingCommandType::TxAppData:
            result = execute_cmd_tx_app_data(command);
            break;
        default:
            result = ErrorCode::Unsupported;
            break;
        }
        if (result != ErrorCode::Ok)
        {
            s_status.last_error = static_cast<uint32_t>(result);
        }
    }
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
