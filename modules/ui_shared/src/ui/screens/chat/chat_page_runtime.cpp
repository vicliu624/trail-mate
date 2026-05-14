#include "ui/screens/chat/chat_page_runtime.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/delivery/chat_delivery_action_service.h"
#include "chat/delivery/chat_delivery_event_port.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/team_ui_store_runtime.h"
#include "ui/app_runtime.h"
#include "ui/presentation_sources/legacy_chat_action_sink.h"
#include "ui/presentation_sources/legacy_chat_delivery_action_bridge.h"
#include "ui/presentation_sources/legacy_chat_delivery_event_bridge.h"
#include "ui/presentation_sources/legacy_chat_presentation_source.h"
#include "ui/presentation_sources/team_chat_action_sink.h"
#include "ui/presentation_sources/team_chat_presentation_source.h"
#include "ui/screens/chat/chat_ui_controller.h"
#include "ui/team_actions/legacy_team_action_bridge.h"
#include "ui/ui_common.h"
#include "ui_presentation/chat/chat_workspace_model.h"
#include "team/usecase/team_controller.h"

#include <memory>

namespace
{

const chat::ui::shell::Host* s_host = nullptr;
lv_obj_t* s_chat_container = nullptr;
std::unique_ptr<::ui::presentation_sources::LegacyChatPresentationSource> s_chat_source = nullptr;
std::unique_ptr<::ui::presentation_sources::LegacyChatActionSink> s_chat_sink = nullptr;
std::unique_ptr<::ui::chat::ChatWorkspaceModel> s_chat_model = nullptr;
std::unique_ptr<::chat::delivery::ChatDeliveryReadModel> s_delivery_read_model = nullptr;
std::unique_ptr<::chat::delivery::ChatDeliveryEventProjector> s_delivery_projector = nullptr;
std::unique_ptr<::chat::delivery::ProjectingChatDeliveryEventPort> s_delivery_event_port = nullptr;
std::unique_ptr<::ui::presentation_sources::LegacyChatDeliveryEventBridge> s_delivery_event_bridge = nullptr;
std::unique_ptr<::chat::delivery::ChatDeliveryActionService> s_delivery_action_service = nullptr;
std::unique_ptr<::ui::presentation_sources::LegacyChatDeliveryActionBridge> s_delivery_action_bridge = nullptr;
std::unique_ptr<::ui::presentation_sources::TeamChatPresentationSource> s_team_chat_source = nullptr;
std::unique_ptr<::ui::presentation_sources::ITeamChatCommandPort> s_team_chat_command_port = nullptr;
std::unique_ptr<::ui::presentation_sources::TeamChatActionSink> s_team_chat_sink = nullptr;
std::unique_ptr<::ui::chat::ChatWorkspaceModel> s_team_chat_model = nullptr;
std::unique_ptr<::ui::team_actions::ITeamLocationSource> s_team_location_source = nullptr;
std::unique_ptr<::ui::team_actions::LegacyTeamActionBridge> s_team_action_sink = nullptr;
std::unique_ptr<chat::ui::UiController> s_ui_controller = nullptr;

class TeamControllerChatCommandPort final
    : public ::ui::presentation_sources::ITeamChatCommandPort
{
  public:
    explicit TeamControllerChatCommandPort(::team::TeamController& controller)
        : controller_(controller)
    {
    }

    bool setKeysFromPsk(const ::team::TeamId& team_id,
                        uint32_t key_id,
                        const uint8_t* psk,
                        size_t psk_len) override
    {
        return controller_.setKeysFromPsk(team_id, key_id, psk, psk_len);
    }

    bool sendTeamChat(const ::team::proto::TeamChatMessage& message,
                      uint8_t team_channel_raw) override
    {
        return controller_.onChat(
            message,
            static_cast<::chat::ChannelId>(team_channel_raw));
    }

  private:
    ::team::TeamController& controller_;
};

class GpsTeamLocationSource final
    : public ::ui::team_actions::ITeamLocationSource
{
  public:
    bool currentTeamLocation(::ui::team_actions::TeamLocationSnapshot& out) override
    {
        const auto gps_state = platform::ui::gps::get_data();
        if (!gps_state.valid)
        {
            out = {};
            return false;
        }

        out.valid = true;
        out.lat = gps_state.lat;
        out.lon = gps_state.lng;
        out.has_altitude = gps_state.has_alt;
        out.altitude_m = gps_state.alt_m;
        out.accuracy_m = 0.0f;
        out.timestamp = 0;
        return true;
    }
};

void request_shell_exit(void*)
{
    if (s_host)
    {
        ::ui::page::request_exit(s_host);
        return;
    }
    ui_request_exit_to_menu();
}

} // namespace

