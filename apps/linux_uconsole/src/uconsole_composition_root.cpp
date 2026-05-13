#include "uconsole_composition_root.h"

#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"

namespace trailmate::uconsole
{

UConsoleCompositionRoot::UConsoleCompositionRoot() = default;

UConsoleCompositionRoot::~UConsoleCompositionRoot()
{
    shutdown();
}

bool UConsoleCompositionRoot::initialize()
{
    if (initialized_)
    {
        return true;
    }

    if (!services_.initialize())
    {
        return false;
    }

    app_services_.chat = &services_.chat();
    app_services_.contacts = &services_.contacts();

    chat_source_ =
        std::make_unique<::ui::presentation_sources::LegacyChatPresentationSource>(
            services_.chat(), &services_.contacts());
    chat_sink_ =
        std::make_unique<::ui::presentation_sources::LegacyChatActionSink>(
            services_.chat());
    chat_presentation_model_ =
        std::make_unique<::ui::chat::ChatWorkspaceModel>(*chat_source_,
                                                         *chat_sink_);

    presentation_.workspace.chat = chat_presentation_model_.get();
    presentation_.workspace.map = &map_model_.presentationModel();

    initialized_ = true;
    return true;
}

void UConsoleCompositionRoot::shutdown()
{
    initialized_ = false;
    presentation_.workspace = {};
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

UConsoleChatWorkspaceModel& UConsoleCompositionRoot::chatModel() noexcept
{
    return chat_model_;
}

UConsoleMapWorkspaceModel& UConsoleCompositionRoot::mapModel() noexcept
{
    return map_model_;
}

} // namespace trailmate::uconsole
