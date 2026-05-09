#include "uconsole/uconsole_dashboard_model.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "app/linux_app_services.h"
#include "chat/domain/contact_types.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "platform/linux/map_tile_cache.h"
#include "platform/linux/runtime_paths.h"
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

    std::size_t conversation_total = 0;
    const auto conversations =
        chat_service.getConversations(0, 6, &conversation_total);
    out.conversation_count = conversation_total;
    out.unread_count = chat_service.getTotalUnread();

    out.mesh_protocol = protocolLabel(services_.meshProtocol());
    out.self_node = formatNodeId(services_.selfNodeId());

    out.conversations.reserve(conversations.size());
    for (const auto& item : conversations)
    {
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

    std::string mesh_line = "Mesh: ";
    mesh_line += out.mesh_protocol;
    if (mesh_adapter == nullptr)
    {
        mesh_line += " transport unavailable";
    }
    else
    {
        const ::chat::MeshCapabilities capabilities =
            mesh_adapter->getCapabilities();
        mesh_line += capabilities.supports_unicast_text
                         ? " transport ready"
                         : " transport not connected";
    }

    const ::platform::linux_runtime::MapTileCache tile_cache;
    const auto tile_stats = tile_cache.stats();
    out.capability_lines = {
        "Mode: Linux desktop-class handheld",
        "AIO2: no adapter connected",
        mesh_line,
        "BLE: not used on Linux",
        "Storage: SQLite " +
            ::platform::linux_runtime::sqlite_database_path().string(),
        "Map cache: " + std::to_string(tile_stats.cached_tiles) +
            " tiles at " + tile_stats.root.string(),
    };

    return out;
}

} // namespace trailmate::uconsole
