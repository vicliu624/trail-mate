#include "platform/esp/arduino_common/hostlink/hostlink_service.h"

#include "hostlink/hostlink_codec.h"
#include "hostlink/hostlink_frame_router.h"
#include "hostlink/hostlink_service_codec.h"
#include "hostlink/hostlink_types.h"
#include "platform/esp/arduino_common/hostlink/hostlink_bridge_radio.h"
#include "platform/esp/arduino_common/hostlink/hostlink_config_service.h"
#include "usb/usb_cdc_transport.h"

#include "app/app_facade_access.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/usecase/chat_service.h"
#include "platform/esp/arduino_common/gps/gps_service_api.h"
#include "team/protocol/team_chat.h"
#include "team/protocol/team_mgmt.h"
#include "team/protocol/team_portnum.h"
#include "team/protocol/team_waypoint.h"
#include "team/usecase/team_controller.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "sys/clock.h"

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

TaskHandle_t s_task = nullptr;
QueueHandle_t s_tx_queue = nullptr;
QueueHandle_t s_cmd_queue = nullptr;
volatile bool s_stop = false;
SessionRuntime s_session{};

Decoder s_decoder(kMaxFrameLen);

void set_state(LinkState st)
{
    set_link_state(s_session, st);
}

uint16_t next_seq()
{
    return next_tx_sequence(s_session);
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
        note_tx(s_session);
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
    const uint32_t caps =
        CapTxMsg | CapConfig | CapSetTime | CapStatus | CapLogs | CapGps |
        CapAppData | CapTeamState | CapAprsGateway | CapTxAppData;

    HelloAckPayloadInfo info;
    info.protocol_version = kProtocolVersion;
    info.max_frame_len = static_cast<uint16_t>(kMaxFrameLen);
    info.capabilities = caps;
    info.model = "TrailMate";
    info.firmware = "dev";

    std::vector<uint8_t> payload;
    if (!build_hello_ack_payload(payload, info))
    {
        return;
    }

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
                              static_cast<uint8_t>(s_session.status.state),
                              s_session.status.last_error,
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
    GpsPayloadSnapshot snapshot;
    snapshot.valid = gps_state.valid;
    snapshot.has_alt = gps_state.has_alt;
    snapshot.has_speed = gps_state.has_speed;
    snapshot.has_course = gps_state.has_course;
    snapshot.satellites = gps_state.satellites;
    snapshot.age = gps_state.age;
    snapshot.lat = gps_state.lat;
    snapshot.lng = gps_state.lng;
    snapshot.alt_m = gps_state.alt_m;
    snapshot.speed_mps = gps_state.speed_mps;
    snapshot.course_deg = gps_state.course_deg;

    std::vector<uint8_t> payload;
    if (!build_gps_payload(payload, snapshot))
    {
        return false;
    }

    return enqueue_event(static_cast<uint8_t>(FrameType::EvGps),
                         payload.data(), payload.size(), false);
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
    case team::TeamService::SendError::UnsupportedByProtocol:
        return ErrorCode::Unsupported;
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
    app::IAppMessagingFacade& messaging_api = app::messagingFacade();
    chat::ChannelId ch = static_cast<chat::ChannelId>(command.channel);
    std::string text(reinterpret_cast<const char*>(command.payload), command.payload_len);
    chat::MessageId msg_id = messaging_api.getChatService().sendText(ch, text, command.to);
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

    app::IAppMessagingFacade& messaging_api = app::messagingFacade();
    app::IAppTeamFacade& team_api = app::teamFacade();
    team::TeamController* controller = team_api.getTeamController();
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
        team::proto::TeamWaypointMessage msg;
        if (!team::proto::decodeTeamWaypointMessage(payload, payload_len, &msg))
        {
            return ErrorCode::InvalidParam;
        }
        return map_team_send_result(controller->onWaypoint(msg, ch, command.to, want_response),
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

    chat::IMeshAdapter* mesh = messaging_api.getMeshAdapter();
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
    PendingCommand command{};
    if (!parse_tx_msg_command(frame,
                              static_cast<uint8_t>(chat::ChannelId::MAX_CHANNELS),
                              command))
    {
        return ErrorCode::InvalidParam;
    }

    if (!enqueue_pending_command(command))
    {
        return ErrorCode::Busy;
    }
    return ErrorCode::Ok;
}

ErrorCode handle_cmd_tx_app_data(const Frame& frame)
{
    PendingCommand command{};
    if (!parse_tx_app_data_command(frame,
                                   static_cast<uint8_t>(chat::ChannelId::MAX_CHANNELS),
                                   team::proto::kTeamIdSize,
                                   command))
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
        note_error(s_session, err);
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
    if (!parse_u64_le(frame.payload.data(), frame.payload.size(), off, epoch))
    {
        return ErrorCode::InvalidParam;
    }
    if (!set_time_epoch(epoch))
    {
        return ErrorCode::Internal;
    }
    return ErrorCode::Ok;
}

ErrorCode execute_frame_command(HostlinkCommandId command, const Frame& frame)
{
    switch (command)
    {
    case HostlinkCommandId::TxMsg:
        return handle_cmd_tx_msg(frame);
    case HostlinkCommandId::GetConfig:
        return handle_cmd_get_config();
    case HostlinkCommandId::SetConfig:
        return handle_cmd_set_config(frame);
    case HostlinkCommandId::SetTime:
        return handle_cmd_set_time(frame);
    case HostlinkCommandId::GetGps:
        return handle_cmd_get_gps();
    case HostlinkCommandId::TxAppData:
        return handle_cmd_tx_app_data(frame);
    case HostlinkCommandId::None:
    default:
        return ErrorCode::Unsupported;
    }
}

void handle_frame(const Frame& frame)
{
    const HostlinkFrameDecision decision = route_frame(frame, is_ready(s_session));
    switch (decision.type)
    {
    case HostlinkFrameDecisionType::RejectNotReady:
        send_ack(frame.seq, decision.error);
        return;
    case HostlinkFrameDecisionType::CompleteHello:
        send_hello_ack(frame.seq);
        mark_handshake_complete(s_session, sys::millis_now());
        send_status_event(true);
        hostlink::bridge::on_link_ready();
        return;
    case HostlinkFrameDecisionType::DispatchCommand:
        break;
    case HostlinkFrameDecisionType::Unsupported:
    default:
        send_ack(frame.seq, decision.error);
        return;
    }

    ErrorCode result = execute_frame_command(decision.command, frame);
    if (result != ErrorCode::Ok)
    {
        note_error(s_session, static_cast<uint32_t>(result));
    }
    send_ack(frame.seq, result);
}

void hostlink_task(void* /*arg*/)
{
    usb_cdc::start();
    reset_session(s_session, sys::millis_now());
    s_decoder.reset();

    uint8_t rx_buf[128];

    while (!s_stop)
    {
        const bool connected = usb_cdc::is_connected();
        if (!connected)
        {
            mark_disconnected(s_session);
            s_decoder.reset();
            if (s_cmd_queue)
            {
                xQueueReset(s_cmd_queue);
            }
        }
        else
        {
            if (is_waiting(s_session))
            {
                mark_handshake_started(s_session, sys::millis_now(), kHandshakeTimeoutMs);
            }
        }

        size_t n = usb_cdc::read(rx_buf, sizeof(rx_buf));
        if (n > 0)
        {
            note_rx(s_session);
            s_decoder.push(rx_buf, n);
        }

        Frame frame;
        while (s_decoder.next(frame))
        {
            handle_frame(frame);
        }

        if (handshake_expired(s_session, sys::millis_now()))
        {
        }

        const uint32_t now_ms = sys::millis_now();
        if (should_emit_status(s_session, now_ms, kStatusIntervalMs))
        {
            send_status_event(false);
            mark_status_emitted(s_session, now_ms);
        }
        if (should_emit_gps(s_session, now_ms, kGpsIntervalMs))
        {
            send_gps_event();
            mark_gps_emitted(s_session, now_ms);
        }

        TxItem item;
        while (is_ready(s_session) &&
               s_tx_queue &&
               xQueueReceive(s_tx_queue, &item, 0) == pdPASS)
        {
            if (item.data && item.len)
            {
                usb_cdc::write(item.data, item.len);
                note_tx(s_session);
            }
            if (item.data)
            {
                free(item.data);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    usb_cdc::stop();
    stop_session(s_session);
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
    stop_session(s_session);
}

void process_pending_commands()
{
    if (!s_cmd_queue)
    {
        return;
    }
    if (!is_ready(s_session))
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
            note_error(s_session, static_cast<uint32_t>(result));
        }
    }
}

bool is_active()
{
    return s_task != nullptr;
}

Status get_status()
{
    return s_session.status;
}

bool enqueue_event(uint8_t type, const uint8_t* payload, size_t len, bool high_priority)
{
    if (!s_tx_queue || !is_ready(s_session))
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
