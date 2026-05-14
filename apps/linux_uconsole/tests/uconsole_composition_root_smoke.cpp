#include "uconsole_composition_root.h"

#include "product_composition/app_services_bundle.h"
#include "product_composition/presentation_bundle.h"

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
    const auto root_dir =
        std::filesystem::temp_directory_path() /
        "trailmate_uconsole_composition_root_smoke";

    std::error_code ec;
    std::filesystem::remove_all(root_dir, ec);
    std::filesystem::create_directories(root_dir / "settings", ec);
    std::filesystem::create_directories(root_dir / "sd", ec);
    std::filesystem::create_directories(root_dir / "cache", ec);

    set_env_var("TRAIL_MATE_SETTINGS_ROOT",
                (root_dir / "settings").string());
    set_env_var("TRAIL_MATE_SD_ROOT", (root_dir / "sd").string());
    set_env_var("TRAIL_MATE_CACHE_ROOT", (root_dir / "cache").string());
    set_env_var("TRAIL_MATE_LORA_DISABLE", "1");
    set_env_var("TRAIL_MATE_GPS_VALID", "0");

    trailmate::uconsole::UConsoleCompositionRoot root;
    assert(root.initialize());
    assert(root.deliveryReadModel().size() == 0);
    root.deliveryEventPort().publishDeliveryEvent(
        ::chat::delivery::ChatDeliveryEvent{
            ::chat::delivery::ChatDeliveryRef{0, 77, 0},
            ::chat::delivery::DeliveryState::Failed,
            ::chat::delivery::SendFailureKind::Rejected,
            44});
    assert(root.deliveryReadModel().size() == 1);
    const auto clear_result = root.deliveryActionSink().handleDeliveryAction(
        ::chat::delivery::ChatDeliveryActionRequest{
            ::chat::delivery::ChatDeliveryActionKind::ClearFailure,
            ::chat::delivery::ChatDeliveryRef{0, 77, 0}});
    assert(clear_result.ok);
    assert(root.deliveryReadModel().size() == 0);

    assert(product_composition::hasChatServices(root.appServices()));
    assert(product_composition::hasInteractivePresentation(root.presentation()));

    auto& workspace = root.presentation().workspace;
    assert(workspace.hasChat());
    assert(!workspace.hasTeamChat());
    assert(workspace.hasMap());

    const auto chat_snapshot = workspace.chat->snapshot();
    assert(chat_snapshot.header.valid);

    const auto map_snapshot = workspace.map->snapshot();
    assert(map_snapshot.header.valid);
    assert(map_snapshot.active_tool == ::ui::map::MapToolKind::Pan);

    const auto uconsole_map_snapshot = root.mapModel().snapshot();
    assert(uconsole_map_snapshot.presentation_workspace.header.valid);

    const auto uconsole_chat_snapshot =
        root.chatModel().snapshot(8, 8, trailmate::uconsole::ChatThreadSortMode::Recent);
    assert(uconsole_chat_snapshot.total_conversations >=
           uconsole_chat_snapshot.conversations.size());

    root.shutdown();
    std::filesystem::remove_all(root_dir, ec);
    return 0;
}
