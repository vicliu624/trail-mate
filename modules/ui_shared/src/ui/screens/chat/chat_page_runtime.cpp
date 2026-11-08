#include "ui/screens/chat/chat_page_runtime.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/delivery/chat_delivery_action_service.h"
#include "chat/delivery/chat_delivery_event_port.h"
#include "platform/ui/team_ui_store_runtime.h"
#include "ui/app_runtime.h"
#include "ui/presentation_sources/chat_presentation_source.h"
#include "ui/presentation_sources/runtime_chat_action_sink.h"
#include "ui/presentation_sources/team_chat_action_sink.h"
#include "ui/presentation_sources/team_chat_presentation_source.h"
#include "ui/runtime/ui_feedback.h"
#include "ui/screens/chat/chat_team_workflow.h"
#include "ui/screens/chat/chat_ui_controller.h"
#include "ui/team_actions/team_action_runtime_sink.h"
#include "ui/team_actions/team_runtime_adapters.h"
#include "ui/ui_common.h"
#include "ui_chat_runtime/chat_delivery_action_port_adapter.h"
#include "ui_chat_runtime/chat_delivery_event_projection_adapter.h"
#include "ui_chat_runtime/chat_page_runtime_event_pump.h"
#include "ui_key_verification_runtime/key_verification_action_sink.h"
#include "ui_key_verification_runtime/key_verification_presentation_source.h"
#include "ui_key_verification_runtime/key_verification_session_adapter.h"
#include "ui_presentation/chat/chat_workspace_model.h"
#include "ui_presentation/key_verification/key_verification_model.h"

#include <limits>
#include <memory>

namespace
{

class ChatServiceRetryChatMessagePort final
    : public ::chat::delivery::IRetryChatMessagePort
{
  public:
    explicit ChatServiceRetryChatMessagePort(::chat::ChatService& chat_service)
        : chat_service_(chat_service)
    {
    }

    ::chat::delivery::ChatDeliveryActionResult retryMessage(
        ::chat::delivery::ChatDeliveryRef ref) override
    {
        const ::chat::MessageId msg_id = messageIdFromRef(ref);
        if (msg_id == 0)
        {
            return ::chat::delivery::ChatDeliveryActionResult::fail(
                ::chat::delivery::ChatDeliveryActionFailure::InvalidRef);
        }

        const bool has_protocol = ref.protocol != 0;
        const auto protocol =
            static_cast<::chat::MeshProtocol>(ref.protocol);
        const ::chat::ChatMessage* message =
            has_protocol ? chat_service_.getMessageForProtocol(msg_id,
                                                               protocol)
                         : chat_service_.getMessage(msg_id);
        if (message == nullptr)
        {
            return ::chat::delivery::ChatDeliveryActionResult::fail(
                ::chat::delivery::ChatDeliveryActionFailure::NotFound);
        }
        if (message->status != ::chat::MessageStatus::Failed)
        {
            return ::chat::delivery::ChatDeliveryActionResult::fail(
                ::chat::delivery::ChatDeliveryActionFailure::NotRetryable);
        }

        const bool resent =
            has_protocol ? chat_service_.resendFailedForProtocol(msg_id,
                                                                 protocol)
                         : chat_service_.resendFailed(msg_id);
        if (!resent)
        {
            return ::chat::delivery::ChatDeliveryActionResult::fail(
                ::chat::delivery::ChatDeliveryActionFailure::Rejected);
        }
        return ::chat::delivery::ChatDeliveryActionResult::success();
    }

  private:
    static ::chat::MessageId messageIdFromRef(
        ::chat::delivery::ChatDeliveryRef ref)
    {
        if (ref.protocol_id != 0)
        {
            return ref.protocol_id;
        }
        if (ref.local_id > 0 &&
            ref.local_id <= std::numeric_limits<::chat::MessageId>::max())
        {
            return static_cast<::chat::MessageId>(ref.local_id);
        }
        return 0;
    }

    ::chat::ChatService& chat_service_;
};

const chat::ui::shell::Host* s_host = nullptr;
lv_obj_t* s_chat_container = nullptr;
std::unique_ptr<::ui::presentation_sources::ChatPresentationSource> s_chat_source = nullptr;
std::unique_ptr<::ui::presentation_sources::RuntimeChatActionSink> s_chat_sink = nullptr;
std::unique_ptr<::ui::chat::ChatWorkspaceModel> s_chat_model = nullptr;
std::unique_ptr<::chat::delivery::ChatDeliveryReadModel> s_delivery_read_model = nullptr;
std::unique_ptr<::chat::delivery::ChatDeliveryEventProjector> s_delivery_projector = nullptr;
std::unique_ptr<::chat::delivery::ProjectingChatDeliveryEventPort> s_delivery_event_port = nullptr;
std::unique_ptr<::ui_chat_runtime::ChatDeliveryEventProjectionAdapter> s_delivery_event_adapter = nullptr;
std::unique_ptr<ChatServiceRetryChatMessagePort> s_delivery_retry_port = nullptr;
std::unique_ptr<::chat::delivery::ChatDeliveryActionService> s_delivery_action_service = nullptr;
std::unique_ptr<::ui_chat_runtime::ChatDeliveryActionPortAdapter> s_delivery_action_adapter = nullptr;
std::unique_ptr<::ui_key_verification_runtime::KeyVerificationSessionAdapter> s_key_verification_session = nullptr;
std::unique_ptr<::ui_key_verification_runtime::KeyVerificationPresentationSource> s_key_verification_source = nullptr;
std::unique_ptr<::ui_key_verification_runtime::KeyVerificationActionSink> s_key_verification_sink = nullptr;
std::unique_ptr<::ui::key_verification::KeyVerificationModel> s_key_verification_model = nullptr;
std::unique_ptr<::ui::presentation_sources::TeamChatPresentationSource> s_team_chat_source = nullptr;
std::unique_ptr<::ui::presentation_sources::ITeamChatCommandPort> s_team_chat_command_port = nullptr;
std::unique_ptr<::ui::presentation_sources::TeamChatActionSink> s_team_chat_sink = nullptr;
std::unique_ptr<::ui::chat::ChatWorkspaceModel> s_team_chat_model = nullptr;
std::unique_ptr<::ui::team_actions::ITeamLocationSource> s_team_location_source = nullptr;
std::unique_ptr<::ui::team_actions::TeamActionRuntimeSink> s_team_action_sink = nullptr;
std::unique_ptr<chat::ui::ChatTeamWorkflow> s_team_workflow = nullptr;
std::unique_ptr<chat::ui::UiController> s_ui_controller = nullptr;
std::unique_ptr<chat::ui::ChatPageRuntimeEventPump> s_event_pump = nullptr;
std::unique_ptr<chat::ui::IChatUiRuntime> s_runtime_facade = nullptr;

struct PendingComposeRoute
{
    chat::ConversationId conversation{};
    bool pending = false;
    bool view_conversation = false;
};

PendingComposeRoute s_pending_compose_route{};

class ChatPageRuntimeFacade final : public chat::ui::IChatUiRuntime
{
  public:
    ChatPageRuntimeFacade(chat::ui::ChatPageRuntimeEventPump& event_pump,
                          chat::ui::UiController& controller)
        : event_pump_(event_pump), controller_(controller)
    {
    }

