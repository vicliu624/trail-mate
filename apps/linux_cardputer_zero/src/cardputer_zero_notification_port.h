#pragma once

#include <cstdint>

namespace trailmate
{
namespace apps
{
namespace linux_cardputer_zero
{

enum class NotificationUrgency : std::uint8_t
{
    Low = 0,
    Normal = 1,
    Critical = 2,
};

struct NotificationRequest
{
    const char* app_name = "TrailMate";
    const char* summary = "";
    const char* body = "";
    const char* app_icon = "";
    std::uint32_t replaces_id = 0;
    NotificationUrgency urgency = NotificationUrgency::Normal;
    int expire_timeout_ms = -1;
};

struct FreedesktopNotificationsContract
{
    const char* bus_name;
    const char* object_path;
    const char* interface_name;
    const char* notify_method;
    const char* notify_signature;
    const char* notify_return_signature;
    const char* close_method;
    const char* get_capabilities_method;
    const char* get_server_information_method;
    const char* notification_closed_signal;
    const char* action_invoked_signal;
    const char* capability_body;
    const char* capability_actions;
    const char* capability_persistence;
    const char* hint_urgency;
    const char* hint_desktop_entry;
    const char* hint_transient;
    const char* hint_category;
    const char* hint_resident;
    const char* shell_ipc_socket_suffix;
    bool shell_ipc_creates_notifications;
};

struct FreedesktopNotifyCall
{
    const FreedesktopNotificationsContract* contract = nullptr;
    const char* app_name = "";
    std::uint32_t replaces_id = 0;
    const char* app_icon = "";
    const char* summary = "";
    const char* body = "";
    std::uint8_t urgency_hint = 1;
    int expire_timeout_ms = -1;
};

class CardputerZeroNotificationPort
{
  public:
    const FreedesktopNotificationsContract& contract() const;
    FreedesktopNotifyCall makeNotifyCall(const NotificationRequest& request) const;
    bool validate() const;
};

} // namespace linux_cardputer_zero
} // namespace apps
} // namespace trailmate
