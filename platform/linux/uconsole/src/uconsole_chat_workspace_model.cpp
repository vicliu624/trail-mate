#include "uconsole/uconsole_chat_workspace_model.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "app/linux_app_services.h"
#include "chat/domain/contact_types.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "meshtastic/mesh.pb.h"
#include "pb_encode.h"
#include "platform/ui/gps_runtime.h"
#include "sys/clock.h"

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

[[nodiscard]] const char* statusLabel(::chat::MessageStatus status) noexcept
{
    switch (status)
    {
    case ::chat::MessageStatus::Incoming:
        return "received";
    case ::chat::MessageStatus::Queued:
        return "queued";
    case ::chat::MessageStatus::Sent:
        return "sent";
    case ::chat::MessageStatus::Failed:
        return "failed";
    }
    return "unknown";
}

[[nodiscard]] std::string formatNodeId(std::uint32_t node_id)
{
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "%08lX",
                  static_cast<unsigned long>(node_id));
    return buffer;
}

[[nodiscard]] std::string formatNodeLabel(std::uint32_t node_id)
{
    return "0x" + formatNodeId(node_id);
}

[[nodiscard]] std::string contactDisplayName(
    const ::chat::contacts::ContactService& contacts,
    ::chat::NodeId node_id)
{
    if (node_id == 0)
    {
        return {};
    }
    if (const auto* node = contacts.getNodeInfo(node_id))
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
    const std::string stored = contacts.getContactName(node_id);
    if (!stored.empty())
    {
        return stored;
    }
    return {};
}

[[nodiscard]] std::string displayNameOrId(
    const ::chat::contacts::ContactService& contacts,
    ::chat::NodeId node_id)
{
    const std::string name = contactDisplayName(contacts, node_id);
    return name.empty() ? formatNodeLabel(node_id) : name;
}

[[nodiscard]] std::string formatChannel(::chat::ChannelId channel)
{
    switch (channel)
    {
    case ::chat::ChannelId::PRIMARY:
        return "Primary";
    case ::chat::ChannelId::SECONDARY:
        return "Secondary";
    case ::chat::ChannelId::MAX_CHANNELS:
        return "Channel";
    }
    return "Channel";
}

[[nodiscard]] std::string formatAge(std::uint32_t timestamp)
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

[[nodiscard]] double degToRad(double degrees)
{
    return degrees * 3.14159265358979323846 / 180.0;
}

[[nodiscard]] double distanceMeters(double lat_a,
                                    double lon_a,
                                    double lat_b,
                                    double lon_b)
{
    constexpr double kEarthRadiusM = 6371000.0;
    const double d_lat = degToRad(lat_b - lat_a);
    const double d_lon = degToRad(lon_b - lon_a);
    const double a =
        std::sin(d_lat / 2.0) * std::sin(d_lat / 2.0) +
        std::cos(degToRad(lat_a)) * std::cos(degToRad(lat_b)) *
            std::sin(d_lon / 2.0) * std::sin(d_lon / 2.0);
    const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return kEarthRadiusM * c;
}

[[nodiscard]] double bearingDegrees(double from_lat,
                                    double from_lon,
                                    double to_lat,
                                    double to_lon)
{
    const double lat1 = degToRad(from_lat);
    const double lat2 = degToRad(to_lat);
    const double d_lon = degToRad(to_lon - from_lon);
    const double y = std::sin(d_lon) * std::cos(lat2);
    const double x = std::cos(lat1) * std::sin(lat2) -
                     std::sin(lat1) * std::cos(lat2) * std::cos(d_lon);
    double bearing = std::atan2(y, x) * 180.0 / 3.14159265358979323846;
    if (bearing < 0.0)
    {
        bearing += 360.0;
    }
    return bearing;
}

[[nodiscard]] std::string formatDistance(double meters)
{
    if (!std::isfinite(meters) || meters < 0.0)
    {
        return "distance ?";
    }
    if (meters < 1000.0)
    {
        char buffer[24] = {};
        std::snprintf(buffer, sizeof(buffer), "%.0f m", meters);
        return buffer;
    }
    char buffer[24] = {};
    std::snprintf(buffer, sizeof(buffer), "%.1f km", meters / 1000.0);
    return buffer;
}

[[nodiscard]] const char* contactProtocolLabel(
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
    default:
        return "Unknown";
    }
}

[[nodiscard]] ::chat::MeshProtocol meshProtocolForNode(
    ::chat::contacts::NodeProtocolType protocol,
    ::chat::MeshProtocol fallback) noexcept
{
    switch (protocol)
    {
    case ::chat::contacts::NodeProtocolType::Meshtastic:
        return ::chat::MeshProtocol::Meshtastic;
    case ::chat::contacts::NodeProtocolType::MeshCore:
        return ::chat::MeshProtocol::MeshCore;
    case ::chat::contacts::NodeProtocolType::RNode:
        return ::chat::MeshProtocol::RNode;
    case ::chat::contacts::NodeProtocolType::LXMF:
        return ::chat::MeshProtocol::LXMF;
    case ::chat::contacts::NodeProtocolType::Unknown:
    default:
        return fallback;
    }
}

[[nodiscard]] ::chat::ChannelId channelForNode(
    const ::chat::contacts::NodeInfo& node) noexcept
{
    return node.channel == 1U ? ::chat::ChannelId::SECONDARY
                              : ::chat::ChannelId::PRIMARY;
}

[[nodiscard]] std::string formatCoordinate(std::int32_t value_e7)
{
    char buffer[24] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%.5f",
                  static_cast<double>(value_e7) / 10000000.0);
    return buffer;
}