    void update() override
    {
        event_pump_.update();
        controller_.update();
    }

    void onChatEvent(sys::Event* event) override
    {
        event_pump_.handleEvent(event);
    }

    chat::ui::ChatUiState getState() const override
    {
        return controller_.getState();
    }

    bool isTeamConversationActive() const override
    {
        return controller_.isTeamConversationActive();
    }

  private:
    chat::ui::ChatPageRuntimeEventPump& event_pump_;
    chat::ui::UiController& controller_;
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

bool requestCompose(const chat::ConversationId& conversation)
{
    if (!is_available())
    {
        return false;
    }
    s_pending_compose_route.conversation = conversation;
    s_pending_compose_route.pending = true;
    s_pending_compose_route.view_conversation = false;
    return true;
}

bool requestConversation(const chat::ConversationId& conversation)
{
    if (!requestCompose(conversation)) return false;
    s_pending_compose_route.view_conversation = true;
    return true;
}

void clearRequestedCompose()
{
    s_pending_compose_route.pending = false;
    s_pending_compose_route.conversation = chat::ConversationId{};
}

void enter(const shell::Host* host, lv_obj_t* parent)
{
    if (!is_available())
    {
        s_host = host;
        if (s_chat_container && lv_obj_is_valid(s_chat_container))
        {
            lv_obj_del(s_chat_container);
        }

        s_chat_container = lv_obj_create(parent);
        lv_obj_set_size(s_chat_container, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(s_chat_container, lv_color_hex(0xFFF3DF), 0);
        lv_obj_set_style_bg_opa(s_chat_container, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_chat_container, 0, 0);
        lv_obj_set_style_pad_all(s_chat_container, 0, 0);
        lv_obj_set_style_radius(s_chat_container, 0, 0);

        lv_obj_t* label = lv_label_create(s_chat_container);
        lv_label_set_text(label, "Chat runtime unavailable");
        lv_obj_set_style_text_color(label, lv_color_hex(0x5B4632), 0);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
        return;
    }

    s_host = host;

    if (s_chat_container && lv_obj_is_valid(s_chat_container))
    {
        lv_obj_del(s_chat_container);
    }
    s_chat_container = nullptr;
    s_runtime_facade.reset();
    s_event_pump.reset();
    s_ui_controller.reset();
    s_team_workflow.reset();
    s_team_action_sink.reset();
    s_team_location_source.reset();
    s_team_chat_model.reset();
    s_team_chat_sink.reset();
    s_team_chat_command_port.reset();
    s_team_chat_source.reset();
    s_chat_model.reset();
    s_chat_sink.reset();
    s_chat_source.reset();
    s_key_verification_model.reset();
    s_key_verification_sink.reset();
    s_key_verification_source.reset();
    s_key_verification_session.reset();
    s_delivery_action_adapter.reset();
    s_delivery_action_service.reset();
    s_delivery_retry_port.reset();
    s_delivery_event_adapter.reset();
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

    chat::ChannelId default_channel = (app::configFacade().readConfig().chat_channel == 1)
                                          ? chat::ChannelId::SECONDARY
                                          : chat::ChannelId::PRIMARY;
    auto& chat_service = app::messagingFacade().getChatService();
    ::chat::delivery::ChatDeliveryReadModel* delivery_read_model =
        app::messagingFacade().getChatDeliveryReadModel();
    ::chat::delivery::IChatDeliveryEventPort* delivery_event_port =
        app::messagingFacade().getChatDeliveryEventPort();
    const bool using_facade_delivery =
        delivery_read_model != nullptr && delivery_event_port != nullptr;
    if (!using_facade_delivery)
    {
        s_delivery_read_model =
            std::unique_ptr<::chat::delivery::ChatDeliveryReadModel>(
                new ::chat::delivery::ChatDeliveryReadModel());
        s_delivery_projector =
            std::unique_ptr<::chat::delivery::ChatDeliveryEventProjector>(
                new ::chat::delivery::ChatDeliveryEventProjector(
                    *s_delivery_read_model));
        s_delivery_event_port =
            std::unique_ptr<::chat::delivery::ProjectingChatDeliveryEventPort>(
                new ::chat::delivery::ProjectingChatDeliveryEventPort(
                    *s_delivery_projector));
        delivery_read_model = s_delivery_read_model.get();
        delivery_event_port = s_delivery_event_port.get();
    }
    if (!using_facade_delivery)
    {
        s_delivery_event_adapter =
            std::unique_ptr<::ui_chat_runtime::ChatDeliveryEventProjectionAdapter>(
                new ::ui_chat_runtime::ChatDeliveryEventProjectionAdapter(
                    chat_service,
                    *delivery_event_port));
    }
    s_delivery_retry_port =
        std::unique_ptr<ChatServiceRetryChatMessagePort>(
            new ChatServiceRetryChatMessagePort(chat_service));
    s_delivery_action_service =
        std::unique_ptr<::chat::delivery::ChatDeliveryActionService>(
            new ::chat::delivery::ChatDeliveryActionService(
                *delivery_read_model,
                s_delivery_retry_port.get()));
    s_delivery_action_adapter =
        std::unique_ptr<::ui_chat_runtime::ChatDeliveryActionPortAdapter>(
            new ::ui_chat_runtime::ChatDeliveryActionPortAdapter(
                *s_delivery_action_service));
    s_chat_source = std::unique_ptr<::ui::presentation_sources::ChatPresentationSource>(
        new ::ui::presentation_sources::ChatPresentationSource(
            chat_service,
            &app::messagingFacade().getContactService(),
            delivery_read_model,
            app::messagingFacade().getMeshAdapter()));
    s_chat_sink = std::unique_ptr<::ui::presentation_sources::RuntimeChatActionSink>(
        new ::ui::presentation_sources::RuntimeChatActionSink(chat_service));
    s_chat_model = std::unique_ptr<::ui::chat::ChatWorkspaceModel>(
        new ::ui::chat::ChatWorkspaceModel(*s_chat_source, *s_chat_sink));
    s_key_verification_session =
        std::unique_ptr<::ui_key_verification_runtime::KeyVerificationSessionAdapter>(
            new ::ui_key_verification_runtime::KeyVerificationSessionAdapter());
    s_key_verification_source =
        std::unique_ptr<::ui_key_verification_runtime::KeyVerificationPresentationSource>(
            new ::ui_key_verification_runtime::KeyVerificationPresentationSource(
                *s_key_verification_session,
                &app::messagingFacade().getContactService(),
                app::messagingFacade().getMeshAdapter()));
    s_key_verification_sink =
        std::unique_ptr<::ui_key_verification_runtime::KeyVerificationActionSink>(
            new ::ui_key_verification_runtime::KeyVerificationActionSink(
                *s_key_verification_session,
                app::messagingFacade().getMeshAdapter(),
                &app::messagingFacade().getContactService()));
    s_key_verification_model =
        std::unique_ptr<::ui::key_verification::KeyVerificationModel>(
            new ::ui::key_verification::KeyVerificationModel(
                *s_key_verification_source,
                *s_key_verification_sink));
    s_team_chat_source =
        std::unique_ptr<::ui::presentation_sources::TeamChatPresentationSource>(
            new ::ui::presentation_sources::TeamChatPresentationSource(
                team::ui::team_ui_snapshot_store(),
                team::ui::team_ui_chat_log_store()));
    if (auto* team_controller = app::teamFacade().getTeamController())
    {
        s_team_chat_command_port =
            std::unique_ptr<::ui::presentation_sources::ITeamChatCommandPort>(
                new ::ui::team_actions::TeamControllerChatCommandPort(
                    *team_controller));
    }
    s_team_chat_sink =
        std::unique_ptr<::ui::presentation_sources::TeamChatActionSink>(
            new ::ui::presentation_sources::TeamChatActionSink(
                team::ui::team_ui_snapshot_store(),
                team::ui::team_ui_chat_log_store(),
                s_team_chat_command_port.get()));
    s_team_chat_model = std::unique_ptr<::ui::chat::ChatWorkspaceModel>(
        new ::ui::chat::ChatWorkspaceModel(*s_team_chat_source, *s_team_chat_sink));
    s_team_location_source =
        std::unique_ptr<::ui::team_actions::ITeamLocationSource>(
            new ::ui::team_actions::GpsTeamLocationSource());
    s_team_action_sink =
        std::unique_ptr<::ui::team_actions::TeamActionRuntimeSink>(
            new ::ui::team_actions::TeamActionRuntimeSink(
                team::ui::team_ui_snapshot_store(),
                team::ui::team_ui_chat_log_store(),
                s_team_chat_command_port.get(),
                s_team_location_source.get()));
    s_team_workflow = std::unique_ptr<chat::ui::ChatTeamWorkflow>(
        new chat::ui::ChatTeamWorkflow(*s_team_chat_model,
                                       s_team_action_sink.get()));

    s_ui_controller = std::unique_ptr<chat::ui::UiController>(
        new chat::ui::UiController(s_chat_container,
                                   chat_service,
                                   *s_chat_model,
                                   *s_team_workflow,
                                   s_key_verification_model.get(),
                                   s_delivery_action_adapter.get(),
                                   default_channel,
                                   request_shell_exit,
                                   nullptr));
    s_ui_controller->init();
    if (s_pending_compose_route.pending)
    {
        const chat::ConversationId requested = s_pending_compose_route.conversation;
        const bool view_conversation = s_pending_compose_route.view_conversation;
        s_pending_compose_route.pending = false;
        if (view_conversation) s_ui_controller->onChannelClicked(requested);
        const bool opened = view_conversation
                                ? s_ui_controller->getState() == chat::ui::ChatUiState::Conversation
                                : s_ui_controller->openComposeForConversation(requested);
        std::printf("[Chat][Route] %s target protocol=%u channel=%u peer=%08lX opened=%u\n",
                    view_conversation ? "conversation" : "compose",
                    static_cast<unsigned>(requested.protocol),
                    static_cast<unsigned>(requested.channel),
                    static_cast<unsigned long>(requested.peer),
                    opened ? 1U : 0U);
        if (!opened)
        {
            ::ui::feedback::show_notice("Chat destination unavailable", 2000);
        }
    }
    s_event_pump =
        std::unique_ptr<chat::ui::ChatPageRuntimeEventPump>(
            new chat::ui::ChatPageRuntimeEventPump(
                chat_service,
                s_delivery_event_adapter.get(),
                s_key_verification_source.get(),
                s_key_verification_model.get(),
                s_ui_controller.get()));
    s_runtime_facade =
        std::unique_ptr<chat::ui::IChatUiRuntime>(
            new ChatPageRuntimeFacade(*s_event_pump, *s_ui_controller));

    app::runtimeFacade().setChatUiRuntime(s_runtime_facade.get());
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
    s_runtime_facade.reset();
    s_event_pump.reset();
    s_ui_controller.reset();
    s_team_workflow.reset();
    s_team_action_sink.reset();
    s_team_location_source.reset();
    s_team_chat_model.reset();
    s_team_chat_sink.reset();
    s_team_chat_command_port.reset();
    s_team_chat_source.reset();
    s_chat_model.reset();
    s_chat_sink.reset();
    s_chat_source.reset();
    s_key_verification_model.reset();
    s_key_verification_sink.reset();
    s_key_verification_source.reset();
    s_key_verification_session.reset();
    s_delivery_action_adapter.reset();
    s_delivery_action_service.reset();
    s_delivery_retry_port.reset();
    s_delivery_event_adapter.reset();
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
