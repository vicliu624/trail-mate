#include "ui/presentation_sources/legacy_mesh_status_source.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/usecase/chat_service.h"
#include "platform/ui/team_ui_store_runtime.h"
#include "sys/clock.h"

#include <cstdio>

#if __has_include("ble/ble_manager.h")
#include "ble/ble_manager.h"
#define TRAIL_MATE_UI_PRESENTATION_HAS_BLE_MANAGER_HEADER 1
#else
#define TRAIL_MATE_UI_PRESENTATION_HAS_BLE_MANAGER_HEADER 0
#endif

namespace ui::presentation_sources
{

bool LegacyMeshStatusSource::buildMeshStatusSnapshot(ui::mesh::MeshStatusSnapshot& out) const
{
    out = ui::mesh::MeshStatusSnapshot{};
    out.header.valid = true;
    out.header.version = 1;
    out.header.generated_at_ms = sys::millis_now();
    out.state = ui::mesh::MeshDisplayState::Ready;
    out.radio_ready = true;

    team::ui::TeamUiSnapshot team_snapshot;
    const bool has_team = team::ui::team_ui_get_store().load(team_snapshot) && team_snapshot.in_team;
    const uint32_t team_unread = has_team ? team_snapshot.team_chat_unread : 0U;
    const uint32_t chat_unread =
        static_cast<uint32_t>(app::messagingFacade().getChatService().getTotalUnread());
    const uint32_t unread = chat_unread + team_unread;

    bool ble_active = app::runtimeFacade().isBleEnabled();
    bool ble_linked = false;
#if TRAIL_MATE_UI_PRESENTATION_HAS_BLE_MANAGER_HEADER
    if (auto* ble = app::runtimeFacade().getBleManager())
    {
        ble_active = ble->isEnabled();
        ble::BlePairingStatus ble_status{};
        if (ble->getPairingStatus(&ble_status))
        {
            ble_linked = ble_status.is_connected;
        }
    }
#endif

    out.team_linked = has_team;
    out.known_nodes = has_team ? static_cast<uint32_t>(team_snapshot.members.size()) : 0U;
    out.unread_messages = unread;

    ui::copyText(out.protocol_label,
                 chat::infra::meshProtocolName(app::configFacade().getConfig().mesh_protocol));
    ui::copyText(out.radio_label,
                 ble_linked ? "BLE linked"
                            : (ble_active ? "BLE bridge ready" : "LoRa direct path"));

    char status[64];
    std::snprintf(status,
                  sizeof(status),
                  "%s  |  %s",
                  out.radio_label.c_str(),
                  unread > 0 ? "new activity" : "quiet net");
    ui::copyText(out.status_line, status);
    return true;
}

LegacyMeshStatusSource& legacy_mesh_status_source()
{
    static LegacyMeshStatusSource source;
    return source;
}

} // namespace ui::presentation_sources
