#pragma once

#include "app/app_facade_access.h"
#include "chat/usecase/auto_reply_observer.h"
#include "platform/ui/auto_reply_settings.h"
#include "platform/ui/screen_runtime.h"
#include "platform/ui/settings_store.h"
#include <memory>

namespace platform::shared::chat
{
inline std::unique_ptr<::chat::ChatService::IncomingMessageObserver>
create_auto_reply_observer(::chat::ChatService& service)
{
    ::chat::auto_reply::ObserverHooks hooks;
    hooks.read_reply_text = [](std::string& text)
    {
        return ui::settings_store::get_bool(ui::auto_reply::kSettingsNamespace,
                                            ui::auto_reply::kEnabledKey, false) &&
               ui::settings_store::get_string(ui::auto_reply::kSettingsNamespace,
                                              ui::auto_reply::kTextKey, text);
    };
    hooks.screen_sleeping = ui::screen::is_sleeping;
    hooks.self_node = []()
    { return app::appFacade().getSelfNodeId(); };
    hooks.max_text_bytes = ui::auto_reply::kTextMaxBytes;
    return std::unique_ptr<::chat::ChatService::IncomingMessageObserver>(
        new ::chat::auto_reply::Observer(service, hooks));
}
} // namespace platform::shared::chat
