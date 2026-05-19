#include "ui_legacy_adapters/legacy_chat_delivery_action_bridge.h"

#include <type_traits>

int main()
{
    static_assert(std::is_same<
                      ui::presentation_sources::LegacyChatDeliveryActionBridge,
                      ui_chat_runtime::ChatDeliveryActionPortAdapter>::value,
                  "LegacyChatDeliveryActionBridge must stay an alias only");
    return 0;
}