[[nodiscard]] const char* roleLabel(::chat::contacts::NodeRoleType role) noexcept
{
    using Role = ::chat::contacts::NodeRoleType;
    switch (role)
    {
    case Role::Client:
        return "Client";
    case Role::ClientMute:
        return "Client mute";
    case Role::Router:
        return "Router";
    case Role::RouterClient:
        return "Router client";
    case Role::Repeater:
        return "Repeater";
    case Role::Tracker:
        return "Tracker";
    case Role::Sensor:
        return "Sensor";
    case Role::Tak:
        return "TAK";
    case Role::ClientHidden:
        return "Client hidden";
    case Role::LostAndFound:
        return "Lost and found";
    case Role::TakTracker:
        return "TAK tracker";
    case Role::RouterLate:
        return "Router late";
    case Role::ClientBase:
        return "Client base";
    case Role::Unknown:
    default:
        return "Unknown";
    }
}

[[nodiscard]] std::string formatNodeInfoId(std::uint32_t node_id)
{
    char buffer[16] = {};
    if (node_id <= 0xFFFFFFUL)
    {
        std::snprintf(buffer,
                      sizeof(buffer),
                      "!%06lX",
                      static_cast<unsigned long>(node_id));
    }
    else
    {
        std::snprintf(buffer,
                      sizeof(buffer),
                      "!%08lX",
                      static_cast<unsigned long>(node_id));
    }
    return buffer;
}

[[nodiscard]] std::string formatMacAddress(const std::uint8_t mac[6])
{
    char buffer[24] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0],
                  mac[1],
                  mac[2],
                  mac[3],
                  mac[4],
                  mac[5]);
    return buffer;
}

[[nodiscard]] std::string formatFloatValue(double value,
                                           const char* suffix,
                                           int precision)
{
    if (!std::isfinite(value))
    {
        return "?";
    }
    char format[16] = {};
    std::snprintf(format, sizeof(format), "%%.%df%%s", precision);
    char buffer[40] = {};
    std::snprintf(buffer, sizeof(buffer), format, value, suffix ? suffix : "");
    return buffer;
}

[[nodiscard]] std::string formatDop(std::uint32_t value)
{
    if (value == 0)
    {
        return "?";
    }
    char buffer[24] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%.2f",
                  static_cast<double>(value) / 100.0);
    return buffer;
}

void appendDetailRow(ChatNodeDetailSection& section,
                     std::string label,
                     std::string value,
                     bool attention = false)
{
    if (value.empty())
    {
        return;
    }
    section.rows.push_back(
        ChatNodeDetailRow{std::move(label), std::move(value), attention});
}

void appendDetailSection(ChatNodeDetailSnapshot& out,
                         ChatNodeDetailSection section)
{
    if (!section.rows.empty())
    {
        out.sections.push_back(std::move(section));
    }
}

[[nodiscard]] ChatNodeInfoItem makeNodeInfoItem(
    const ::chat::contacts::NodeInfo& node)
{
    ChatNodeInfoItem item{};
    item.node_id = node.node_id;
    item.title = node.display_name.empty() ? formatNodeLabel(node.node_id)
                                           : node.display_name;
    item.subtitle = node.long_name[0] != '\0' ? std::string(node.long_name)
                                              : formatNodeLabel(node.node_id);
    item.via_mqtt = node.via_mqtt;
    item.has_position = node.position.valid;
    item.is_contact = node.is_contact;
    item.is_ignored = node.is_ignored;
    item.has_public_key = node.has_public_key;
    item.key_verified = node.key_manually_verified;

    item.status = std::string(contactProtocolLabel(node.protocol)) + " / " +
                  (node.via_mqtt ? "MQTT" : "LoRa") + " / " +
                  formatAge(node.last_seen);
    if (node.is_ignored)
    {
        item.status += " / ignored";
    }
    if (node.has_public_key)
    {
        item.status += node.key_manually_verified ? " / key trusted"
                                                  : " / key unverified";
    }

    const std::string hops = node.hops_away == 0xFF
                                 ? "?"
                                 : std::to_string(node.hops_away);
    const std::string channel =
        node.channel == 0xFF ? "?" : std::to_string(node.channel);
    char signal[112] = {};
    std::snprintf(signal,
                  sizeof(signal),
                  "RSSI %.1f dBm / SNR %.1f dB / hops %s / ch %s",
                  static_cast<double>(node.rssi),
                  static_cast<double>(node.snr),
                  hops.c_str(),
                  channel.c_str());
    item.signal = signal;

    if (node.position.valid)
    {
        item.position = formatCoordinate(node.position.latitude_i) + ", " +
                        formatCoordinate(node.position.longitude_i);
        if (node.position.has_altitude)
        {
            item.position += " / ";
            item.position += std::to_string(node.position.altitude);
            item.position += " m";
        }
    }
    else
    {
        item.position = "No position packet stored.";
    }
    return item;
}

