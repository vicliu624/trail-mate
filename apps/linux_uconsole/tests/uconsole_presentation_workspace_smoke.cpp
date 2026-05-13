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

#include <cassert>

int main()
{
    ui::tests::FakeDeviceStatusSource device_source;
    device_source.snapshot_value.header.valid = true;
    ui::copyText(device_source.snapshot_value.status_line, "uConsole device");
    ui::device::DeviceStatusModel device(device_source);

    ui::tests::FakeGpsStatusSource gps_source;
    gps_source.snapshot_value.header.valid = true;
    gps_source.snapshot_value.fix_valid = true;
    gps_source.snapshot_value.satellites = 6;
    ui::gps::GpsStatusModel gps(gps_source);

    ui::tests::FakeMeshStatusSource mesh_source;
    mesh_source.snapshot_value.header.valid = true;
    mesh_source.snapshot_value.radio_ready = true;
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

    assert(snapshot.header.valid);
    assert(snapshot.has_device);
    assert(snapshot.has_gps);
    assert(snapshot.has_mesh);
    assert(snapshot.has_settings);
    assert(snapshot.has_chat);
    assert(snapshot.has_team_chat);
    assert(snapshot.has_map);
    assert(snapshot.device.header.valid);
    assert(snapshot.gps.fix_valid);
    assert(snapshot.mesh.radio_ready);
    assert(snapshot.map.self.valid);

    return 0;
}
