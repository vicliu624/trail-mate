#include "linux_sim_composition_root.h"

#include "product_composition/presentation_bundle.h"
#include "ui_presentation/menu/menu_model.h"
#include "ui_presentation/workspace/presentation_workspace_probe.h"

#include <cassert>
#include <cstddef>

namespace
{

bool contains(const ui::menu::MenuModel& menu, ui::menu::MenuScreenId id)
{
    for (std::size_t index = 0; index < menu.size(); ++index)
    {
        if (menu.items()[index].screen_id == id)
        {
            return true;
        }
    }
    return false;
}

} // namespace

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
    root.deliveryEventPort().publishDeliveryEvent(
        ::chat::delivery::ChatDeliveryEvent{
            ::chat::delivery::ChatDeliveryRef{0, 43, 0},
            ::chat::delivery::DeliveryState::Failed,
            ::chat::delivery::SendFailureKind::AckTimeout,
            13});
    assert(root.deliveryReadModel().size() == 2);
    const auto clear_result = root.deliveryActionSink().handleDeliveryAction(
        ::chat::delivery::ChatDeliveryActionRequest{
            ::chat::delivery::ChatDeliveryActionKind::ClearFailure,
            ::chat::delivery::ChatDeliveryRef{0, 43, 0}});
    assert(clear_result.ok);
    assert(root.deliveryReadModel().size() == 1);

    auto& presentation = root.presentation();
    assert(product_composition::hasInteractivePresentation(presentation));
    assert(product_composition::hasUxMenu(presentation));
    assert(root.uxMenu().size() > 0);
    assert(root.presentation().ux_menu == &root.uxMenu());
    assert(contains(root.uxMenu(), ui::menu::MenuScreenId::Dashboard));
    assert(contains(root.uxMenu(), ui::menu::MenuScreenId::Chat));
    assert(contains(root.uxMenu(), ui::menu::MenuScreenId::Map));
    assert(contains(root.uxMenu(), ui::menu::MenuScreenId::Gps));
    assert(contains(root.uxMenu(), ui::menu::MenuScreenId::Settings));

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
