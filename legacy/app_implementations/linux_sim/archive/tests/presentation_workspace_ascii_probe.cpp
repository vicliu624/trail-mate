#include "fake/fake_chat_action_sink.h"
#include "fake/fake_chat_presentation_source.h"
#include "fake/fake_device_status_source.h"
#include "fake/fake_gps_status_source.h"
#include "fake/fake_map_action_sink.h"
#include "fake/fake_map_presentation_source.h"
#include "fake/fake_mesh_status_source.h"
#include "fake/fake_settings_action_sink.h"
#include "fake/fake_settings_source.h"

#include "ui_presentation/common/fixed_text.h"
#include "ui_presentation/workspace/presentation_workspace.h"
#include "ui_presentation/workspace/presentation_workspace_probe.h"

#include <cstdio>

namespace
{

const char* yesNo(bool value)
{
    return value ? "yes" : "no";
}

} // namespace

int main()
{
    ui::tests::FakeDeviceStatusSource device_source;
    device_source.snapshot_value.header.valid = true;
    ui::copyText(device_source.snapshot_value.status_line, "Device ready");
    ui::device::DeviceStatusModel device(device_source);

    ui::tests::FakeGpsStatusSource gps_source;
    gps_source.snapshot_value.header.valid = true;
    gps_source.snapshot_value.fix_valid = true;
    gps_source.snapshot_value.satellites = 8;
    ui::copyText(gps_source.snapshot_value.fix_label, "FIX");
    ui::gps::GpsStatusModel gps(gps_source);

    ui::tests::FakeMeshStatusSource mesh_source;
    mesh_source.snapshot_value.header.valid = true;
    ui::copyText(mesh_source.snapshot_value.status_line, "Mesh ready");
    ui::mesh::MeshStatusModel mesh(mesh_source);

    ui::tests::FakeSettingsSource settings_source;
    ui::tests::FakeSettingsActionSink settings_sink;
    ui::settings::SettingsModel settings(settings_source, settings_sink);

    ui::tests::FakeChatPresentationSource chat_source;
    ui::tests::FakeChatActionSink chat_sink;
    ui::chat::ChatWorkspaceModel chat(chat_source, chat_sink);

    ui::tests::FakeChatPresentationSource team_chat_source;
    ui::tests::FakeChatActionSink team_chat_sink;
    ui::chat::ChatWorkspaceModel team_chat(team_chat_source, team_chat_sink);

    ui::tests::FakeMapPresentationSource map_source;
    map_source.snapshot_value.header.valid = true;
    map_source.snapshot_value.self.valid = true;
    map_source.snapshot_value.self.lat = 52.4068;
    map_source.snapshot_value.self.lon = -1.5197;
    ui::copyText(map_source.snapshot_value.status_line, "GPS fix");
    ui::tests::FakeMapActionSink map_sink;
    ui::map::MapWorkspaceModel map(map_source, map_sink);

    ui::workspace::PresentationWorkspace workspace;
    workspace.device = &device;
    workspace.gps = &gps;
    workspace.mesh = &mesh;
    workspace.settings = &settings;
    workspace.chat = &chat;
    workspace.team_chat = &team_chat;
    workspace.map = &map;

    const auto snapshot =
        ui::workspace::PresentationWorkspaceProbe::snapshot(workspace);

    if (!snapshot.header.valid)
    {
        std::printf("PRESENTATION: unavailable\n");
        return 1;
    }

    std::printf("PRESENTATION WORKSPACE\n");
    std::printf("DEVICE: %s %s\n",
                yesNo(snapshot.has_device),
                snapshot.device.status_line.c_str());
    std::printf("GPS: %s %s sats=%u\n",
                yesNo(snapshot.has_gps),
                snapshot.gps.fix_label.c_str(),
                static_cast<unsigned>(snapshot.gps.satellites));
    std::printf("MESH: %s %s\n",
                yesNo(snapshot.has_mesh),
                snapshot.mesh.status_line.c_str());
    std::printf("SETTINGS: %s\n", yesNo(snapshot.has_settings));
    std::printf("CHAT: %s\n", yesNo(snapshot.has_chat));
    std::printf("TEAM_CHAT: %s\n", yesNo(snapshot.has_team_chat));
    std::printf("MAP: %s %.4f,%.4f %s\n",
                yesNo(snapshot.has_map),
                snapshot.map.self.lat,
                snapshot.map.self.lon,
                snapshot.map.status_line.c_str());

    return 0;
}