namespace chat::ui::runtime
{

bool is_available()
{
    return app::hasAppFacade();
}

lv_obj_t* get_container()
{
    return s_chat_container;
}

void enter(const shell::Host* host, lv_obj_t* parent)
{
    if (!is_available())
    {
        return;
    }

    s_host = host;

    if (s_chat_container && lv_obj_is_valid(s_chat_container))
    {
        lv_obj_del(s_chat_container);
    }
    s_chat_container = nullptr;
    s_ui_controller.reset();
    s_team_action_sink.reset();
    s_team_location_source.reset();
    s_team_chat_model.reset();
    s_team_chat_sink.reset();
    s_team_chat_command_port.reset();
    s_team_chat_source.reset();
    s_chat_model.reset();
    s_chat_sink.reset();
    s_chat_source.reset();
    s_delivery_action_bridge.reset();
    s_delivery_action_service.reset();
    s_delivery_event_bridge.reset();
    s_delivery_event_port.reset();
    s_delivery_projector.reset();
    s_delivery_read_model.reset();

    lv_group_t* prev_group = lv_group_get_default();
    set_default_group(nullptr);

    s_chat_container = lv_obj_create(parent);
    lv_obj_set_size(s_chat_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_chat_container, lv_color_hex(0xFFF3DF), 0);
    lv_obj_set_style_bg_opa(s_chat_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_chat_container, 0, 0);
    lv_obj_set_style_pad_all(s_chat_container, 0, 0);
    lv_obj_set_style_radius(s_chat_container, 0, 0);

    if (app_g != nullptr)
    {
        set_default_group(app_g);
    }
    else
    {
        set_default_group(prev_group);
    }

    chat::ChannelId default_channel = (app::configFacade().getConfig().chat_channel == 1)
                                          ? chat::ChannelId::SECONDARY
                                          : chat::ChannelId::PRIMARY;
    auto& chat_service = app::messagingFacade().getChatService();
    s_delivery_read_model =
        std::make_unique<::chat::delivery::ChatDeliveryReadModel>();
    s_delivery_projector =
        std::make_unique<::chat::delivery::ChatDeliveryEventProjector>(
            *s_delivery_read_model);
    s_delivery_event_port =
        std::make_unique<::chat::delivery::ProjectingChatDeliveryEventPort>(
            *s_delivery_projector);
    s_delivery_event_bridge =
        std::make_unique<::ui::presentation_sources::LegacyChatDeliveryEventBridge>(
            chat_service,
            *s_delivery_event_port);
    s_delivery_action_service =
        std::make_unique<::chat::delivery::ChatDeliveryActionService>(
            *s_delivery_read_model);
    s_delivery_action_bridge =
        std::make_unique<::ui::presentation_sources::LegacyChatDeliveryActionBridge>(
            *s_delivery_action_service);
    s_chat_source = std::unique_ptr<::ui::presentation_sources::LegacyChatPresentationSource>(
        new ::ui::presentation_sources::LegacyChatPresentationSource(
            chat_service,
            &app::messagingFacade().getContactService(),
            s_delivery_read_model.get()));
    s_chat_sink = std::unique_ptr<::ui::presentation_sources::LegacyChatActionSink>(
        new ::ui::presentation_sources::LegacyChatActionSink(chat_service));
    s_chat_model = std::unique_ptr<::ui::chat::ChatWorkspaceModel>(
        new ::ui::chat::ChatWorkspaceModel(*s_chat_source, *s_chat_sink));
    s_team_chat_source =
        std::unique_ptr<::ui::presentation_sources::TeamChatPresentationSource>(
            new ::ui::presentation_sources::TeamChatPresentationSource(
                team::ui::team_ui_get_store()));
    if (auto* team_controller = app::teamFacade().getTeamController())
    {
        s_team_chat_command_port =
            std::unique_ptr<::ui::presentation_sources::ITeamChatCommandPort>(
                new TeamControllerChatCommandPort(*team_controller));
    }
    s_team_chat_sink =
        std::unique_ptr<::ui::presentation_sources::TeamChatActionSink>(
            new ::ui::presentation_sources::TeamChatActionSink(
                team::ui::team_ui_get_store(),
                s_team_chat_command_port.get()));
    s_team_chat_model = std::unique_ptr<::ui::chat::ChatWorkspaceModel>(
        new ::ui::chat::ChatWorkspaceModel(*s_team_chat_source, *s_team_chat_sink));
    s_team_location_source =
        std::unique_ptr<::ui::team_actions::ITeamLocationSource>(
            new GpsTeamLocationSource());
    s_team_action_sink =
        std::unique_ptr<::ui::team_actions::LegacyTeamActionBridge>(
            new ::ui::team_actions::LegacyTeamActionBridge(
                team::ui::team_ui_get_store(),
                s_team_chat_command_port.get(),
                s_team_location_source.get()));

    s_ui_controller = std::unique_ptr<chat::ui::UiController>(
        new chat::ui::UiController(s_chat_container,
                                   chat_service,
                                   *s_chat_model,
                                   *s_team_chat_model,
                                   s_team_action_sink.get(),
                                   s_delivery_event_bridge.get(),
                                   default_channel,
                                   request_shell_exit,
                                   nullptr));
    s_ui_controller->init();

    app::runtimeFacade().setChatUiRuntime(s_ui_controller.get());
}

void exit(lv_obj_t* parent)
{
    (void)parent;

    if (!is_available())
    {
        s_host = nullptr;
        return;
    }

    app::runtimeFacade().setChatUiRuntime(nullptr);
    s_ui_controller.reset();
    s_team_action_sink.reset();
    s_team_location_source.reset();
    s_team_chat_model.reset();
    s_team_chat_sink.reset();
    s_team_chat_command_port.reset();
    s_team_chat_source.reset();
    s_chat_model.reset();
    s_chat_sink.reset();
    s_chat_source.reset();
    s_delivery_action_bridge.reset();
    s_delivery_action_service.reset();
    s_delivery_event_bridge.reset();
    s_delivery_event_port.reset();
    s_delivery_projector.reset();
    s_delivery_read_model.reset();

    if (s_chat_container && !lv_obj_is_valid(s_chat_container))
    {
        s_chat_container = nullptr;
    }
    if (s_chat_container)
    {
        lv_obj_del(s_chat_container);
        s_chat_container = nullptr;
    }

    s_host = nullptr;
}

} // namespace chat::ui::runtime
