#include "platform/esp/arduino_common/chat/infra/auto_reply_observer.h"
#include "platform/shared/chat/auto_reply_observer.h"

namespace chat::infra
{
std::unique_ptr<chat::ChatService::IncomingMessageObserver>
create_auto_reply_observer(chat::ChatService& service)
{
    return platform::shared::chat::create_auto_reply_observer(service);
}
} // namespace chat::infra
