#include "uconsole/uconsole_dashboard_model.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "app/linux_app_services.h"
#include "chat/domain/contact_types.h"
#include "chat/linux_raw_lora_mesh_adapter.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "platform/linux/map_tile_cache.h"
#include "platform/linux/runtime_packet_log.h"
#include "platform/linux/runtime_paths.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/team_ui_store_runtime.h"
#include "sys/clock.h"
#include "team/protocol/team_chat.h"
#include "uconsole/uconsole_hardware_probe.h"

namespace trailmate::uconsole
{
namespace
{

[[nodiscard]] const char* protocolLabel(::chat::MeshProtocol protocol) noexcept
{
    switch (protocol)
    {
    case ::chat::MeshProtocol::Meshtastic:
        return "Meshtastic";
    case ::chat::MeshProtocol::MeshCore:
        return "MeshCore";
    case ::chat::MeshProtocol::RNode:
        return "RNode";
    case ::chat::MeshProtocol::LXMF:
        return "LXMF";
    }
    return "Unknown";
}

[[nodiscard]] const char* nodeProtocolLabel(
    ::chat::contacts::NodeProtocolType protocol) noexcept
{
    switch (protocol)
    {
    case ::chat::contacts::NodeProtocolType::Meshtastic:
        return "Meshtastic";
    case ::chat::contacts::NodeProtocolType::MeshCore:
        return "MeshCore";
    case ::chat::contacts::NodeProtocolType::RNode:
        return "RNode";
    case ::chat::contacts::NodeProtocolType::LXMF:
        return "LXMF";
    case ::chat::contacts::NodeProtocolType::Unknown:
        return "Unknown";
    }
    return "Unknown";
}

[[nodiscard]] std::string formatNodeId(std::uint32_t node_id)
{
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "%08lX",
                  static_cast<unsigned long>(node_id));
    return buffer;
}

[[nodiscard]] std::string formatUnread(int unread)
{
    if (unread <= 0) return "read";
    char buffer[24] = {};
    std::snprintf(buffer, sizeof(buffer), "%d unread", unread);
    return buffer;
}

[[nodiscard]] std::string formatLastSeen(std::uint32_t timestamp)
{
    if (timestamp == 0) return "no activity";

    const std::uint32_t now = sys::epoch_seconds_now();
    if (timestamp >= now) return "now";

    const std::uint32_t age = now - timestamp;
    if (age < 60U) return "now";
    if (age < 3600U)
    {
        char buffer[24] = {};
        std::snprintf(buffer, sizeof(buffer), "%lum ago",
                      static_cast<unsigned long>(age / 60U));
        return buffer;
    }
    if (age < 86400U)
    {
        char buffer[24] = {};
        std::snprintf(buffer, sizeof(buffer), "%luh ago",
                      static_cast<unsigned long>(age / 3600U));
        return buffer;
    }

    char buffer[24] = {};
    std::snprintf(buffer, sizeof(buffer), "%lud ago",
                  static_cast<unsigned long>(age / 86400U));
    return buffer;
}

[[nodiscard]] std::uint64_t secondsToMs(std::uint32_t timestamp)
{
    return static_cast<std::uint64_t>(timestamp) * 1000ULL;
}

[[nodiscard]] std::string formatClock(std::uint64_t timestamp_ms)
{
    const std::time_t seconds =
        static_cast<std::time_t>(timestamp_ms / 1000ULL);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    char buffer[16] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%02d:%02d",
                  local.tm_hour,
                  local.tm_min);
    return buffer;
}

[[nodiscard]] bool containsInsensitive(const std::string& text,
                                       const char* needle)
{
    if (needle == nullptr || needle[0] == '\0')
    {
        return false;
    }
    std::string lower_text = text;
    std::string lower_needle = needle;
    std::transform(lower_text.begin(),
                   lower_text.end(),
                   lower_text.begin(),
                   [](unsigned char ch)
                   {
                       return static_cast<char>(std::tolower(ch));
                   });
    std::transform(lower_needle.begin(),
                   lower_needle.end(),
                   lower_needle.begin(),
                   [](unsigned char ch)
                   {
                       return static_cast<char>(std::tolower(ch));
                   });
    return lower_text.find(lower_needle) != std::string::npos;
}

