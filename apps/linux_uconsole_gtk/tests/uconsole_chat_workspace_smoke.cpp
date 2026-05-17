#include "app/linux_app_services.h"
#include "chat/domain/contact_types.h"
#include "chat/usecase/contact_service.h"
#include "uconsole/uconsole_chat_workspace_model.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace
{

void set_env_var(const char* name, const std::string& value)
{
    setenv(name, value.c_str(), 1);
}

} // namespace

int main()
{
    const auto root =
        std::filesystem::temp_directory_path() /
        "trailmate_uconsole_chat_workspace_smoke";

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "settings", ec);
    std::filesystem::create_directories(root / "sd", ec);
    std::filesystem::create_directories(root / "cache", ec);

    set_env_var("TRAIL_MATE_SETTINGS_ROOT", (root / "settings").string());
    set_env_var("TRAIL_MATE_SD_ROOT", (root / "sd").string());
    set_env_var("TRAIL_MATE_CACHE_ROOT", (root / "cache").string());
    set_env_var("TRAIL_MATE_LORA_DISABLE", "1");
    set_env_var("TRAIL_MATE_GPS_VALID", "0");

    trailmate::linux_app::LinuxAppServices services;
    trailmate::uconsole::UConsoleChatWorkspaceModel model(services);

    ::chat::contacts::NodeUpdate node{};
    node.short_name = "AAEC";
    node.long_name = "lilygo-AAEC";
    node.has_last_seen = true;
    node.last_seen = 123456U;
    node.has_protocol = true;
    node.protocol =
        static_cast<std::uint8_t>(::chat::contacts::NodeProtocolType::Meshtastic);
    node.has_channel = true;
    node.channel = 0;
    node.has_hops_away = true;
    node.hops_away = 2;
    node.has_snr = true;
    node.snr = 6.5F;
    node.has_rssi = true;
    node.rssi = -72.0F;
    node.has_position = true;
    node.position.valid = true;
    node.position.latitude_i = 312304000;
    node.position.longitude_i = 1214737000;
    services.contacts().applyNodeUpdate(0x0C16AAECU, node);

    const auto snapshot =
        model.snapshot(12, 20, trailmate::uconsole::ChatThreadSortMode::LastSeen);
    bool found_node_thread = false;
    std::size_t node_index = 0;
    for (std::size_t index = 0; index < snapshot.conversations.size(); ++index)
    {
        const auto& item = snapshot.conversations[index];
        if (item.id.peer == 0x0C16AAECU)
        {
            found_node_thread = true;
            node_index = index;
            assert(item.group == "Nearby");
            assert(item.title == "AAEC");
            assert(item.preview.empty());
            assert(item.facts.find("hops 2") != std::string::npos);
        }
    }
    assert(found_node_thread);

    assert(model.selectConversationAt(node_index,
                                      12,
                                      trailmate::uconsole::ChatThreadSortMode::
                                          LastSeen));
    const auto selected =
        model.snapshot(12, 20, trailmate::uconsole::ChatThreadSortMode::LastSeen);
    assert(selected.active_conversation.peer == 0x0C16AAECU);
    assert(!selected.nodes.empty());
    assert(selected.nodes.front().node_id == 0x0C16AAECU);
    assert(selected.nodes.front().title == "AAEC");

    const auto details = model.nodeDetails(0x0C16AAECU);
    assert(details.found);
    assert(details.has_position);
    assert(details.lat > 31.0);
    assert(details.lon > 121.0);
    assert(!details.sections.empty());

    std::filesystem::remove_all(root, ec);
    return 0;
}
