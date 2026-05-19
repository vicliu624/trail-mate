#include "ui_legacy_adapters/legacy_chat_delivery_event_bridge.h"

#include <type_traits>

int main()
{
    static_assert(std::is_same<
                      ui::presentation_sources::LegacyChatDeliveryEventBridge,
                      ui_chat_runtime::ChatDeliveryEventProjectionAdapter>::value,
                  "LegacyChatDeliveryEventBridge must stay an alias only");
    return 0;
}
