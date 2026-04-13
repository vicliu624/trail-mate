#include "platform/esp/arduino_common/app_event_runtime_support.h"

#include <cstdio>
#include <string>

#include "app/app_facades.h"
#include "board/BoardBase.h"
#include "chat/usecase/contact_service.h"
#include "platform/esp/arduino_common/app_runtime_support.h"
#include "platform/esp/arduino_common/hostlink/hostlink_bridge_radio.h"
#include "platform/ui/settings_store.h"
#include "sys/event_bus.h"
#include "team/protocol/team_chat.h"
#include "ui/chat_ui_runtime.h"
#include "ui/screens/team/team_page_shell.h"
#include "ui/widgets/system_notification.h"

namespace platform::esp::arduino_common
{
namespace
{

void triggerMessageFeedback(app::IAppFacade& app_context)
{
    BoardBase* board = app_context.getBoard();
    if (!board)
    {
        return;
    }
    if (platform::ui::settings_store::get_bool("settings", "vibration_enabled", true))
    {
        board->vibrator();
    }
    board->playMessageTone();
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
    triggerMessageFeedback(app_context);

    std::string notice = "Team: ";
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
            notice += "Location: " + loc.label;
        }
        else
        {
            notice += "Location";
        }
    }
    else if (msg.header.type == team::proto::TeamChatType::Command)
    {
        team::proto::TeamChatCommand cmd;
        if (team::proto::decodeTeamChatCommand(msg.payload.data(), msg.payload.size(), &cmd))
        {
            const char* name = "Command";
            switch (cmd.cmd_type)
            {
            case team::proto::TeamCommandType::RallyTo:
                name = "RallyTo";
                break;
            case team::proto::TeamCommandType::MoveTo:
                name = "MoveTo";
                break;
            case team::proto::TeamCommandType::Hold:
                name = "Hold";
                break;
            default:
                break;
            }
            notice += "Command: ";
            notice += name;
        }
        else
        {
            notice += "Command";
        }
    }
    else
    {
        notice += "Message";
    }

    ::ui::SystemNotification::show(notice.c_str(), 3000);
}

void tickUiRuntime(app::IAppFacade& app_context)
{
    platform::esp::arduino_common::tickRuntime(app_context);

    chat::ui::IChatUiRuntime* chat_ui_runtime = app_context.getChatUiRuntime();
    if (chat_ui_runtime)
    {
        chat_ui_runtime->update();
    }
}

bool handleUiEvent(app::IAppFacade& app_context, sys::Event* event)
{
    if (!event)
    {
        return true;
    }

    hostlink::bridge::on_event(*event);

    switch (event->type)
    {
    case sys::EventType::ChatNewMessage:
    {
        auto* msg_event = static_cast<sys::ChatNewMessageEvent*>(event);
        triggerMessageFeedback(app_context);
        ::ui::SystemNotification::show(msg_event->text, 3000);
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
        std::string msg = "Key verify: enter number for " + resolveContactName(app_context, kv_event->node_id);
        ::ui::SystemNotification::show(msg.c_str(), 4000);
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
        std::string msg = "Key verify: " + name + " " + number_buf;
        ::ui::SystemNotification::show(msg.c_str(), 5000);
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
        std::string msg = std::string("Key verify: ") + (kv_event->is_sender ? "send " : "confirm ") +
                          kv_event->verification_code + " " +
                          resolveContactName(app_context, kv_event->node_id);
        ::ui::SystemNotification::show(msg.c_str(), 5000);
        delete event;
        return true;
    }
    default:
        break;
    }

    if (event->type == sys::EventType::TeamKick ||
        event->type == sys::EventType::TeamTransferLeader ||
        event->type == sys::EventType::TeamKeyDist ||
        event->type == sys::EventType::TeamStatus ||
        event->type == sys::EventType::TeamPosition ||
        event->type == sys::EventType::TeamWaypoint ||
        event->type == sys::EventType::TeamTrack ||
        event->type == sys::EventType::TeamChat ||
        event->type == sys::EventType::TeamPairing ||
        event->type == sys::EventType::TeamError ||
        event->type == sys::EventType::SystemTick)
    {
        team::ui::shell::handle_event(nullptr, event);
        delete event;
        return true;
    }

    chat::ui::IChatUiRuntime* chat_ui_runtime = app_context.getChatUiRuntime();
    if (chat_ui_runtime)
    {
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
