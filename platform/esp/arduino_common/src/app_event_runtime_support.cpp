#include "platform/esp/arduino_common/app_event_runtime_support.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "app/app_facades.h"
#include "app/app_system_tick.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "platform/esp/arduino_common/app_runtime_support.h"
#include "platform/esp/arduino_common/notification_runtime.h"
#include "sys/event_bus.h"
#include "team/protocol/team_chat.h"
#include "ui/chat_ui_runtime.h"
#include "ui/localization.h"
#include "ui/runtime/ui_feedback.h"
#include "ui/screens/team/team_page_shell.h"
#include "ui/widgets/reticulum_ping_overlay.h"
#include "ui/widgets/top_bar_power_presenter.h"
#include "ui_chat_runtime/chat_delivery_feedback_controller.h"

namespace platform::esp::arduino_common
{
namespace
{

bool messageAlertsEnabled()
{
    return notification::message_alerts_enabled();
}

bool isTeamRuntimeEvent(sys::EventType type)
{
    return type == sys::EventType::TeamKick ||
           type == sys::EventType::TeamTransferLeader ||
           type == sys::EventType::TeamKeyDist ||
           type == sys::EventType::TeamKeyRequest ||
           type == sys::EventType::TeamStatus ||
           type == sys::EventType::TeamPosition ||
           type == sys::EventType::TeamWaypoint ||
           type == sys::EventType::TeamTrack ||
           type == sys::EventType::TeamChat ||
           type == sys::EventType::TeamPairing ||
           type == sys::EventType::TeamError;
}

class UiFeedbackChatDeliveryFeedbackPort final
    : public ::ui_chat_runtime::IChatDeliveryFeedbackPort
{
  public:
    void showChatDeliverySent(chat::MessageId msg_id) override
    {
        (void)msg_id;
        ::ui::feedback::show_notice(::ui::i18n::tr("Sent"), 1400);
    }

    void showChatDeliveryFailed(chat::MessageId msg_id) override
    {
        (void)msg_id;
        ::ui::feedback::show_notice(::ui::i18n::tr("Send failed"), 2000);
    }
};

::ui_chat_runtime::ChatDeliveryFeedbackController& chatDeliveryFeedback()
{
    static UiFeedbackChatDeliveryFeedbackPort port;
    static ::ui_chat_runtime::ChatDeliveryFeedbackController controller(port);
    return controller;
}

void triggerMessageFeedback(app::IAppFacade& app_context)
{
    (void)notification::play_alert(app_context, notification::AlertKind::Message);
}

std::string resolveContactName(app::IAppFacade& app_context, chat::NodeId node_id)
{
    std::string name = app_context.getContactService().getContactName(node_id);
    if (!name.empty())
    {
        return name;
    }

    char fallback[16];
    snprintf(fallback, sizeof(fallback), "%08lX", static_cast<unsigned long>(node_id));
    return fallback;
}

void handleTeamChatNotification(app::IAppFacade& app_context, const sys::TeamChatEvent& team_event)
{
    if (!messageAlertsEnabled())
    {
        return;
    }
    triggerMessageFeedback(app_context);

    std::string notice = ::ui::i18n::tr("Team: ");
    const auto& msg = team_event.data.msg;
    if (msg.header.type == team::proto::TeamChatType::Text)
    {
        std::string text(msg.payload.begin(), msg.payload.end());
        if (text.size() > 48)
        {
            text = text.substr(0, 45) + "...";
        }
        notice += text;
    }
    else if (msg.header.type == team::proto::TeamChatType::Location)
    {
        team::proto::TeamChatLocation loc;
        if (team::proto::decodeTeamChatLocation(msg.payload.data(), msg.payload.size(), &loc) &&
            !loc.label.empty())
        {
            notice += ::ui::i18n::format("Location: %s", loc.label.c_str());
        }
        else
        {
            notice += ::ui::i18n::tr("Location");
        }
    }
    else if (msg.header.type == team::proto::TeamChatType::Command)
    {
        team::proto::TeamChatCommand cmd;
        if (team::proto::decodeTeamChatCommand(msg.payload.data(), msg.payload.size(), &cmd))
        {
            const char* name = ::ui::i18n::tr("Command");
            switch (cmd.cmd_type)
            {
            case team::proto::TeamCommandType::RallyTo:
                name = ::ui::i18n::tr("RallyTo");
                break;
            case team::proto::TeamCommandType::MoveTo:
                name = ::ui::i18n::tr("MoveTo");
                break;
            case team::proto::TeamCommandType::Hold:
                name = ::ui::i18n::tr("Hold");
                break;
            default:
                break;
            }
            notice += ::ui::i18n::format("Command: %s", name);
        }
        else
        {
            notice += ::ui::i18n::tr("Command");
        }
    }
    else
    {
        notice += ::ui::i18n::tr("Message");
    }

    ::ui::feedback::show_notice(notice.c_str(), 3000);
}

void handleChatSendResultFeedback(app::IAppFacade& app_context,
                                  const sys::ChatSendResultEvent& event)
{
    const chat::ChatMessage* message =
        event.has_protocol
            ? app_context.getChatService().getMessageForProtocol(
                  event.msg_id,
                  event.protocol)
            : app_context.getChatService().getMessage(event.msg_id);
    chatDeliveryFeedback().onChatSendResult(
        event.msg_id,
        event.success,
        message);
}

void tickUiRuntime(app::IAppFacade& app_context)
{
    app::publishSystemTick(sys::millis_now());
    platform::esp::arduino_common::tickRuntime(app_context);

    chat::ui::IChatUiRuntime* chat_ui_runtime = app_context.getChatUiRuntime();
    if (chat_ui_runtime)
    {
        chat_ui_runtime->update();
    }

    ::ui::widgets::top_bar_power::tick();
}

bool handleUiEvent(app::IAppFacade& app_context, sys::Event* event)
{
    if (!event)
    {
        return true;
    }

    switch (event->type)
    {
    case sys::EventType::ReticulumPingResult:
    {
        const auto* ping_event =
            static_cast<sys::ReticulumPingResultEvent*>(event);
        if (ping_event->result == sys::ReticulumPingResult::Delivered)
        {
            ::ui::widgets::reticulum_ping::show_delivered(
                ping_event->destination_hash,
                ping_event->elapsed_ms,
                ping_event->hops);
        }
        else
        {
            ::ui::widgets::reticulum_ping::show_timeout(
                ping_event->destination_hash,
                ping_event->elapsed_ms);
        }
        delete event;
        return true;
    }
    case sys::EventType::ChatSendResult:
        handleChatSendResultFeedback(
            app_context,
            *static_cast<sys::ChatSendResultEvent*>(event));
        break;
    case sys::EventType::ChatNewMessage:
    {
        auto* msg_event = static_cast<sys::ChatNewMessageEvent*>(event);
        const bool voice_message =
            msg_event->content_kind == sys::ChatMessageContentKind::Voice;
        const char* const notice = voice_message
                                       ? ::ui::i18n::tr("Voice message")
                                       : msg_event->text;
        const bool alerts_enabled = messageAlertsEnabled();
        std::printf("[UI][event] chat_new_message kind=%s msg=%lu ch=%u len=%u origin=%u alerts=%u text='%s'\n",
                    voice_message ? "voice" : "text",
                    static_cast<unsigned long>(msg_event->msg_id),
                    static_cast<unsigned>(msg_event->channel),
                    static_cast<unsigned>(std::strlen(msg_event->text)),
                    static_cast<unsigned>(msg_event->rx_meta.origin),
                    alerts_enabled ? 1U : 0U,
                    msg_event->text);
        if (alerts_enabled)
        {
            triggerMessageFeedback(app_context);
            ::ui::feedback::show_notice(notice, 3000);
            std::printf("[UI][notice] chat_new_message kind=%s msg=%lu shown=1\n",
                        voice_message ? "voice" : "text",
                        static_cast<unsigned long>(msg_event->msg_id));
        }
        else
        {
            std::printf("[UI][notice] chat_new_message kind=%s msg=%lu shown=0 reason=alerts_disabled\n",
                        voice_message ? "voice" : "text",
                        static_cast<unsigned long>(msg_event->msg_id));
        }
        break;
    }
    case sys::EventType::TeamChat:
        handleTeamChatNotification(app_context, *static_cast<sys::TeamChatEvent*>(event));
        break;
    case sys::EventType::KeyVerificationNumberRequest:
    {
        chat::ui::IChatUiRuntime* chat_ui_runtime = app_context.getChatUiRuntime();
        if (chat_ui_runtime)
        {
            chat_ui_runtime->onChatEvent(event);
            return true;
        }
        auto* kv_event = static_cast<sys::KeyVerificationNumberRequestEvent*>(event);
        const std::string msg =
            ::ui::i18n::format("Key verify: enter number for %s",
                               resolveContactName(app_context, kv_event->node_id).c_str());
        ::ui::feedback::show_notice(msg.c_str(), 4000);
        delete event;
        return true;
    }
    case sys::EventType::KeyVerificationNumberInform:
    {
        chat::ui::IChatUiRuntime* chat_ui_runtime = app_context.getChatUiRuntime();
        if (chat_ui_runtime)
        {
            chat_ui_runtime->onChatEvent(event);
            return true;
        }
        auto* kv_event = static_cast<sys::KeyVerificationNumberInformEvent*>(event);
        const std::string name = resolveContactName(app_context, kv_event->node_id);
        const uint32_t number = kv_event->security_number % 1000000;
        char number_buf[16];
        snprintf(number_buf, sizeof(number_buf), "%03u %03u", number / 1000, number % 1000);
        const std::string msg = ::ui::i18n::format("Key verify: %s %s", name.c_str(), number_buf);
        ::ui::feedback::show_notice(msg.c_str(), 5000);
        delete event;
        return true;
    }
    case sys::EventType::KeyVerificationFinal:
    {
        chat::ui::IChatUiRuntime* chat_ui_runtime = app_context.getChatUiRuntime();
        if (chat_ui_runtime)
        {
            chat_ui_runtime->onChatEvent(event);
            return true;
        }
        auto* kv_event = static_cast<sys::KeyVerificationFinalEvent*>(event);
        const std::string msg = kv_event->is_sender
                                    ? ::ui::i18n::format("Key verify: send %s %s",
                                                         kv_event->verification_code,
                                                         resolveContactName(app_context, kv_event->node_id).c_str())
                                    : ::ui::i18n::format("Key verify: confirm %s %s",
                                                         kv_event->verification_code,
                                                         resolveContactName(app_context, kv_event->node_id).c_str());
        ::ui::feedback::show_notice(msg.c_str(), 5000);
        delete event;
        return true;
    }
    default:
        break;
    }

    const bool team_runtime_event = isTeamRuntimeEvent(event->type);
    if (team_runtime_event || event->type == sys::EventType::SystemTick)
    {
        team::ui::shell::handle_event(nullptr, event);
        delete event;
        return true;
    }

    chat::ui::IChatUiRuntime* chat_ui_runtime = app_context.getChatUiRuntime();
    if (chat_ui_runtime)
    {
        if (event->type == sys::EventType::ChatNewMessage)
        {
            auto* msg_event = static_cast<sys::ChatNewMessageEvent*>(event);
            std::printf("[UI][chat_runtime] dispatch chat_new_message kind=%u msg=%lu ch=%u\n",
                        static_cast<unsigned>(msg_event->content_kind),
                        static_cast<unsigned long>(msg_event->msg_id),
                        static_cast<unsigned>(msg_event->channel));
        }
        chat_ui_runtime->onChatEvent(event);
        return true;
    }

    delete event;
    return true;
}

} // namespace

app::AppEventRuntimeHooks makeAppEventRuntimeHooks()
{
    app::AppEventRuntimeHooks hooks{};
    hooks.update_core_services = platform::esp::arduino_common::updateCoreServices;
    hooks.tick = tickUiRuntime;
    hooks.dispatch_event = platform::esp::arduino_common::dispatchEvent;
    hooks.handle_event = handleUiEvent;
    return hooks;
}

} // namespace platform::esp::arduino_common
