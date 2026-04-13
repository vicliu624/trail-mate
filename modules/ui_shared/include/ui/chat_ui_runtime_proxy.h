#pragma once

#include "ui/chat_ui_runtime.h"

namespace chat::ui
{

class GlobalChatUiRuntime final : public IChatUiRuntime
{
  public:
    GlobalChatUiRuntime();
    ~GlobalChatUiRuntime() override;

    void setActiveRuntime(IChatUiRuntime* runtime);
    IChatUiRuntime* getActiveRuntime() const;

    void update() override;
    void onChatEvent(sys::Event* event) override;
    ChatUiState getState() const override;
    bool isTeamConversationActive() const override;

  private:
    IChatUiRuntime* active_runtime_ = nullptr;
};

} // namespace chat::ui
