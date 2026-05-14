#include "linux_sim_composition_root.h"

#include "product_composition/presentation_bundle.h"
#include "ui_presentation/workspace/presentation_workspace_probe.h"

#include <cassert>

int main()
{
    trailmate::linux_sim::LinuxSimCompositionRoot root;
    assert(root.initialize());
    assert(root.deliveryReadModel().size() == 0);
    root.deliveryEventPort().publishDeliveryEvent(
        ::chat::delivery::ChatDeliveryEvent{
            ::chat::delivery::ChatDeliveryRef{0, 42, 0},
            ::chat::delivery::DeliveryState::Sent,
            ::chat::delivery::SendFailureKind::None,
            12});
    assert(root.deliveryReadModel().size() == 1);

    auto& presentation = root.presentation();
    assert(product_composition::hasInteractivePresentation(presentation));

    auto& workspace = presentation.workspace;
    assert(ui::workspace::hasCoreStatusModels(workspace));
    assert(workspace.hasDevice());
    assert(workspace.hasGps());
    assert(workspace.hasMesh());
    assert(workspace.hasSettings());
    assert(workspace.hasChat());
    assert(workspace.hasTeamChat());
    assert(workspace.hasMap());

    auto snapshot =
        ui::workspace::PresentationWorkspaceProbe::snapshot(workspace);
    assert(snapshot.header.valid);
    assert(snapshot.has_device);
    assert(snapshot.has_gps);
    assert(snapshot.has_mesh);
    assert(snapshot.has_settings);
    assert(snapshot.has_chat);
    assert(snapshot.has_team_chat);
    assert(snapshot.has_map);

    return 0;
}
