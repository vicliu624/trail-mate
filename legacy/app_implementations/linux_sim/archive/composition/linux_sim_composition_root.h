#pragma once

#include "fake/fake_chat_action_sink.h"
#include "fake/fake_chat_presentation_source.h"
#include "fake/fake_device_status_source.h"
#include "fake/fake_gps_status_source.h"
#include "fake/fake_map_action_sink.h"
#include "fake/fake_map_presentation_source.h"
#include "fake/fake_mesh_status_source.h"
#include "fake/fake_settings_action_sink.h"
#include "fake/fake_settings_source.h"

#include "chat/delivery/chat_delivery_action_service.h"
#include "chat/delivery/chat_delivery_event_port.h"
#include "product_composition/presentation_bundle.h"
#include "ui_presentation/chat/chat_workspace_model.h"
#include "ui_presentation/device/device_status_model.h"
#include "ui_presentation/gps/gps_status_model.h"
#include "ui_presentation/menu/menu_model.h"
#include "ui_presentation/map/map_workspace_model.h"
#include "ui_presentation/mesh/mesh_status_model.h"
#include "ui_presentation/screen/screen_binding_registry.h"
#include "ui_presentation/settings/settings_model.h"

namespace trailmate::linux_sim
{

struct LinuxSimCompositionRootConfig
{
    const char* ux_pack_id = "simulator_full";
};

class LinuxSimCompositionRoot
{
  public:
    bool initialize();
    bool initialize(const LinuxSimCompositionRootConfig& config);

    product_composition::PresentationBundle& presentation();
    const product_composition::PresentationBundle& presentation() const;
    const ui::menu::MenuModel& uxMenu() const;
    chat::delivery::ChatDeliveryReadModel& deliveryReadModel();
    chat::delivery::ChatDeliveryEventProjector& deliveryProjector();
    chat::delivery::IChatDeliveryEventPort& deliveryEventPort();
    chat::delivery::IChatDeliveryActionSink& deliveryActionSink();

  private:
    chat::delivery::ChatDeliveryReadModel delivery_read_model_{};
    chat::delivery::ChatDeliveryEventProjector delivery_projector_{
        delivery_read_model_};
    chat::delivery::ProjectingChatDeliveryEventPort delivery_event_port_{
        delivery_projector_};
    chat::delivery::ChatDeliveryActionService delivery_action_service_{
        delivery_read_model_};

    ui::tests::FakeDeviceStatusSource device_source_{};
    ui::device::DeviceStatusModel device_model_{device_source_};

    ui::tests::FakeGpsStatusSource gps_source_{};
    ui::gps::GpsStatusModel gps_model_{gps_source_};

    ui::tests::FakeMeshStatusSource mesh_source_{};
    ui::mesh::MeshStatusModel mesh_model_{mesh_source_};

    ui::tests::FakeSettingsSource settings_source_{};
    ui::tests::FakeSettingsActionSink settings_sink_{};
    ui::settings::SettingsModel settings_model_{settings_source_, settings_sink_};

    ui::tests::FakeChatPresentationSource chat_source_{};
    ui::tests::FakeChatActionSink chat_sink_{};
    ui::chat::ChatWorkspaceModel chat_model_{chat_source_, chat_sink_};

    ui::tests::FakeChatPresentationSource team_chat_source_{};
    ui::tests::FakeChatActionSink team_chat_sink_{};
    ui::chat::ChatWorkspaceModel team_chat_model_{team_chat_source_, team_chat_sink_};

    ui::tests::FakeMapPresentationSource map_source_{};
    ui::tests::FakeMapActionSink map_sink_{};
    ui::map::MapWorkspaceModel map_model_{map_source_, map_sink_};

    product_composition::PresentationBundle presentation_{};
    ui::menu::MenuModel ux_menu_{};
    ui::screen::ScreenBindingRegistry screen_bindings_{};
};

} // namespace trailmate::linux_sim
