#include "platform/gtk/gtk_uconsole_mqtt_settings.h"

#include <algorithm>
#include <string>

#include "platform/ui/settings_store.h"

namespace trailmate::uconsole::gtk
{

constexpr const char* kMqttSettingsNamespace = "uconsole_mqtt";
constexpr const char* kMqttEnabledKey = "enabled";
constexpr const char* kMqttNameKey = "name";
constexpr const char* kMqttHostKey = "host";
constexpr const char* kMqttPortKey = "port";
constexpr const char* kMqttUsernameKey = "username";
constexpr const char* kMqttPasswordKey = "password";
constexpr const char* kMqttTopicKey = "topic";
constexpr const char* kMqttTlsKey = "tls";
constexpr const char* kMqttClientIdKey = "client_id";
constexpr const char* kMqttCleanSessionKey = "clean_session";
constexpr const char* kMqttQosKey = "qos";
std::string storedString(const char* key, const char* fallback)
{
    std::string value{};
    if (::platform::ui::settings_store::get_string(
            kMqttSettingsNamespace, key, value))
    {
        return value;
    }
    return fallback ? std::string(fallback) : std::string();
}

LinuxMqttUiSettings loadMqttSettings()
{
    LinuxMqttUiSettings settings{};
    settings.enabled = ::platform::ui::settings_store::get_bool(
        kMqttSettingsNamespace, kMqttEnabledKey, settings.enabled);
    settings.name = storedString(kMqttNameKey, settings.name.c_str());
    settings.host = storedString(kMqttHostKey, settings.host.c_str());
    settings.port = std::clamp(::platform::ui::settings_store::get_int(
                                   kMqttSettingsNamespace,
                                   kMqttPortKey,
                                   settings.port),
                               1,
                               65535);
    settings.username =
        storedString(kMqttUsernameKey, settings.username.c_str());
    settings.password =
        storedString(kMqttPasswordKey, settings.password.c_str());
    settings.topic = storedString(kMqttTopicKey, settings.topic.c_str());
    settings.tls = ::platform::ui::settings_store::get_bool(
        kMqttSettingsNamespace, kMqttTlsKey, settings.tls);
    settings.client_id =
        storedString(kMqttClientIdKey, settings.client_id.c_str());
    settings.clean_session = ::platform::ui::settings_store::get_bool(
        kMqttSettingsNamespace,
        kMqttCleanSessionKey,
        settings.clean_session);
    settings.qos = std::clamp(::platform::ui::settings_store::get_int(
                                  kMqttSettingsNamespace,
                                  kMqttQosKey,
                                  settings.qos),
                              0,
                              2);
    return settings;
}

void saveMqttSettings(const LinuxMqttUiSettings& settings)
{
    ::platform::ui::settings_store::put_bool(
        kMqttSettingsNamespace, kMqttEnabledKey, settings.enabled);
    ::platform::ui::settings_store::put_string(
        kMqttSettingsNamespace, kMqttNameKey, settings.name.c_str());
    ::platform::ui::settings_store::put_string(
        kMqttSettingsNamespace, kMqttHostKey, settings.host.c_str());
    ::platform::ui::settings_store::put_int(
        kMqttSettingsNamespace, kMqttPortKey,
        std::clamp(settings.port, 1, 65535));
    ::platform::ui::settings_store::put_string(
        kMqttSettingsNamespace, kMqttUsernameKey, settings.username.c_str());
    ::platform::ui::settings_store::put_string(
        kMqttSettingsNamespace, kMqttPasswordKey, settings.password.c_str());
    ::platform::ui::settings_store::put_string(
        kMqttSettingsNamespace, kMqttTopicKey, settings.topic.c_str());
    ::platform::ui::settings_store::put_bool(
        kMqttSettingsNamespace, kMqttTlsKey, settings.tls);
    ::platform::ui::settings_store::put_string(
        kMqttSettingsNamespace, kMqttClientIdKey, settings.client_id.c_str());
    ::platform::ui::settings_store::put_bool(
        kMqttSettingsNamespace,
        kMqttCleanSessionKey,
        settings.clean_session);
    ::platform::ui::settings_store::put_int(
        kMqttSettingsNamespace, kMqttQosKey, std::clamp(settings.qos, 0, 2));
}

} // namespace trailmate::uconsole::gtk
