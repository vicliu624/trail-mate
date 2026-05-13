#include "fake/fake_chat_action_sink.h"
#include "fake/fake_chat_presentation_source.h"
#include "fake/fake_device_status_source.h"
#include "fake/fake_gps_status_source.h"
#include "fake/fake_map_action_sink.h"
#include "fake/fake_map_presentation_source.h"
#include "fake/fake_mesh_status_source.h"
#include "fake/fake_settings_action_sink.h"
#include "fake/fake_settings_source.h"

#include "ui_presentation/workspace/presentation_workspace.h"

#include <cassert>

int main()
{
    ui::tests::FakeDeviceStatusSource device_source;
    ui::device::DeviceStatusModel device(device_source);

    ui::tests::FakeGpsStatusSource gps_source;
    ui::gps::GpsStatusModel gps(gps_source);

    ui::tests::FakeMeshStatusSource mesh_source;
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
    ui::tests::FakeMapActionSink map_sink;
    ui::map::MapWorkspaceModel map(map_source, map_sink);

    ui::workspace::PresentationWorkspace empty;
    assert(!empty.hasDevice());
    assert(!empty.hasGps());
    assert(!empty.hasMesh());
    assert(!empty.hasSettings());
    assert(!empty.hasChat());
    assert(!empty.hasTeamChat());
    assert(!empty.hasMap());
    assert(!ui::workspace::hasCoreStatusModels(empty));
    assert(!ui::workspace::hasInteractiveWorkspaceModels(empty));

    ui::workspace::PresentationWorkspace workspace;
    workspace.device = &device;
    workspace.gps = &gps;
    workspace.mesh = &mesh;
    workspace.settings = &settings;
    workspace.chat = &chat;
    workspace.team_chat = &team_chat;
    workspace.map = &map;

    assert(workspace.hasDevice());
    assert(workspace.hasGps());
    assert(workspace.hasMesh());
    assert(workspace.hasSettings());
    assert(workspace.hasChat());
    assert(workspace.hasTeamChat());
    assert(workspace.hasMap());
    assert(ui::workspace::hasCoreStatusModels(workspace));
    assert(ui::workspace::hasInteractiveWorkspaceModels(workspace));

    ui::workspace::PresentationWorkspace status_only;
    status_only.device = &device;
    status_only.gps = &gps;
    status_only.mesh = &mesh;
    assert(ui::workspace::hasCoreStatusModels(status_only));
    assert(!ui::workspace::hasInteractiveWorkspaceModels(status_only));

    return 0;
}
