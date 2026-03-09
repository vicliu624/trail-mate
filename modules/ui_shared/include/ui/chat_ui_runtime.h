#pragma once

namespace sys
{
struct Event;
}

namespace chat::ui
{

enum class ChatUiState
{
    ChannelList,
    Conversation,
    Compose,
};

class IChatUiRuntime
{
  public:
    virtual ~IChatUiRuntime() = default;

    virtual void update() = 0;
    virtual void onChatEvent(sys::Event* event) = 0;
    virtual ChatUiState getState() const = 0;
    virtual bool isTeamConversationActive() const = 0;
};

} // namespace chat::ui