[[nodiscard]] std::string trimCopy(const std::string& text)
{
    const auto is_space = [](unsigned char ch)
    {
        return std::isspace(ch) != 0;
    };

    auto begin = std::find_if_not(text.begin(), text.end(), is_space);
    auto end = std::find_if_not(text.rbegin(), text.rend(), is_space).base();
    if (begin >= end) return {};
    return std::string(begin, end);
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

[[nodiscard]] bool sameConversation(const ::chat::ConversationId& left,
                                    const ::chat::ConversationId& right) noexcept
{
    return left == right;
}

[[nodiscard]] std::string titleForConversation(
    const ::chat::ConversationId& id,
    const std::string& stored_name,
    const ::chat::contacts::ContactService& contacts)
{
    if (!stored_name.empty() && stored_name != "Broadcast") return stored_name;
    if (id.peer == 0)
    {
        return formatChannel(id.channel) + " broadcast";
    }
    return displayNameOrId(contacts, id.peer);
}

[[nodiscard]] std::string metaForConversation(
    const ::chat::ConversationMeta& conversation,
    const ::chat::contacts::ContactService& contacts)
{
    std::string meta = protocolLabel(conversation.id.protocol);
    meta += " / ";
    meta += formatChannel(conversation.id.channel);
    if (conversation.id.peer != 0)
    {
        meta += " / ";
        meta += formatNodeLabel(conversation.id.peer);
        if (const auto* node = contacts.getNodeInfo(conversation.id.peer))
        {
            meta += node->via_mqtt ? " / MQTT" : " / LoRa";
        }
    }
    meta += " / ";
    meta += formatAge(conversation.last_timestamp);
    if (conversation.unread > 0)
    {
        char buffer[32] = {};
        std::snprintf(buffer, sizeof(buffer), " / %d unread",
                      conversation.unread);
        meta += buffer;
    }
    return meta;
}

[[nodiscard]] const ::chat::contacts::NodeInfo* nodeForConversation(
    const ::chat::ConversationId& id,
    const ::chat::contacts::ContactService& contacts)
{
    if (id.peer == 0)
    {
        return nullptr;
    }
    return contacts.getNodeInfo(id.peer);
}

[[nodiscard]] std::string groupForConversation(
    const ::chat::ConversationMeta& conversation,
    const ::chat::contacts::NodeInfo* node)
{
    if (conversation.id.peer == 0)
    {
        return "Broadcast";
    }
    if (containsInsensitive(conversation.name, "team") ||
        containsInsensitive(conversation.preview, "team"))
    {
        return "Team";
    }
    if (node != nullptr && node->is_contact)
    {
        return "Contacts";
    }
    return "Nearby";
}

[[nodiscard]] std::string factsForConversation(
    const ::chat::ConversationMeta& conversation,
    const ::chat::contacts::NodeInfo* node,
    bool has_local_gps,
    double local_lat,
    double local_lon)
{
    if (conversation.id.peer == 0)
    {
        return "broadcast / channel-wide";
    }
    if (node == nullptr)
    {
        return "node facts pending";
    }

    std::string facts = "hops ";
    facts += node->hops_away == 0xFF ? "?" : std::to_string(node->hops_away);
    facts += " / ";
    facts += formatAge(node->last_seen);
    facts += " / ";
    facts += node->via_mqtt ? "MQTT" : "LoRa";
    if (has_local_gps && node->position.valid)
    {
        const double lat =
            static_cast<double>(node->position.latitude_i) / 10000000.0;
        const double lon =
            static_cast<double>(node->position.longitude_i) / 10000000.0;
        facts += " / ";
        facts += formatDistance(distanceMeters(local_lat, local_lon, lat, lon));
    }
    return facts;
}

[[nodiscard]] ChatConversationItem makeConversationItem(
    const ::chat::ConversationMeta& conversation,
    const ::chat::ConversationId& active,
    const ::chat::contacts::ContactService& contacts,
    bool has_local_gps,
    double local_lat,
    double local_lon)
{
    ChatConversationItem item{};
    item.id = conversation.id;
    const auto* node = nodeForConversation(conversation.id, contacts);
    item.group = groupForConversation(conversation, node);
    item.title = titleForConversation(conversation.id, conversation.name,
                                      contacts);
    if (!conversation.preview.empty())
    {
        item.preview = conversation.preview;
    }
    else if (conversation.id.peer == 0)
    {
        item.preview = "No messages in this thread yet.";
    }
    item.meta = metaForConversation(conversation, contacts);
    item.facts = factsForConversation(conversation,
                                      node,
                                      has_local_gps,
                                      local_lat,
                                      local_lon);
    item.unread = conversation.unread;
    item.broadcast = conversation.id.peer == 0;
    item.direct = conversation.id.peer != 0;
    item.contact = node != nullptr && node->is_contact;
    item.team = item.group == "Team";
    if (node != nullptr)
    {
        item.last_seen = node->last_seen;
        item.hops_away = node->hops_away;
        if (has_local_gps && node->position.valid)
        {
            const double lat =
                static_cast<double>(node->position.latitude_i) / 10000000.0;
            const double lon =
                static_cast<double>(node->position.longitude_i) / 10000000.0;
            item.distance_m = distanceMeters(local_lat, local_lon, lat, lon);
            item.has_distance = std::isfinite(item.distance_m);
        }
    }
    item.active = sameConversation(conversation.id, active);
    return item;
}

void sortConversations(std::vector<::chat::ConversationMeta>& conversations,
                       ChatThreadSortMode sort_mode,
                       const ::chat::contacts::ContactService& contacts,
                       bool has_local_gps,
                       double local_lat,
                       double local_lon)
{
    auto distance_for = [&](const ::chat::ConversationMeta& conversation)
    {
        const auto* node = nodeForConversation(conversation.id, contacts);
        if (!has_local_gps || node == nullptr || !node->position.valid)
        {
            return std::numeric_limits<double>::infinity();
        }
        const double lat =
            static_cast<double>(node->position.latitude_i) / 10000000.0;
        const double lon =
            static_cast<double>(node->position.longitude_i) / 10000000.0;
        return distanceMeters(local_lat, local_lon, lat, lon);
    };
    auto hops_for = [&](const ::chat::ConversationMeta& conversation)
    {
        const auto* node = nodeForConversation(conversation.id, contacts);
        if (node == nullptr || node->hops_away == 0xFF)
        {
            return 0xFF;
        }
        return static_cast<int>(node->hops_away);
    };
    auto last_seen_for = [&](const ::chat::ConversationMeta& conversation)
    {
        const auto* node = nodeForConversation(conversation.id, contacts);
        return node == nullptr ? conversation.last_timestamp : node->last_seen;
    };

    std::stable_sort(conversations.begin(),
                     conversations.end(),
                     [&](const auto& left, const auto& right)
                     {
                         switch (sort_mode)
                         {
                         case ChatThreadSortMode::Hops:
                             if (hops_for(left) != hops_for(right))
                             {
                                 return hops_for(left) < hops_for(right);
                             }
                             break;
                         case ChatThreadSortMode::Distance:
                             if (distance_for(left) != distance_for(right))
                             {
                                 return distance_for(left) < distance_for(right);
                             }
                             break;
                         case ChatThreadSortMode::LastSeen:
                             if (last_seen_for(left) != last_seen_for(right))
                             {
                                 return last_seen_for(left) > last_seen_for(right);
                             }
                             break;
                         case ChatThreadSortMode::Recent:
                         default:
                             break;
                         }
                         return left.last_timestamp > right.last_timestamp;
                     });
}

[[nodiscard]] std::string unreadSenderSummary(
    ::chat::ChatService& chat_service,
    const ::chat::ConversationMeta& conversation,
    const ::chat::contacts::ContactService& contacts)
{
    if (conversation.unread <= 0)
    {
        return {};
    }

    auto messages = chat_service.getRecentMessages(conversation.id, 64);
    std::vector<::chat::NodeId> senders{};
    senders.reserve(static_cast<std::size_t>(conversation.unread));
    int remaining = conversation.unread;
    for (auto it = messages.rbegin(); it != messages.rend() && remaining > 0;
         ++it)
    {
        if (it->status != ::chat::MessageStatus::Incoming)
        {
            continue;
        }
        --remaining;
        if (it->from == 0)
        {
            continue;
        }
        if (std::find(senders.begin(), senders.end(), it->from) ==
            senders.end())
        {
            senders.push_back(it->from);
        }
    }

    if (senders.empty())
    {
        return "Unread";
    }

    std::string out = "Unread from " +
                      displayNameOrId(contacts, senders.front());
    if (senders.size() > 1U)
    {
        out += " +";
        out += std::to_string(senders.size() - 1U);
    }
    return out;
}

[[nodiscard]] ChatMessageItem makeMessageItem(
    const ::chat::ChatMessage& message,
    ::chat::NodeId self_node,
    const ::chat::contacts::ContactService& contacts)
{
    ChatMessageItem item{};
    item.outgoing = message.from == 0 || message.from == self_node;
    item.failed = message.status == ::chat::MessageStatus::Failed;
    item.sender =
        item.outgoing ? "You" : displayNameOrId(contacts, message.from);
    item.text = message.text.empty() ? "(empty)" : message.text;
    item.meta = statusLabel(message.status);
    item.meta += " / ";
    item.meta += formatAge(message.timestamp);
    if (message.msg_id != 0)
    {
        char buffer[32] = {};
        std::snprintf(buffer, sizeof(buffer), " / #%08lX",
                      static_cast<unsigned long>(message.msg_id));
        item.meta += buffer;
    }
    if (!item.outgoing && message.from != 0)
    {
        item.meta += " / ";
        item.meta += formatNodeLabel(message.from);
    }
    return item;
}

[[nodiscard]] ::chat::ConversationMeta makeSyntheticPrimary(
    const ::chat::ConversationId& id)
{
    ::chat::ConversationMeta conversation{};
    conversation.id = id;
    conversation.name = "Broadcast";
    conversation.preview = "No messages in this thread yet.";
    conversation.last_timestamp = 0;
    conversation.unread = 0;
    return conversation;
}

[[nodiscard]] bool hasDirectConversationForNode(
    const std::vector<::chat::ConversationMeta>& conversations,
    ::chat::NodeId node_id)
{
    return std::any_of(conversations.begin(),
                       conversations.end(),
                       [node_id](const auto& conversation)
                       {
                           return conversation.id.peer == node_id;
                       });
}

void appendNodeConversationIfMissing(
    std::vector<::chat::ConversationMeta>& conversations,
    const ::chat::contacts::NodeInfo& node,
    ::chat::MeshProtocol fallback_protocol)
{
    if (node.node_id == 0 ||
        hasDirectConversationForNode(conversations, node.node_id))
    {
        return;
    }

    ::chat::ConversationMeta conversation{};
    conversation.id =
        ::chat::ConversationId(channelForNode(node),
                               node.node_id,
                               meshProtocolForNode(node.protocol,
                                                   fallback_protocol));
    conversation.name = node.display_name.empty()
                            ? formatNodeLabel(node.node_id)
                            : node.display_name;
    conversation.preview.clear();
    conversation.last_timestamp = node.last_seen;
    conversation.unread = 0;
    conversations.push_back(std::move(conversation));
}

} // namespace

