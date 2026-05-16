#pragma once

#include <string>

namespace trailmate::uconsole::gtk
{

struct LinuxMqttUiSettings
{
    bool enabled = false;
    std::string name = "Meshtastic CN";
    std::string host = "mqtt.mess.host";
    int port = 1883;
    std::string username = "meshdev";
    std::string password = "large4cats";
    std::string topic = "msh/CN/#";
    bool tls = false;
    std::string client_id{};
    bool clean_session = false;
    int qos = 1;
};

LinuxMqttUiSettings loadMqttSettings();
void saveMqttSettings(const LinuxMqttUiSettings& settings);

} // namespace trailmate::uconsole::gtk