[[nodiscard]] std::string timelineKindFromText(const std::string& text)
{
    if (containsInsensitive(text, "team"))
    {
        return "team";
    }
    if (containsInsensitive(text, "nodeinfo") ||
        containsInsensitive(text, "node info"))
    {
        return "node";
    }
    if (containsInsensitive(text, "position") ||
        containsInsensitive(text, "location"))
    {
        return "position";
    }
    if (containsInsensitive(text, "telemetry") ||
        containsInsensitive(text, "metrics"))
    {
        return "telemetry";
    }
    if (containsInsensitive(text, "message") ||
        containsInsensitive(text, "text") ||
        containsInsensitive(text, "chat"))
    {
        return "message";
    }
    return "system";
}

void pushOverviewTimeline(std::vector<OverviewTimelineItem>& out,
                          OverviewTimelineItem item)
{
    if (item.timestamp_ms == 0)
    {
        item.timestamp_ms = secondsToMs(sys::epoch_seconds_now());
    }
    if (item.time_label.empty())
    {
        item.time_label = formatClock(item.timestamp_ms);
    }
    if (item.kind.empty())
    {
        item.kind = timelineKindFromText(item.title + " " + item.detail);
    }
    out.push_back(std::move(item));
}

[[nodiscard]] ContactPreview makeContactPreview(
    const ::chat::contacts::NodeInfo& node)
{
    ContactPreview preview{};
    preview.name = node.display_name.empty() ? node.short_name : node.display_name;
    if (preview.name.empty()) preview.name = node.long_name;
    if (preview.name.empty()) preview.name = formatNodeId(node.node_id);
    preview.node_id = formatNodeId(node.node_id);
    preview.status = formatLastSeen(node.last_seen);
    preview.protocol = nodeProtocolLabel(node.protocol);
    return preview;
}

[[nodiscard]] std::string conversationLabel(
    const ::chat::ConversationId& id,
    const std::string& stored_name,
    const ::chat::contacts::ContactService& contacts)
{
    if (!stored_name.empty() && stored_name != "Broadcast")
    {
        return stored_name;
    }
    if (id.peer == 0)
    {
        return id.channel == ::chat::ChannelId::SECONDARY
                   ? "Secondary broadcast"
                   : "Primary broadcast";
    }
    if (const auto* node = contacts.getNodeInfo(id.peer))
    {
        if (!node->display_name.empty())
        {
            return node->display_name;
        }
        if (node->short_name[0] != '\0')
        {
            return std::string(node->short_name);
        }
        if (node->long_name[0] != '\0')
        {
            return std::string(node->long_name);
        }
    }
    return formatNodeId(id.peer);
}

[[nodiscard]] RecentContactPreview makeRecentContactPreview(
    const ::chat::ConversationMeta& conversation,
    const ::chat::contacts::ContactService& contacts)
{
    RecentContactPreview preview{};
    preview.conversation = conversation.id;
    preview.name = conversationLabel(conversation.id, conversation.name, contacts);
    preview.direct = conversation.id.peer != 0;
    preview.team = containsInsensitive(conversation.name, "team") ||
                   containsInsensitive(conversation.preview, "team");
    preview.has_unread = conversation.unread > 0;
    preview.badge = preview.team ? "Team" : (preview.direct ? "Direct" : "Broadcast");
    preview.meta = formatUnread(conversation.unread) + " / " +
                   formatLastSeen(conversation.last_timestamp);
    if (conversation.id.peer != 0)
    {
        if (const auto* node = contacts.getNodeInfo(conversation.id.peer))
        {
            preview.detail =
                std::string("hops ") +
                (node->hops_away == 0xFF
                     ? "?"
                     : std::to_string(node->hops_away)) +
                " / " + (node->via_mqtt ? "MQTT" : "LoRa");
        }
    }
    if (preview.detail.empty())
    {
        preview.detail = conversation.preview.empty()
                             ? "No messages in this thread yet."
                             : conversation.preview;
    }
    return preview;
}

[[nodiscard]] bool envConfigured(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0';
}

