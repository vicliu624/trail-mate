#include "platform/esp/arduino_common/app_runtime_support.h"

#include <cstdio>
#include <string>

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "app/app_facades.h"
#include "ble/ble_manager.h"
#include "board/BoardBase.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "platform/esp/arduino_common/app_tasks.h"
#include "platform/esp/arduino_common/device_identity.h"
#include "platform/esp/arduino_common/hostlink/hostlink_service.h"
#include "platform/ui/settings_store.h"
#include "sys/event_bus.h"
#include "team/usecase/team_pairing_service.h"
#include "team/usecase/team_service.h"
#include "team/usecase/team_track_sampler.h"
#include "ui/localization.h"
#include "ui/widgets/system_notification.h"

namespace platform::esp::arduino_common
{
namespace
{

constexpr const char* kSettingsNs = "settings";
constexpr const char* kContactAlertsKey = "chat_contact_alerts";
constexpr int kContactAlertsNone = 0;
constexpr int kContactAlertsContacts = 1;
constexpr int kContactAlertsAll = 2;

void triggerNodeInfoFeedback(app::IAppFacade& app_context)
{
    BoardBase* board = app_context.getBoard();
    if (!board)
    {
        return;
    }

    if (platform::ui::settings_store::get_bool(kSettingsNs, "vibration_enabled", true))
    {
        board->vibrator();
    }

    board->playMessageTone();
}

std::string resolveNodeInfoName(app::IAppFacade& app_context, const sys::NodeInfoUpdateEvent& node_event)
{
    std::string name = app_context.getContactService().getContactName(node_event.node_id);
    if (!name.empty())
    {
        return name;
    }

    if (node_event.short_name[0] != '\0')
    {
        return std::string(node_event.short_name);
    }

    if (node_event.long_name[0] != '\0')
    {
        return std::string(node_event.long_name);
    }

    char fallback[16];
    std::snprintf(fallback, sizeof(fallback), "%08lX", static_cast<unsigned long>(node_event.node_id));
    return fallback;
}

void notifyNodeInfoUpdate(app::IAppFacade& app_context, const sys::NodeInfoUpdateEvent& node_event)
{
    if (node_event.node_id == 0 || node_event.node_id == app_context.getSelfNodeId() || node_event.is_ignored)
    {
        return;
    }

    int alert_mode = platform::ui::settings_store::get_int(kSettingsNs,
                                                           kContactAlertsKey,
                                                           kContactAlertsContacts);
    if (alert_mode < kContactAlertsNone || alert_mode > kContactAlertsAll)
    {
        alert_mode = kContactAlertsContacts;
    }
    if (alert_mode == kContactAlertsNone)
    {
        return;
    }

    const chat::contacts::NodeInfo* node_info =
        app_context.getContactService().getNodeInfo(node_event.node_id);
    const bool is_contact = node_info && node_info->is_contact;
    if (alert_mode == kContactAlertsContacts && !is_contact)
    {
        return;
    }

    triggerNodeInfoFeedback(app_context);

    const std::string message =
        ::ui::i18n::format("Node info: %s", resolveNodeInfoName(app_context, node_event).c_str());
    ::ui::SystemNotification::show(message.c_str(), 3000);
}

} // namespace

BackgroundTaskStartResult startBackgroundTasks(LoraBoard* board, chat::IMeshAdapter* adapter)
{
    if (!board)
    {
        return BackgroundTaskStartResult::NotSupported;
    }

    if (!app::AppTasks::init(*board, adapter))
    {
        return BackgroundTaskStartResult::Failed;
    }

    return BackgroundTaskStartResult::Started;
}

void tickBoundLifecycle(std::size_t max_events)
{
    app::IAppLifecycleFacade& lifecycle = app::lifecycleFacade();
    lifecycle.updateCoreServices();
    lifecycle.tickEventRuntime();
    lifecycle.dispatchPendingEvents(max_events);
}

void tickRuntime(app::IAppFacade& app_context)
{
    ble::BleManager* ble_manager = app_context.getBleManager();
    if (ble_manager)
    {
        ble_manager->update();
    }
}

void updateCoreServices(app::IAppFacade& app_context)
{
    hostlink::process_pending_commands();

    app_context.getChatService().processIncoming();
    app_context.getChatService().flushStore();

    team::TeamService* team_service = app_context.getTeamService();
    if (team_service)
    {
        team_service->processIncoming();
    }

    team::TeamPairingService* team_pairing = app_context.getTeamPairing();
    if (team_pairing)
    {
        team_pairing->update();
    }

    const bool team_active = team_service && team_service->hasKeys();
    app_context.setTeamModeActive(team_active);

    team::TeamTrackSampler* team_track_sampler = app_context.getTeamTrackSampler();
    if (team_track_sampler)
    {
        team_track_sampler->update(app_context.getTeamController(), team_active);
    }
}

bool dispatchEvent(app::IAppFacade& app_context, sys::Event* event)
{
    if (!event)
    {
        return true;
    }

    switch (event->type)
    {
    case sys::EventType::ChatSendResult:
    {
        auto* result_event = static_cast<sys::ChatSendResultEvent*>(event);
        app_context.getChatService().handleSendResult(result_event->msg_id, result_event->success);
        return false;
    }
    case sys::EventType::NodeInfoUpdate:
    {
        auto* node_event = static_cast<sys::NodeInfoUpdateEvent*>(event);
        chat::contacts::NodeUpdate update{};
        update.short_name = node_event->short_name;
        update.long_name = node_event->long_name;
        update.has_last_seen = true;
        update.last_seen = node_event->timestamp;
        update.has_snr = true;
        update.snr = node_event->snr;
        update.has_rssi = true;
        update.rssi = node_event->rssi;
        update.has_protocol = true;
        update.protocol = node_event->protocol;
        update.has_role = true;
        update.role = node_event->role;
        update.has_hops_away = true;
        update.hops_away = node_event->hops_away;
        update.has_hw_model = true;
        update.hw_model = node_event->hw_model;
        update.has_channel = true;
        update.channel = node_event->channel;
        update.has_macaddr = node_event->has_macaddr;
        if (node_event->has_macaddr)
        {
            memcpy(update.macaddr, node_event->macaddr, sizeof(update.macaddr));
        }
        update.has_via_mqtt = true;
        update.via_mqtt = node_event->via_mqtt;
        update.has_is_ignored = true;
        update.is_ignored = node_event->is_ignored;
        update.has_public_key = true;
        update.public_key_present = node_event->has_public_key;
        update.has_key_manually_verified = true;
        update.key_manually_verified = node_event->key_manually_verified;
        update.has_device_metrics = node_event->has_device_metrics;
        if (node_event->has_device_metrics)
        {
            update.device_metrics = node_event->device_metrics;
        }
        app_context.getContactService().applyNodeUpdate(node_event->node_id, update);
        notifyNodeInfoUpdate(app_context, *node_event);
        delete event;
        return true;
    }
    case sys::EventType::NodeProtocolUpdate:
    {
        auto* node_event = static_cast<sys::NodeProtocolUpdateEvent*>(event);
        app_context.getContactService().updateNodeProtocol(
            node_event->node_id,
            node_event->protocol,
            node_event->timestamp);
        delete event;
        return true;
    }
    case sys::EventType::NodePositionUpdate:
    {
        auto* pos_event = static_cast<sys::NodePositionUpdateEvent*>(event);
        chat::contacts::NodePosition pos{};
        pos.valid = true;
        pos.latitude_i = pos_event->latitude_i;
        pos.longitude_i = pos_event->longitude_i;
        pos.has_altitude = pos_event->has_altitude;
        pos.altitude = pos_event->altitude;
        pos.timestamp = pos_event->timestamp;
        pos.precision_bits = pos_event->precision_bits;
        pos.pdop = pos_event->pdop;
        pos.hdop = pos_event->hdop;
        pos.vdop = pos_event->vdop;
        pos.gps_accuracy_mm = pos_event->gps_accuracy_mm;
        app_context.getContactService().updateNodePosition(pos_event->node_id, pos);
        delete event;
        return true;
    }
    default:
        return false;
    }
}

std::unique_ptr<ble::BleManager> createBleManager(app::IAppBleFacade& app_facade)
{
    std::unique_ptr<ble::BleManager> ble_manager(new ble::BleManager(app_facade));
    if (app_facade.getConfig().ble_enabled)
    {
        ble_manager->setEnabled(true);
    }
    return ble_manager;
}

} // namespace platform::esp::arduino_common