UConsoleChatWorkspaceModel::UConsoleChatWorkspaceModel(
    linux_app::LinuxAppServices& services)
    : services_(services)
{
}

ChatWorkspaceSnapshot UConsoleChatWorkspaceModel::snapshot(
    std::size_t conversation_limit,
    std::size_t message_limit,
    ChatThreadSortMode sort_mode)
{
    ensureActiveConversation();

    ChatWorkspaceSnapshot out{};
    out.active_conversation = active_conversation_;
    out.action_status = action_status_;
    out.total_unread = services_.chat().getTotalUnread();
    out.can_send = canSendActiveConversation();
    out.can_contact_active_peer = active_conversation_.peer != 0;
    out.can_request_nodeinfo =
        services_.meshAdapter() != nullptr && active_conversation_.peer != 0;
    const auto* adapter = services_.meshAdapter();
    const ::chat::MeshCapabilities capabilities =
        adapter == nullptr ? ::chat::MeshCapabilities{}
                           : adapter->getCapabilities();
    const bool appdata_ready = adapter != nullptr && adapter->isReady() &&
                               (active_conversation_.peer == 0
                                    ? capabilities.supports_broadcast_appdata
                                    : capabilities.supports_unicast_appdata);
    auto& contacts = services_.contacts();
    const auto gps = ::platform::ui::gps::get_data();
    const bool has_local_gps = gps.valid;
    const double local_lat = gps.lat;
    const double local_lon = gps.lng;
    out.can_send_position = appdata_ready && has_local_gps;
    out.can_send_poi = appdata_ready && has_local_gps &&
                       active_conversation_.protocol ==
                           ::chat::MeshProtocol::Meshtastic;

    std::size_t total = 0;
    auto conversations =
        loadConversationPage(conversation_limit, &total, sort_mode);
    out.total_conversations = total;

    if (!conversations.empty())
    {
        const bool active_visible =
            std::any_of(conversations.begin(), conversations.end(),
                        [this](const auto& conversation)
                        {
                            return sameConversation(conversation.id,
                                                    active_conversation_);
                        });
        if (!active_visible)
        {
            auto active_meta = makeSyntheticPrimary(active_conversation_);
            active_meta.name =
                titleForConversation(active_conversation_, {}, contacts);
            conversations.insert(conversations.begin(), std::move(active_meta));
        }
    }

    out.conversations.reserve(conversations.size());
    displayed_conversations_.clear();
    displayed_conversations_.reserve(conversations.size());
    for (const auto& conversation : conversations)
    {
        ChatConversationItem item = makeConversationItem(conversation,
                                                         active_conversation_,
                                                         contacts,
                                                         has_local_gps,
                                                         local_lat,
                                                         local_lon);
        item.unread_source =
            unreadSenderSummary(services_.chat(), conversation, contacts);
        out.conversations.push_back(std::move(item));
        displayed_conversations_.push_back(conversation.id);
    }

    auto active_it =
        std::find_if(conversations.begin(), conversations.end(),
                     [this](const auto& conversation)
                     {
                         return sameConversation(conversation.id,
                                                 active_conversation_);
                     });
    if (active_it != conversations.end())
    {
        out.active_title =
            titleForConversation(active_it->id, active_it->name, contacts);
        out.active_meta = metaForConversation(*active_it, contacts);
    }
    else
    {
        out.active_title = titleForConversation(active_conversation_, {},
                                                contacts);
        out.active_meta = protocolLabel(active_conversation_.protocol);
    }

    auto messages =
        services_.chat().getRecentMessages(active_conversation_, message_limit);
    out.messages.reserve(messages.size());
    const ::chat::NodeId self_node = services_.selfNodeId();
    for (const auto& message : messages)
    {
        out.messages.push_back(makeMessageItem(message, self_node, contacts));
        const ::chat::NodeId sender =
            message.from == 0 || message.from == self_node ? 0 : message.from;
        if (sender == 0)
        {
            continue;
        }
        const bool already_listed =
            std::any_of(out.nodes.begin(),
                        out.nodes.end(),
                        [sender](const auto& node)
                        {
                            return node.node_id == sender;
                        });
        if (!already_listed && out.nodes.size() < 5U)
        {
            if (const auto* node = contacts.getNodeInfo(sender))
            {
                out.nodes.push_back(makeNodeInfoItem(*node));
            }
        }
    }

    if (active_conversation_.peer != 0 &&
        std::none_of(out.nodes.begin(),
                     out.nodes.end(),
                     [this](const auto& node)
                     {
                         return node.node_id == active_conversation_.peer;
                     }))
    {
        if (const auto* node = contacts.getNodeInfo(active_conversation_.peer))
        {
            out.nodes.insert(out.nodes.begin(), makeNodeInfoItem(*node));
        }
    }

    return out;
}