[[nodiscard]] bool gpsSourceConfigured()
{
    std::string auto_path{};
    return envConfigured("TRAIL_MATE_GPS_DEVICE") ||
           envConfigured("TRAIL_MATE_GPS_NMEA_FILE") ||
           envConfigured("TRAIL_MATE_GPS_VALID") ||
           envConfigured("TRAIL_MATE_GPS_LAT") ||
           envConfigured("TRAIL_MATE_GPS_LNG") ||
           uconsoleAutoGpsSerialPath(auto_path);
}

[[nodiscard]] bool parseEnvDouble(const char* name, double& out)
{
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0')
    {
        return false;
    }

    char* end = nullptr;
    const double parsed = std::strtod(value, &end);
    if (end == value || (end != nullptr && *end != '\0') ||
        !std::isfinite(parsed))
    {
        return false;
    }

    out = parsed;
    return true;
}

[[nodiscard]] bool configuredMapCenter(double& lat, double& lon)
{
    return parseEnvDouble("TRAIL_MATE_MAP_LAT", lat) &&
           parseEnvDouble("TRAIL_MATE_MAP_LNG", lon);
}

[[nodiscard]] std::string compactStoragePath()
{
    return ::platform::linux_runtime::sqlite_database_path().string();
}

[[nodiscard]] std::string formatCoordinate(double lat, double lon)
{
    char buffer[64] = {};
    std::snprintf(buffer, sizeof(buffer), "%.5f, %.5f", lat, lon);
    return buffer;
}

[[nodiscard]] std::string formatSpeed(double speed_mps)
{
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%.1f m/s", speed_mps);
    return buffer;
}

[[nodiscard]] std::string teamIdLabel(const ::team::TeamId& id)
{
    char buffer[17] = {};
    for (std::size_t index = 0; index < 8U && index < id.size(); ++index)
    {
        std::snprintf(buffer + (index * 2U), 3, "%02X",
                      static_cast<unsigned>(id[index]));
    }
    return buffer;
}

[[nodiscard]] std::string cleanPayloadText(const std::vector<std::uint8_t>& payload)
{
    std::string text{};
    text.reserve(std::min<std::size_t>(payload.size(), 80U));
    for (const std::uint8_t byte : payload)
    {
        if (text.size() >= 80U)
        {
            text += "...";
            break;
        }
        const unsigned char ch = static_cast<unsigned char>(byte);
        text += std::isprint(ch) ? static_cast<char>(ch) : '.';
    }
    return text;
}

struct TimelineCandidate
{
    std::uint32_t timestamp = 0;
    TeamTimelineItem item{};
};

void pushTimeline(std::vector<TimelineCandidate>& out,
                  std::uint32_t timestamp,
                  std::string title,
                  std::string detail,
                  bool attention = false)
{
    if (timestamp == 0 && title.empty() && detail.empty())
    {
        return;
    }

    out.push_back(TimelineCandidate{.timestamp = timestamp,
                                    .item = {.title = std::move(title),
                                             .detail = std::move(detail),
                                             .attention = attention}});
}

void appendPacketLogTimeline(std::vector<OverviewTimelineItem>& out)
{
    const ::platform::linux_runtime::PacketLogSource sources[] = {
        ::platform::linux_runtime::PacketLogSource::Lora,
        ::platform::linux_runtime::PacketLogSource::Mqtt,
    };
    for (const auto source : sources)
    {
        const auto logs =
            ::platform::linux_runtime::recent_packet_logs(source, 120U);
        for (const auto& log : logs)
        {
            OverviewTimelineItem item{};
            item.timestamp_ms = log.timestamp_ms;
            item.title = log.title.empty()
                             ? std::string(::platform::linux_runtime::
                                               packet_log_source_label(source)) +
                                   " activity"
                             : log.title;
            item.detail = log.summary;
            item.kind = timelineKindFromText(item.title + " " + item.detail);
            item.outgoing = log.direction ==
                            ::platform::linux_runtime::PacketLogDirection::Tx;
            item.direct = containsInsensitive(log.summary, " direct ") ||
                          containsInsensitive(log.summary, " to ");
            item.team = containsInsensitive(item.title, "team") ||
                        containsInsensitive(item.detail, "team");
            item.badge =
                item.team ? "Team"
                          : std::string(::platform::linux_runtime::
                                            packet_log_source_label(source)) +
                                " " +
                                ::platform::linux_runtime::
                                    packet_log_direction_label(log.direction);
            item.attention = containsInsensitive(item.title, "fail") ||
                             containsInsensitive(item.detail, "fail") ||
                             containsInsensitive(item.detail, "error");
            pushOverviewTimeline(out, std::move(item));
        }
    }
}

