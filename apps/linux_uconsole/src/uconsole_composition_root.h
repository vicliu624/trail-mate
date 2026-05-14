#pragma once

#include "app/linux_app_services.h"
#include "chat/delivery/chat_delivery_event_port.h"
#include "product_composition/app_services_bundle.h"
#include "product_composition/presentation_bundle.h"
#include "uconsole/uconsole_chat_workspace_model.h"
#include "uconsole/uconsole_map_workspace_model.h"
#include "ui/presentation_sources/legacy_chat_action_sink.h"
#include "ui/presentation_sources/legacy_chat_presentation_source.h"
#include "ui_presentation/chat/chat_workspace_model.h"

#include <memory>

namespace trailmate::uconsole
{

class UConsoleCompositionRoot final
{
  public:
    UConsoleCompositionRoot();
    ~UConsoleCompositionRoot();

    UConsoleCompositionRoot(const UConsoleCompositionRoot&) = delete;
    UConsoleCompositionRoot& operator=(const UConsoleCompositionRoot&) = delete;

    bool initialize();
    void shutdown();

    [[nodiscard]] linux_app::LinuxAppServices& services() noexcept;
    [[nodiscard]] product_composition::AppServicesBundle& appServices() noexcept;
    [[nodiscard]] product_composition::PresentationBundle& presentation()
        noexcept;
    [[nodiscard]] UConsoleChatWorkspaceModel& chatModel() noexcept;
    [[nodiscard]] UConsoleMapWorkspaceModel& mapModel() noexcept;
    [[nodiscard]] ::chat::delivery::ChatDeliveryReadModel& deliveryReadModel()
        noexcept;
    [[nodiscard]] ::chat::delivery::ChatDeliveryEventProjector&
    deliveryProjector() noexcept;
    [[nodiscard]] ::chat::delivery::IChatDeliveryEventPort&
    deliveryEventPort() noexcept;

  private:
    ::chat::delivery::ChatDeliveryReadModel delivery_read_model_{};
    ::chat::delivery::ChatDeliveryEventProjector delivery_projector_{
        delivery_read_model_};
    ::chat::delivery::ProjectingChatDeliveryEventPort delivery_event_port_{
        delivery_projector_};

    linux_app::LinuxAppServices services_{};
    UConsoleChatWorkspaceModel chat_model_{services_};
    UConsoleMapWorkspaceModel map_model_{services_};

    product_composition::AppServicesBundle app_services_{};
    product_composition::PresentationBundle presentation_{};

    std::unique_ptr<::ui::presentation_sources::LegacyChatPresentationSource>
        chat_source_{};
    std::unique_ptr<::ui::presentation_sources::LegacyChatActionSink>
        chat_sink_{};
    std::unique_ptr<::ui::chat::ChatWorkspaceModel> chat_presentation_model_{};

    bool initialized_ = false;
};

} // namespace trailmate::uconsole
