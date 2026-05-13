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
#include <cstring>

namespace
{

void test_empty_workspace_is_valid_empty_summary()
{
    ui::workspace::PresentationWorkspace workspace;

    const auto snapshot =
        ui::workspace::PresentationWorkspaceProbe::snapshot(workspace);

    assert(snapshot.header.valid);
    assert(snapshot.header.version == 1);
    assert(!snapshot.has_device);
    assert(!snapshot.has_gps);
    assert(!snapshot.has_mesh);
    assert(!snapshot.has_settings);
    assert(!snapshot.has_chat);
    assert(!snapshot.has_team_chat);
    assert(!snapshot.has_map);
}

void test_probe_reads_small_status_and_map_snapshots()
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
    mesh_source.snapshot_value.known_nodes = 4;
    ui::copyText(mesh_source.snapshot_value.status_line, "Mesh ready");
    ui::mesh::MeshStatusModel mesh(mesh_source);

    ui::tests::FakeSettingsSource settings_source;
    ui::tests::FakeSettingsActionSink settings_sink;
    ui::settings::SettingsModel settings(settings_source, settings_sink);

    ui::tests::FakeChatPresentationSource chat_source;
    chat_source.available = false;
    ui::tests::FakeChatActionSink chat_sink;
    ui::chat::ChatWorkspaceModel chat(chat_source, chat_sink);

    ui::tests::FakeChatPresentationSource team_chat_source;
    team_chat_source.available = false;
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

    assert(snapshot.header.valid);
    assert(snapshot.has_device);
    assert(snapshot.has_gps);
    assert(snapshot.has_mesh);
    assert(snapshot.has_settings);
    assert(snapshot.has_chat);
    assert(snapshot.has_team_chat);
    assert(snapshot.has_map);

    assert(std::strcmp(snapshot.device.status_line.c_str(), "Device ready") == 0);
    assert(snapshot.gps.fix_valid);
    assert(snapshot.gps.satellites == 8);
    assert(std::strcmp(snapshot.gps.fix_label.c_str(), "FIX") == 0);
    assert(snapshot.mesh.known_nodes == 4);
    assert(std::strcmp(snapshot.mesh.status_line.c_str(), "Mesh ready") == 0);
    assert(snapshot.map.self.valid);
    assert(snapshot.map.self.lat == 52.4068);
    assert(snapshot.map.self.lon == -1.5197);
    assert(std::strcmp(snapshot.map.status_line.c_str(), "GPS fix") == 0);

    assert(chat_source.build_count == 0);
    assert(team_chat_source.build_count == 0);
}

} // namespace

int main()
{
    test_empty_workspace_is_valid_empty_summary();
    test_probe_reads_small_status_and_map_snapshots();
    return 0;
}