ChatNodeDetailSnapshot UConsoleChatWorkspaceModel::nodeDetails(
    ::chat::NodeId node_id) const
{
    ChatNodeDetailSnapshot out{};
    out.node_id = node_id;
    out.title = node_id == 0 ? "Node unavailable" : formatNodeLabel(node_id);

    if (node_id == 0)
    {
        out.subtitle = "No node id was attached to this action.";
        return out;
    }

    const auto* node = services_.contacts().getNodeInfo(node_id);
    if (node == nullptr)
    {
        out.subtitle = "No NodeInfo record is stored locally yet.";
        return out;
    }

    out.found = true;
    out.title = node->display_name.empty() ? formatNodeLabel(node->node_id)
                                           : node->display_name;
    out.subtitle = std::string(contactProtocolLabel(node->protocol)) + " / " +
                   (node->via_mqtt ? "MQTT" : "LoRa") + " / " +
                   formatAge(node->last_seen);
    if (node->position.valid)
    {
        out.has_position = true;
        out.lat = static_cast<double>(node->position.latitude_i) / 10000000.0;
        out.lon = static_cast<double>(node->position.longitude_i) / 10000000.0;

        if (const auto* self_info =
                services_.contacts().getNodeInfo(services_.selfNodeId());
            self_info != nullptr && self_info->position.valid)
        {
            out.has_self_position = true;
            out.self_lat =
                static_cast<double>(self_info->position.latitude_i) /
                10000000.0;
            out.self_lon =
                static_cast<double>(self_info->position.longitude_i) /
                10000000.0;
        }
        else
        {
            const auto gps = ::platform::ui::gps::get_data();
            if (gps.valid)
            {
                out.has_self_position = true;
                out.self_lat = gps.lat;
                out.self_lon = gps.lng;
            }
        }
        if (out.has_self_position)
        {
            out.distance_m =
                distanceMeters(out.self_lat, out.self_lon, out.lat, out.lon);
            out.bearing_deg =
                bearingDegrees(out.self_lat, out.self_lon, out.lat, out.lon);
        }
    }

    ChatNodeDetailSection identity{"Identity", {}};
    appendDetailRow(identity, "Node ID", formatNodeInfoId(node->node_id));
    if (node->short_name[0] != '\0')
    {
        appendDetailRow(identity, "Short name", node->short_name);
    }
    if (node->long_name[0] != '\0')
    {
        appendDetailRow(identity, "Long name", node->long_name);
    }
    appendDetailRow(identity, "Protocol", contactProtocolLabel(node->protocol));
    appendDetailRow(identity, "Role", roleLabel(node->role));
    if (node->hw_model != 0)
    {
        appendDetailRow(identity,
                        "HW model",
                        "Meshtastic #" + std::to_string(node->hw_model));
    }
    appendDetailRow(identity,
                    "Channel",
                    node->channel == 0xFF
                        ? "Unknown"
                        : std::to_string(static_cast<unsigned>(node->channel)));
    appendDetailRow(identity, "Transport", node->via_mqtt ? "MQTT" : "LoRa");
    appendDetailRow(identity, "Contact", node->is_contact ? "Yes" : "No");
    appendDetailRow(identity,
                    "Ignored",
                    node->is_ignored ? "Yes" : "No",
                    node->is_ignored);
    appendDetailSection(out, std::move(identity));

    ChatNodeDetailSection radio{"Radio", {}};
    appendDetailRow(radio,
                    "RSSI",
                    std::isfinite(node->rssi)
                        ? formatFloatValue(node->rssi, " dBm", 0)
                        : "Unknown");
    appendDetailRow(radio,
                    "SNR",
                    std::isfinite(node->snr)
                        ? formatFloatValue(node->snr, " dB", 1)
                        : "Unknown");
    appendDetailRow(radio,
                    "Hops",
                    node->hops_away == 0xFF
                        ? "Unknown"
                        : std::to_string(static_cast<unsigned>(
                              node->hops_away)));
    if (node->next_hop != 0)
    {
        appendDetailRow(radio,
                        "Next hop",
                        std::to_string(static_cast<unsigned>(node->next_hop)));
    }
    appendDetailRow(radio, "Last seen", formatAge(node->last_seen));
    appendDetailSection(out, std::move(radio));

    ChatNodeDetailSection position{"Position", {}};
    if (node->position.valid)
    {
        appendDetailRow(position,
                        "Latitude",
                        formatCoordinate(node->position.latitude_i));
        appendDetailRow(position,
                        "Longitude",
                        formatCoordinate(node->position.longitude_i));
        if (node->position.has_altitude)
        {
            appendDetailRow(position,
                            "Altitude",
                            std::to_string(node->position.altitude) + " m");
        }
        if (node->position.timestamp != 0)
        {
            appendDetailRow(position,
                            "Position age",
                            formatAge(node->position.timestamp));
        }
        if (node->position.precision_bits != 0)
        {
            appendDetailRow(position,
                            "Precision",
                            std::to_string(node->position.precision_bits) +
                                " bits");
        }
        if (node->position.gps_accuracy_mm != 0)
        {
            appendDetailRow(
                position,
                "GPS accuracy",
                formatFloatValue(
                    static_cast<double>(node->position.gps_accuracy_mm) /
                        1000.0,
                    " m",
                    1));
        }
        appendDetailRow(position, "PDOP", formatDop(node->position.pdop));
        appendDetailRow(position, "HDOP", formatDop(node->position.hdop));
        appendDetailRow(position, "VDOP", formatDop(node->position.vdop));
    }
    else
    {
        appendDetailRow(position,
                        "Stored position",
                        "No POSITION packet has been stored.",
                        true);
    }
    appendDetailSection(out, std::move(position));

    ChatNodeDetailSection security{"Security", {}};
    appendDetailRow(security,
                    "Public key",
                    node->has_public_key ? "Stored" : "Not stored",
                    !node->has_public_key);
    appendDetailRow(security,
                    "Key verification",
                    node->key_manually_verified ? "Trusted" : "Unverified",
                    node->has_public_key && !node->key_manually_verified);
    if (node->has_macaddr)
    {
        appendDetailRow(security, "MAC", formatMacAddress(node->macaddr));
    }
    appendDetailSection(out, std::move(security));

    ChatNodeDetailSection metrics{"Device metrics", {}};
    if (node->has_device_metrics)
    {
        const auto& m = node->device_metrics;
        if (m.has_battery_level)
        {
            appendDetailRow(metrics,
                            "Battery",
                            std::to_string(m.battery_level) + "%");
        }
        if (m.has_voltage)
        {
            appendDetailRow(metrics,
                            "Voltage",
                            formatFloatValue(m.voltage, " V", 2));
        }
        if (m.has_channel_utilization)
        {
            appendDetailRow(metrics,
                            "Channel util",
                            formatFloatValue(m.channel_utilization, "%", 1));
        }
        if (m.has_air_util_tx)
        {
            appendDetailRow(metrics,
                            "TX air util",
                            formatFloatValue(m.air_util_tx, "%", 1));
        }
        if (m.has_uptime_seconds)
        {
            appendDetailRow(metrics,
                            "Uptime",
                            std::to_string(m.uptime_seconds) + " s");
        }
    }
    else
    {
        appendDetailRow(metrics, "Telemetry", "No device metrics stored.");
    }
    appendDetailSection(out, std::move(metrics));

    return out;
}

