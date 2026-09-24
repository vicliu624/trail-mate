#include "ui/screens/settings/settings_spec.h"
#include <cassert>

int main()
{
    using namespace settings::ui;
    const SettingId fields[] = {SettingId::RtTcpSlot, SettingId::RtTcpDefaults,
                                SettingId::RtWifiHost, SettingId::RtWifiPort};
    for (auto protocol : {chat::MeshProtocol::Meshtastic, chat::MeshProtocol::MeshCore,
                          chat::MeshProtocol::Reticulum})
    {
        for (bool bearer_ip : {false, true})
        {
            spec::VisibilityContext context{};
            context.protocol = protocol;
            context.reticulum_wifi_visible = bearer_ip;
            context.wifi_supported = true;
            for (auto field : fields)
            {
                assert(spec::should_show(field, context));
                assert(spec::id_for_key(spec::key_for_id(field)) == field);
            }
            context.wifi_supported = false;
            for (auto field : fields) assert(!spec::should_show(field, context));
        }
    }
}
