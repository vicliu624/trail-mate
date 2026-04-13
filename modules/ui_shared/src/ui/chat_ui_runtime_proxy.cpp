#include "ui/chat_ui_runtime_proxy.h"

#include "sys/event_bus.h"

namespace chat::ui
{
namespace
{
bool isKeyVerificationEvent(sys::EventType type)
{
    return type == sys::EventType::KeyVerificationNumberRequest ||
           type == sys::EventType::KeyVerificationNumberInform ||
           type == sys::EventType::KeyVerificationFinal;
}
} // namespace

GlobalChatUiRuntime::GlobalChatUiRuntime() = default;

GlobalChatUiRuntime::~GlobalChatUiRuntime() = default;

void GlobalChatUiRuntime::setActiveRuntime(IChatUiRuntime* runtime)
{
    active_runtime_ = runtime;
}

IChatUiRuntime* GlobalChatUiRuntime::getActiveRuntime() const
{
    return active_runtime_;
}

void GlobalChatUiRuntime::update()
{
    if (active_runtime_)
    {
        active_runtime_->update();
    }
}

void GlobalChatUiRuntime::onChatEvent(sys::Event* event)
{
    if (!event)
    {
        return;
    }

    if (active_runtime_)
    {
        active_runtime_->onChatEvent(event);
        return;
    }

    if (isKeyVerificationEvent(event->type))
    {
        delete event;
        return;
    }

    delete event;
}

ChatUiState GlobalChatUiRuntime::getState() const
{
    return active_runtime_ ? active_runtime_->getState() : ChatUiState::ChannelList;
}

bool GlobalChatUiRuntime::isTeamConversationActive() const
{
    return active_runtime_ ? active_runtime_->isTeamConversationActive() : false;
}

} // namespace chat::ui