void appendChatTimeline(std::vector<OverviewTimelineItem>& out,
                        ::chat::ChatService& chat_service,
                        const ::chat::contacts::ContactService& contacts)
{
    std::size_t total = 0;
    const auto conversations = chat_service.getConversations(0, 48U, &total);
    for (const auto& conversation : conversations)
    {
        const auto messages =
            chat_service.getRecentMessages(conversation.id, 12U);
        for (const auto& message : messages)
        {
            OverviewTimelineItem item{};
            item.timestamp_ms = secondsToMs(message.timestamp);
            item.kind = "message";
            item.outgoing = message.status != ::chat::MessageStatus::Incoming;
            item.direct = conversation.id.peer != 0;
            item.team = containsInsensitive(conversation.name, "team") ||
                        message.team_location_icon != 0;
            item.badge = item.team ? "Team" : (item.direct ? "Direct" : "Broadcast");
            item.title = item.outgoing ? "Sent message" : "Received message";
            item.detail =
                conversationLabel(conversation.id, conversation.name, contacts);
            if (!message.text.empty())
            {
                item.detail += " / ";
                item.detail += message.text.size() > 72U
                                   ? message.text.substr(0, 72U) + "..."
                                   : message.text;
            }
            item.attention = message.status == ::chat::MessageStatus::Failed;
            pushOverviewTimeline(out, std::move(item));
        }
    }
}

void appendNodeTimeline(std::vector<OverviewTimelineItem>& out,
                        const std::vector<::chat::contacts::NodeInfo>& nodes)
{
    for (const auto& node : nodes)
    {
        const std::string name =
            node.display_name.empty() ? formatNodeId(node.node_id)
                                      : node.display_name;
        if (node.last_seen != 0)
        {
            OverviewTimelineItem item{};
            item.timestamp_ms = secondsToMs(node.last_seen);
            item.kind = "node";
            item.title = "NodeInfo received";
            item.detail = name + " / " + formatNodeId(node.node_id);
            item.badge = node.via_mqtt ? "MQTT" : "LoRa";
            pushOverviewTimeline(out, std::move(item));
        }
        if (node.position.valid && node.position.timestamp != 0)
        {
            OverviewTimelineItem item{};
            item.timestamp_ms = secondsToMs(node.position.timestamp);
            item.kind = "position";
            item.title = "Position received";
            item.detail =
                name + " / " +
                formatCoordinate(static_cast<double>(node.position.latitude_i) /
                                     10000000.0,
                                 static_cast<double>(node.position.longitude_i) /
                                     10000000.0);
            item.badge = node.via_mqtt ? "MQTT" : "LoRa";
            pushOverviewTimeline(out, std::move(item));
        }
        if (node.has_device_metrics && node.last_seen != 0)
        {
            OverviewTimelineItem item{};
            item.timestamp_ms = secondsToMs(node.last_seen);
            item.kind = "telemetry";
            item.title = "Telemetry received";
            item.detail = name;
            if (node.device_metrics.has_battery_level)
            {
                item.detail += " / battery " +
                               std::to_string(node.device_metrics.battery_level) +
                               "%";
            }
            if (node.device_metrics.has_voltage)
            {
                char voltage[24] = {};
                std::snprintf(voltage,
                              sizeof(voltage),
                              " / %.2f V",
                              static_cast<double>(node.device_metrics.voltage));
                item.detail += voltage;
            }
            item.badge = node.via_mqtt ? "MQTT" : "LoRa";
            pushOverviewTimeline(out, std::move(item));
        }
    }
}

[[nodiscard]] std::string memberDisplayName(
    const ::team::ui::TeamMemberUi& member)
{
    if (!member.name.empty())
    {
        return member.name;
    }
    return formatNodeId(member.node_id);
}

