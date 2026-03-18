#include "platform/esp/arduino_common/app_runtime_support.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "app/app_facades.h"
#include "ble/ble_manager.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "platform/esp/arduino_common/app_tasks.h"
#include "platform/esp/arduino_common/device_identity.h"
#include "platform/esp/arduino_common/hostlink/hostlink_service.h"
#include "sys/event_bus.h"
#include "team/usecase/team_pairing_service.h"
#include "team/usecase/team_service.h"
#include "team/usecase/team_track_sampler.h"

namespace platform::esp::arduino_common
{

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
        app_context.getContactService().updateNodeInfo(
            node_event->node_id,
            node_event->short_name,
            node_event->long_name,
            node_event->snr,
            node_event->rssi,
            node_event->timestamp,
            node_event->protocol,
            node_event->role,
            node_event->hops_away,
            node_event->hw_model,
            node_event->channel);
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