bool UConsoleChatWorkspaceModel::selectConversationAt(
    std::size_t index,
    std::size_t conversation_limit,
    ChatThreadSortMode sort_mode)
{
    if (displayed_conversations_.empty())
    {
        static_cast<void>(snapshot(conversation_limit, 0, sort_mode));
    }
    if (index < displayed_conversations_.size())
    {
        return selectConversation(displayed_conversations_[index]);
    }

    std::size_t total = 0;
    auto conversations = loadConversationPage(conversation_limit,
                                              &total,
                                              sort_mode);
    if (conversations.empty() && index == 0)
    {
        return selectPrimaryConversation();
    }
    if (index >= conversations.size())
    {
        return false;
    }
    return selectConversation(conversations[index].id);
}

bool UConsoleChatWorkspaceModel::selectConversation(
    const ::chat::ConversationId& conversation)
{
    active_conversation_ = conversation;
    active_initialized_ = true;
    services_.chat().markConversationRead(active_conversation_);
    action_status_ = "Conversation selected.";
    return true;
}

bool UConsoleChatWorkspaceModel::selectPrimaryConversation()
{
    return selectConversation(primaryConversation());
}

bool UConsoleChatWorkspaceModel::sendText(const std::string& text)
{
    ensureActiveConversation();
    const std::string trimmed = trimCopy(text);
    if (trimmed.empty())
    {
        action_status_ = "Type a message before sending.";
        return false;
    }
    if (!canSendActiveConversation())
    {
        action_status_ = "No Linux mesh transport is connected.";
        return false;
    }

    const ::chat::MessageId message_id =
        services_.chat().sendText(active_conversation_.channel, trimmed,
                                  active_conversation_.peer);
    if (message_id == 0)
    {
        action_status_ = "Message failed to queue.";
        return false;
    }

    action_status_ = "Message queued.";
    return true;
}