void buildTeamOverview(UConsoleDashboardSnapshot& out)
{
    ::team::ui::TeamUiSnapshot team_snapshot{};
    if (!::team::ui::team_ui_get_store().load(team_snapshot))
    {
        out.team_summary = "No team state stored locally.";
        return;
    }

    if (team_snapshot.in_team)
    {
        out.team_summary =
            "Team: " +
            (team_snapshot.team_name.empty() ? std::string("unnamed")
                                             : team_snapshot.team_name) +
            (team_snapshot.self_is_leader ? " / leader" : " / member") +
            " / members " + std::to_string(team_snapshot.members.size()) +
            " / unread " + std::to_string(team_snapshot.team_chat_unread);
    }
    else if (team_snapshot.pending_join)
    {
        out.team_summary = "Team join is pending.";
    }
    else if (team_snapshot.kicked_out)
    {
        out.team_summary = "Last team state: kicked out.";
    }
    else
    {
        out.team_summary = "Not in a team.";
    }

    std::vector<TimelineCandidate> timeline{};

    if (team_snapshot.pending_join)
    {
        pushTimeline(timeline,
                     team_snapshot.pending_join_started_s,
                     "Join pending",
                     formatLastSeen(team_snapshot.pending_join_started_s),
                     true);
    }
    if (team_snapshot.kicked_out)
    {
        pushTimeline(timeline,
                     team_snapshot.last_update_s,
                     "Kicked out",
                     formatLastSeen(team_snapshot.last_update_s),
                     true);
    }
    if (team_snapshot.last_update_s != 0)
    {
        std::string detail = formatLastSeen(team_snapshot.last_update_s);
        if (team_snapshot.has_team_id)
        {
            detail += " / team " + teamIdLabel(team_snapshot.team_id);
        }
        pushTimeline(timeline,
                     team_snapshot.last_update_s,
                     "Team state updated",
                     detail);
    }

    for (const auto& member : team_snapshot.members)
    {
        if (member.last_seen_s == 0)
        {
            continue;
        }
        std::string detail = formatLastSeen(member.last_seen_s);
        detail += member.online ? " / online" : " / offline";
        if (member.leader)
        {
            detail += " / leader";
        }
        pushTimeline(timeline,
                     member.last_seen_s,
                     memberDisplayName(member) + " seen",
                     detail);
    }

    if (team_snapshot.has_team_id)
    {
        std::vector<::team::ui::TeamChatLogEntry> chat_log{};
        if (::team::ui::team_ui_chatlog_load_recent(
                team_snapshot.team_id, 4U, chat_log))
        {
            for (const auto& entry : chat_log)
            {
                std::string detail = formatLastSeen(entry.ts) + " / " +
                                     formatNodeId(entry.peer_id);
                if (entry.type == ::team::proto::TeamChatType::Text)
                {
                    const std::string text = cleanPayloadText(entry.payload);
                    if (!text.empty())
                    {
                        detail += " / " + text;
                    }
                }
                else
                {
                    detail += " / structured team message";
                }
                pushTimeline(timeline,
                             entry.ts,
                             entry.incoming ? "Team chat received"
                                            : "Team chat sent",
                             detail);
            }
        }

        std::vector<::team::ui::TeamPosSample> positions{};
        if (::team::ui::team_ui_posring_load_latest(team_snapshot.team_id,
                                                    positions))
        {
            std::sort(positions.begin(),
                      positions.end(),
                      [](const auto& left, const auto& right)
                      {
                          return left.ts > right.ts;
                      });
            const std::size_t count =
                std::min<std::size_t>(positions.size(), 4U);
            for (std::size_t index = 0; index < count; ++index)
            {
                const auto& position = positions[index];
                const double lat =
                    static_cast<double>(position.lat_e7) / 10000000.0;
                const double lon =
                    static_cast<double>(position.lon_e7) / 10000000.0;
                std::string detail = formatLastSeen(position.ts) + " / " +
                                     formatCoordinate(lat, lon) + " / alt " +
                                     std::to_string(position.alt_m) + " m";
                pushTimeline(timeline,
                             position.ts,
                             formatNodeId(position.member_id) +
                                 " position update",
                             detail);
            }
        }
    }

    std::sort(timeline.begin(),
              timeline.end(),
              [](const auto& left, const auto& right)
              {
                  return left.timestamp > right.timestamp;
              });

    const std::size_t count = std::min<std::size_t>(timeline.size(), 7U);
    out.team_timeline.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        OverviewTimelineItem item{};
        item.timestamp_ms = secondsToMs(timeline[index].timestamp);
        item.title = timeline[index].item.title;
        item.detail = timeline[index].item.detail;
        item.kind = "team";
        item.team = true;
        item.badge = "Team";
        item.attention = timeline[index].item.attention;
        pushOverviewTimeline(out.timeline, std::move(item));
        out.team_timeline.push_back(std::move(timeline[index].item));
    }
}

} // namespace

