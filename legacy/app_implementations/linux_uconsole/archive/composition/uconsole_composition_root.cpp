#include "uconsole_composition_root.h"

#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "ui_lvgl_ux_packs/runtime/compatibility_screen_factory.h"
#include "ui_lvgl_ux_packs/ux/ux_menu_provider.h"

namespace trailmate::uconsole
{

UConsoleCompositionRoot::UConsoleCompositionRoot() = default;

UConsoleCompositionRoot::~UConsoleCompositionRoot()
{
    shutdown();
}

bool UConsoleCompositionRoot::initialize()
{
    return initialize(UConsoleCompositionRootConfig{});
}

bool UConsoleCompositionRoot::initialize(const UConsoleCompositionRootConfig& config)
{
    if (initialized_)
    {
        return true;
    }

    if (config.ux_pack_id == nullptr ||
        !ui_lvgl_ux::buildMenuForUxPack(config.ux_pack_id, ux_menu_))
    {
        return false;
    }

    if (!services_.initialize())
    {
        ux_menu_.clear();
        screen_bindings_.clear();
        return false;
    }

    ui_lvgl_ux::CompatibilityScreenFactory screen_factory;
    screen_factory.buildBindingsForMenu(ux_menu_, screen_bindings_);
    if (screen_bindings_.size() == 0)
    {
        ux_menu_.clear();
        screen_bindings_.clear();
        services_.shutdown();
        return false;
    }

    app_services_.chat = &services_.chat();
    app_services_.contacts = &services_.contacts();

    chat_source_ =
        std::make_unique<::ui::presentation_sources::ChatPresentationSource>(
            services_.chat(), &services_.contacts(), &delivery_read_model_);
    chat_sink_ =
        std::make_unique<::ui::presentation_sources::LegacyChatActionSink>(
            services_.chat());
    chat_presentation_model_ =
        std::make_unique<::ui::chat::ChatWorkspaceModel>(*chat_source_,
                                                         *chat_sink_);

    presentation_.workspace.chat = chat_presentation_model_.get();
    presentation_.workspace.map = &map_model_.presentationModel();
    presentation_.ux_menu = &ux_menu_;
    presentation_.screen_bindings = &screen_bindings_;

    initialized_ = true;
    return true;
}

void UConsoleCompositionRoot::shutdown()
{
    initialized_ = false;
    presentation_.workspace = {};
    presentation_.ux_menu = nullptr;
    presentation_.screen_bindings = nullptr;
    ux_menu_.clear();
    screen_bindings_.clear();
    chat_presentation_model_.reset();
    chat_sink_.reset();
    chat_source_.reset();
    app_services_ = {};
    services_.shutdown();
}

linux_app::LinuxAppServices& UConsoleCompositionRoot::services() noexcept
{
    return services_;
}

product_composition::AppServicesBundle& UConsoleCompositionRoot::appServices()
    noexcept
{
    return app_services_;
}

product_composition::PresentationBundle& UConsoleCompositionRoot::presentation()
    noexcept
{
    return presentation_;
}

const ui::menu::MenuModel& UConsoleCompositionRoot::uxMenu() const noexcept
{
    return ux_menu_;
}

UConsoleChatWorkspaceModel& UConsoleCompositionRoot::chatModel() noexcept
{
    return chat_model_;
}

UConsoleMapWorkspaceModel& UConsoleCompositionRoot::mapModel() noexcept
{
    return map_model_;
}

::chat::delivery::ChatDeliveryReadModel&
UConsoleCompositionRoot::deliveryReadModel() noexcept
{
    return delivery_read_model_;
}

::chat::delivery::ChatDeliveryEventProjector&
UConsoleCompositionRoot::deliveryProjector() noexcept
{
    return delivery_projector_;
}

::chat::delivery::IChatDeliveryEventPort&
UConsoleCompositionRoot::deliveryEventPort() noexcept
{
    return delivery_event_port_;
}

::chat::delivery::IChatDeliveryActionSink&
UConsoleCompositionRoot::deliveryActionSink() noexcept
{
    return delivery_action_service_;
}

} // namespace trailmate::uconsole