bool UConsoleChatWorkspaceModel::sendCurrentPosition()
{
    ensureActiveConversation();
    auto* adapter = services_.meshAdapter();
    if (adapter == nullptr || !adapter->isReady())
    {
        action_status_ = "No Linux mesh transport is connected.";
        return false;
    }
    const auto gps = ::platform::ui::gps::get_data();
    if (!gps.valid)
    {
        action_status_ = "No GPS fix to send.";
        return false;
    }

    meshtastic_Position pos = meshtastic_Position_init_zero;
    pos.has_latitude_i = true;
    pos.latitude_i = static_cast<std::int32_t>(std::lround(gps.lat * 10000000.0));
    pos.has_longitude_i = true;
    pos.longitude_i = static_cast<std::int32_t>(std::lround(gps.lng * 10000000.0));
    pos.timestamp = sys::epoch_seconds_now();
    pos.location_source = meshtastic_Position_LocSource_LOC_INTERNAL;
    if (gps.has_alt)
    {
        pos.has_altitude = true;
        pos.altitude = static_cast<std::int32_t>(std::lround(gps.alt_m));
        pos.altitude_source = meshtastic_Position_AltSource_ALT_INTERNAL;
    }

    std::uint8_t payload[meshtastic_Position_size] = {};
    pb_ostream_t stream = pb_ostream_from_buffer(payload, sizeof(payload));
    if (!pb_encode(&stream, meshtastic_Position_fields, &pos))
    {
        action_status_ = "Position encoding failed.";
        return false;
    }
    if (!adapter->sendAppData(active_conversation_.channel,
                              meshtastic_PortNum_POSITION_APP,
                              payload,
                              stream.bytes_written,
                              active_conversation_.peer,
                              false))
    {
        action_status_ = "Position failed to queue.";
        return false;
    }
    action_status_ = "Position queued.";
    return true;
}

bool UConsoleChatWorkspaceModel::sendCurrentPoi()
{
    ensureActiveConversation();
    auto* adapter = services_.meshAdapter();
    if (adapter == nullptr || !adapter->isReady())
    {
        action_status_ = "No Linux mesh transport is connected.";
        return false;
    }
    if (active_conversation_.protocol != ::chat::MeshProtocol::Meshtastic)
    {
        action_status_ = "POI sharing is currently Meshtastic only.";
        return false;
    }
    const auto gps = ::platform::ui::gps::get_data();
    if (!gps.valid)
    {
        action_status_ = "No GPS fix to turn into a POI.";
        return false;
    }

    meshtastic_Waypoint waypoint = meshtastic_Waypoint_init_zero;
    waypoint.id = sys::epoch_seconds_now();
    waypoint.has_latitude_i = true;
    waypoint.latitude_i = static_cast<std::int32_t>(std::lround(gps.lat * 10000000.0));
    waypoint.has_longitude_i = true;
    waypoint.longitude_i = static_cast<std::int32_t>(std::lround(gps.lng * 10000000.0));
    waypoint.expire = waypoint.id + 86400U;
    std::strncpy(waypoint.name, "Trail Mate POI", sizeof(waypoint.name) - 1);
    std::strncpy(waypoint.description,
                 "Shared from uConsole current GPS fix",
                 sizeof(waypoint.description) - 1);

    std::uint8_t payload[meshtastic_Waypoint_size] = {};
    pb_ostream_t stream = pb_ostream_from_buffer(payload, sizeof(payload));
    if (!pb_encode(&stream, meshtastic_Waypoint_fields, &waypoint))
    {
        action_status_ = "POI encoding failed.";
        return false;
    }
    if (!adapter->sendAppData(active_conversation_.channel,
                              meshtastic_PortNum_WAYPOINT_APP,
                              payload,
                              stream.bytes_written,
                              active_conversation_.peer,
                              false))
    {
        action_status_ = "POI failed to queue.";
        return false;
    }
    action_status_ = "POI queued.";
    return true;
}

bool UConsoleChatWorkspaceModel::requestActiveNodeInfo()
{
    ensureActiveConversation();
    auto* adapter = services_.meshAdapter();
    if (adapter == nullptr || active_conversation_.peer == 0)
    {
        action_status_ = "Select a direct node first.";
        return false;
    }
    if (!adapter->requestNodeInfo(active_conversation_.peer, true))
    {
        action_status_ = "NodeInfo request is not supported by this transport.";
        return false;
    }
    action_status_ = "NodeInfo request queued.";
    return true;
}

bool UConsoleChatWorkspaceModel::addActivePeerAsContact()
{
    ensureActiveConversation();
    if (active_conversation_.peer == 0)
    {
        action_status_ = "Broadcast cannot be added as a contact.";
        return false;
    }
    const std::string name =
        displayNameOrId(services_.contacts(), active_conversation_.peer);
    if (!services_.contacts().addContact(active_conversation_.peer,
                                         name.c_str()))
    {
        action_status_ = "Contact could not be saved.";
        return false;
    }
    action_status_ = "Contact saved.";
    return true;
}