UConsoleDashboardModel::UConsoleDashboardModel(
    linux_app::LinuxAppServices& services)
    : services_(services)
{
}

UConsoleDashboardSnapshot UConsoleDashboardModel::snapshot() const
{
    UConsoleDashboardSnapshot out{};

    auto& chat_service = services_.chat();
    auto& contact_service = services_.contacts();
    const auto* mesh_adapter = services_.meshAdapter();
    const auto* raw_lora_adapter =
        dynamic_cast<const linux_app::LinuxRawLoraMeshAdapter*>(mesh_adapter);
    const UConsoleHardwareProbe hardware_probe = probeUConsoleHardware();

    std::size_t conversation_total = 0;
    const auto conversations =
        chat_service.getConversations(0, 16, &conversation_total);
    out.conversation_count = conversation_total;
    out.unread_count = chat_service.getTotalUnread();

    out.mesh_protocol = protocolLabel(services_.meshProtocol());
    out.self_node = formatNodeId(services_.selfNodeId());

    out.conversations.reserve(std::min<std::size_t>(conversations.size(), 6U));
    for (std::size_t index = 0;
         index < conversations.size() && index < 6U;
         ++index)
    {
        const auto& item = conversations[index];
        ConversationPreview preview{};
        preview.title = item.name.empty() ? "Primary channel" : item.name;
        preview.preview =
            item.preview.empty() ? "No messages in this thread yet."
                                 : item.preview;
        preview.meta = formatUnread(item.unread) + " / " +
                       formatLastSeen(item.last_timestamp);
        preview.unread = item.unread;
        out.conversations.push_back(std::move(preview));
    }
    out.recent_contacts.reserve(5U);
    for (std::size_t index = 0;
         index < conversations.size() && out.recent_contacts.size() < 5U;
         ++index)
    {
        out.recent_contacts.push_back(
            makeRecentContactPreview(conversations[index], contact_service));
    }

    auto contacts = contact_service.getContacts();
    auto nearby = contact_service.getNearby();
    auto ignored = contact_service.getIgnoredNodes();
    out.contact_count = contacts.size();
    out.nearby_count = nearby.size();
    out.ignored_count = ignored.size();

    std::vector<::chat::contacts::NodeInfo> visible_nodes{};
    visible_nodes.reserve(contacts.size() + nearby.size());
    visible_nodes.insert(visible_nodes.end(), contacts.begin(), contacts.end());
    visible_nodes.insert(visible_nodes.end(), nearby.begin(), nearby.end());
    std::sort(visible_nodes.begin(), visible_nodes.end(),
              [](const auto& left, const auto& right)
              {
                  return left.last_seen > right.last_seen;
              });

    const std::size_t contact_limit =
        std::min<std::size_t>(visible_nodes.size(), 7U);
    out.contacts.reserve(contact_limit);
    for (std::size_t index = 0; index < contact_limit; ++index)
    {
        out.contacts.push_back(makeContactPreview(visible_nodes[index]));
    }

    appendPacketLogTimeline(out.timeline);
    appendChatTimeline(out.timeline, chat_service, contact_service);
    appendNodeTimeline(out.timeline, visible_nodes);

    std::string mesh_line = "Mesh: ";
    mesh_line += out.mesh_protocol;
    HardwareStatusItem lora_status{};
    lora_status.name = "LoRa";
    bool mesh_transport_ready = false;
    if (mesh_adapter == nullptr)
    {
        mesh_line += " transport unavailable";
        if (hardware_probe.lora_spi_detected)
        {
            lora_status.state = "Endpoint";
            lora_status.detail = "SPI radio endpoint present at " +
                                 hardware_probe.lora_spi_path +
                                 "; Trail Mate LoRa transport driver is not bound yet.";
        }
        else
        {
            lora_status.state = "Unavailable";
            lora_status.detail = "No LoRa SPI endpoint or mesh transport adapter is attached.";
        }
        lora_status.attention = true;
    }
    else
    {
        mesh_transport_ready = mesh_adapter->isReady();
        mesh_line += mesh_transport_ready ? " transport ready"
                                          : " transport not connected";
        if (raw_lora_adapter != nullptr)
        {
            lora_status.state = mesh_transport_ready ? "Ready" : "Endpoint";
            lora_status.detail = raw_lora_adapter->statusText() + " / " +
                                 raw_lora_adapter->radioConfigText() + " / " +
                                 raw_lora_adapter->radioStatsText();
        }
        else if (mesh_transport_ready)
        {
            lora_status.state = "Ready";
            lora_status.detail = "Mesh text transport is available.";
        }
        else if (hardware_probe.lora_spi_detected)
        {
            lora_status.state = "Endpoint";
            lora_status.detail = "SPI radio endpoint present at " +
                                 hardware_probe.lora_spi_path +
                                 "; protocol transport is not bound yet.";
        }
        else
        {
            lora_status.state = "Offline";
            lora_status.detail = "AIO2/LoRa transport is not connected.";
        }
        lora_status.attention = !mesh_transport_ready;
    }

    const ::platform::linux_runtime::MapTileCache tile_cache;
    const auto tile_stats = tile_cache.stats();

    HardwareStatusItem aio2_status{};
    aio2_status.name = "AIO2";
    if (hardware_probe.aio2_detected)
    {
        aio2_status.state = "Detected";
        aio2_status.detail = hardware_probe.summary;
        aio2_status.attention = false;
    }
    else
    {
        aio2_status.state = "Not detected";
        aio2_status.detail = "No uConsole/AIO2 Linux endpoint was found.";
        aio2_status.attention = true;
    }

    HardwareStatusItem gps_status{};
    gps_status.name = "GPS";
    if (!::platform::ui::gps::is_enabled())
    {
        gps_status.state = "Disabled";
        gps_status.detail = "GPS runtime is disabled.";
        gps_status.attention = true;
    }
    else if (!::platform::ui::gps::is_powered())
    {
        gps_status.state = "Power off";
        gps_status.detail = "GPS runtime reports receiver power off.";
        gps_status.attention = true;
    }
    else if (!gpsSourceConfigured())
    {
        gps_status.state = "No source";
        gps_status.detail = "No GPS serial endpoint or NMEA file configured.";
        gps_status.attention = true;
    }
    else
    {
        const auto gps = ::platform::ui::gps::get_data();
        if (gps.valid)
        {
            gps_status.state = "Fix";
            gps_status.detail = "GPS/NMEA source has a valid fix.";
        }
        else if (hardware_probe.gps_serial_detected)
        {
            gps_status.state = "Endpoint";
            gps_status.detail = "Serial endpoint present at " +
                                hardware_probe.gps_serial_path +
                                "; waiting for valid NMEA fix.";
        }
        else
        {
            gps_status.state = "No fix";
            gps_status.detail =
                "GPS/NMEA source is present but no valid fix yet.";
        }
        gps_status.attention = !gps.valid;
    }

    HardwareStatusItem storage_status{};
    storage_status.name = "Storage";
    storage_status.state = "SQLite";
    storage_status.detail = compactStoragePath();
    storage_status.attention = false;

    HardwareStatusItem map_status{};
    map_status.name = "Map";
    map_status.state = std::to_string(tile_stats.cached_tiles) + " tiles";
    map_status.detail = tile_stats.root.string();
    map_status.attention = false;

    double configured_lat = 0.0;
    double configured_lon = 0.0;
    const auto map_source = ::platform::linux_runtime::sanitize_map_base_source(
        services_.config().map_source);
    out.location.map_meta =
        std::string(::platform::linux_runtime::map_base_source_label(
            map_source)) +
        " / " + std::to_string(tile_stats.cached_tiles) + " cached tiles";
    if (configuredMapCenter(configured_lat, configured_lon))
    {
        out.location.state = "Map center";
        out.location.coordinates =
            formatCoordinate(configured_lat, configured_lon);
        out.location.detail =
            "Using explicit map center from TRAIL_MATE_MAP_LAT/LNG.";
        out.location.attention = false;
    }
    else if (!::platform::ui::gps::is_enabled())
    {
        out.location.state = "GPS disabled";
        out.location.coordinates = "No coordinates";
        out.location.detail = "GPS runtime is disabled.";
        out.location.attention = true;
    }
    else if (!::platform::ui::gps::is_powered())
    {
        out.location.state = "GPS power off";
        out.location.coordinates = "No coordinates";
        out.location.detail = "GPS receiver is not powered.";
        out.location.attention = true;
    }
    else if (!gpsSourceConfigured())
    {
        out.location.state = "No location source";
        out.location.coordinates = "No coordinates";
        out.location.detail = "No GPS serial endpoint or NMEA file configured.";
        out.location.attention = true;
    }
    else
    {
        const auto gps = ::platform::ui::gps::get_data();
        out.location.state = gps.valid ? "GPS fix" : "Waiting for fix";
        out.location.coordinates =
            gps.valid ? formatCoordinate(gps.lat, gps.lng) : "No coordinates";
        out.location.detail =
            gps.valid ? "Configured GPS/NMEA source is reporting position."
                      : "GPS/NMEA source is present but has no valid fix.";
        if (!gps.valid && hardware_probe.gps_serial_detected)
        {
            out.location.detail = "AIO2 serial endpoint present at " +
                                  hardware_probe.gps_serial_path +
                                  "; waiting for NMEA fix.";
        }
        if (gps.valid && gps.has_speed)
        {
            out.location.detail += " Speed " + formatSpeed(gps.speed_mps) + ".";
        }
        out.location.attention = !gps.valid;
    }

    out.messages.title = conversation_total == 0U
                             ? "No stored messages"
                             : std::to_string(conversation_total) + " threads";
    out.messages.detail =
        std::to_string(out.unread_count) + " unread / " +
        (mesh_transport_ready ? "mesh transport ready"
                              : "LoRa transport offline");
    out.messages.latest =
        out.conversations.empty() ? "No messages are stored locally."
                                  : out.conversations.front().title + " - " +
                                        out.conversations.front().preview;
    out.messages.attention = out.unread_count > 0 || !mesh_transport_ready;

    buildTeamOverview(out);
    std::sort(out.timeline.begin(),
              out.timeline.end(),
              [](const auto& left, const auto& right)
              {
                  return left.timestamp_ms > right.timestamp_ms;
              });
    if (out.timeline.size() > 200U)
    {
        out.timeline.resize(200U);
    }

    out.hardware = {aio2_status, lora_status, gps_status, storage_status,
                    map_status};
    out.bottom_status =
        "AIO2: " + aio2_status.state + " | LoRa: " + lora_status.state +
        " | GPS: " + gps_status.state + " | Node: " + out.self_node +
        " | Unread: " + std::to_string(out.unread_count);

    out.capability_lines = {
        "Mode: Linux desktop-class handheld",
        "AIO2: " + aio2_status.state + " - " + aio2_status.detail,
        mesh_line,
        "GPS: " + gps_status.state + " - " + gps_status.detail,
        "BLE: not used on Linux",
        "Storage: SQLite " +
            ::platform::linux_runtime::sqlite_database_path().string(),
        "Map cache: " + std::to_string(tile_stats.cached_tiles) +
            " tiles at " + tile_stats.root.string(),
    };
    if (raw_lora_adapter != nullptr)
    {
        for (const auto& line : raw_lora_adapter->diagnosticLines())
        {
            out.capability_lines.push_back("LoRa: " + line);
        }
    }

    return out;
}

} // namespace trailmate::uconsole