bool UConsoleChatWorkspaceModel::selectNodeConversation(::chat::NodeId node_id)
{
    ensureActiveConversation();
    if (node_id == 0)
    {
        action_status_ = "Node is unavailable.";
        return false;
    }
    const ::chat::ChannelId channel = active_conversation_.channel;
    return selectConversation(::chat::ConversationId(channel,
                                                     node_id,
                                                     services_.meshProtocol()));
}

bool UConsoleChatWorkspaceModel::addNodeAsContact(::chat::NodeId node_id)
{
    if (node_id == 0)
    {
        action_status_ = "Node is unavailable.";
        return false;
    }
    const std::string name = displayNameOrId(services_.contacts(), node_id);
    if (!services_.contacts().addContact(node_id, name.c_str()))
    {
        action_status_ = "Contact could not be saved.";
        return false;
    }
    action_status_ = "Contact saved.";
    return true;
}

bool UConsoleChatWorkspaceModel::requestNodeInfo(::chat::NodeId node_id)
{
    if (node_id == 0)
    {
        action_status_ = "Node is unavailable.";
        return false;
    }
    auto* adapter = services_.meshAdapter();
    if (adapter == nullptr)
    {
        action_status_ = "No Linux mesh transport is connected.";
        return false;
    }
    if (!adapter->requestNodeInfo(node_id, true))
    {
        action_status_ = "NodeInfo request is not supported by this transport.";
        return false;
    }
    action_status_ = "NodeInfo request queued.";
    return true;
}

bool UConsoleChatWorkspaceModel::exchangeUserInfo(::chat::NodeId node_id)
{
    if (node_id == 0)
    {
        action_status_ = "Node is unavailable.";
        return false;
    }
    auto* adapter = services_.meshAdapter();
    if (adapter == nullptr)
    {
        action_status_ = "No Linux mesh transport is connected.";
        return false;
    }
    if (!adapter->requestNodeInfo(node_id, true))
    {
        action_status_ = "UserInfo exchange is not supported by this transport.";
        return false;
    }
    action_status_ = "UserInfo exchange queued.";
    return true;
}

bool UConsoleChatWorkspaceModel::toggleNodeIgnored(::chat::NodeId node_id)
{
    if (node_id == 0)
    {
        action_status_ = "Node is unavailable.";
        return false;
    }
    const auto* node = services_.contacts().getNodeInfo(node_id);
    if (node == nullptr)
    {
        action_status_ = "Node record is not stored yet.";
        return false;
    }
    const bool next = !node->is_ignored;
    if (!services_.contacts().setNodeIgnored(node_id, next))
    {
        action_status_ = "Ignore state could not be saved.";
        return false;
    }
    action_status_ = next ? "Node ignored." : "Node unignored.";
    return true;
}

bool UConsoleChatWorkspaceModel::verifyNodeKey(::chat::NodeId node_id)
{
    if (node_id == 0)
    {
        action_status_ = "Node is unavailable.";
        return false;
    }

    auto* adapter = services_.meshAdapter();
    const ::chat::MeshCapabilities capabilities =
        adapter == nullptr ? ::chat::MeshCapabilities{}
                           : adapter->getCapabilities();
    if (adapter != nullptr && capabilities.supports_pki &&
        adapter->startKeyVerification(node_id))
    {
        action_status_ = "Key verification started.";
        return true;
    }

    const auto* node = services_.contacts().getNodeInfo(node_id);
    if (node == nullptr || !node->has_public_key)
    {
        action_status_ = "No public key is stored for this node.";
        return false;
    }
    if (node->key_manually_verified)
    {
        action_status_ = "Key is already trusted.";
        return true;
    }
    if (!services_.contacts().setNodeKeyManuallyVerified(node_id, true))
    {
        action_status_ = "Key trust state could not be saved.";
        return false;
    }
    action_status_ = "Key marked trusted.";
    return true;
}

void UConsoleChatWorkspaceModel::ensureActiveConversation()
{
    if (active_initialized_) return;
    active_conversation_ = primaryConversation();
    active_initialized_ = true;
}

::chat::ConversationId UConsoleChatWorkspaceModel::primaryConversation() const
{
    return ::chat::ConversationId(::chat::ChannelId::PRIMARY, 0,
                                  services_.meshProtocol());
}

bool UConsoleChatWorkspaceModel::canSendActiveConversation() const
{
    const auto* adapter = services_.meshAdapter();
    if (adapter == nullptr || !adapter->isReady())
    {
        return false;
    }

    const ::chat::MeshCapabilities capabilities = adapter->getCapabilities();
    return capabilities.supports_unicast_text;
}

std::vector<::chat::ConversationMeta>
UConsoleChatWorkspaceModel::loadConversationPage(std::size_t limit,
                                                 std::size_t* total,
                                                 ChatThreadSortMode sort_mode) const
{
    const std::size_t fetch_limit = std::max<std::size_t>(limit, 64U);
    std::size_t stored_total = 0;
    auto conversations = services_.chat().getConversations(0,
                                                           fetch_limit,
                                                           &stored_total);
    auto& contacts = services_.contacts();
    const auto contact_nodes = contacts.getContacts();
    const auto nearby_nodes = contacts.getNearby();
    const auto fallback_protocol = services_.meshProtocol();
    const std::size_t before_node_conversations = conversations.size();
    for (const auto& node : contact_nodes)
    {
        appendNodeConversationIfMissing(conversations, node, fallback_protocol);
    }
    for (const auto& node : nearby_nodes)
    {
        appendNodeConversationIfMissing(conversations, node, fallback_protocol);
    }
    if (total != nullptr)
    {
        *total =
            stored_total + (conversations.size() - before_node_conversations);
    }
    const auto gps = ::platform::ui::gps::get_data();
    sortConversations(conversations,
                      sort_mode,
                      contacts,
                      gps.valid,
                      gps.lat,
                      gps.lng);
    if (conversations.size() > limit)
    {
        conversations.resize(limit);
    }
    return conversations;
}

} // namespace trailmate::uconsole
