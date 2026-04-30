#include "platform/esp/arduino_common/app_context_platform_bindings.h"
#include "platform/esp/arduino_common/app_config_store.h"

#include <Arduino.h>
#include <SD.h>

#include "app/app_facades.h"
#include "board/GpsBoard.h"
#include "board/MotionBoard.h"
#include "chat/infra/store/ram_store.h"
#include "chat/usecase/contact_service.h"
#include "platform/esp/arduino_common/chat/infra/chat_event_bus_bridge.h"
#include "platform/esp/arduino_common/chat/infra/contact_store.h"
#include "platform/esp/arduino_common/chat/infra/mesh_adapter_router.h"
#include "platform/esp/arduino_common/chat/infra/meshtastic/node_store.h"
#include "platform/esp/arduino_common/chat/infra/protocol_factory.h"
#include "platform/esp/arduino_common/chat/infra/store/log_store.h"
#include "platform/esp/arduino_common/device_identity.h"
#include "platform/esp/arduino_common/gps/gps_service.h"
#include "platform/esp/arduino_common/gps/track_recorder.h"
#include "platform/esp/arduino_common/team/crypto/team_crypto.h"
#include "platform/esp/arduino_common/team/event/team_app_data_event_bus_bridge.h"
#include "platform/esp/arduino_common/team/event/team_event_bus_sink.h"
#include "platform/esp/arduino_common/team/event/team_pairing_event_bus_sink.h"
#include "platform/esp/arduino_common/team_platform_bundle.h"
#include "team/usecase/team_controller.h"
#include "team/usecase/team_track_sampler.h"
#include "platform/ui/team_ui_store_runtime.h"
#include "ui/ui_common.h"

namespace
{

std::unique_ptr<chat::IMeshAdapter> create_mesh_runtime()
{
    return std::unique_ptr<chat::IMeshAdapter>(new chat::MeshAdapterRouter());
}

void init_gps_runtime(GpsBoard* gps_board,
                      MotionBoard* motion_board,
                      uint32_t disable_hw_init,
                      const app::AppConfig& config)
{
    if (!gps_board || !motion_board)
    {
        return;
    }

    auto& gps_service = gps::GpsService::getInstance();
    gps_service.begin(*gps_board,
                      *motion_board,
                      disable_hw_init,
                      config.gps_interval_ms,
                      config.motion_config);
    gps_service.setEnabled(config.gps_enabled);
    gps_service.setCollectionInterval(config.gps_interval_ms);
    gps_service.setPowerStrategy(config.gps_strategy);
    gps_service.setGnssConfig(config.gps_mode, config.gps_sat_mask);
    gps_service.setExternalNmeaConfig(config.external_nmea_output_hz, config.external_nmea_sentence_mask);
}

void apply_position_config(const app::AppConfig& config)
{
    gps::GpsService::getInstance().setEnabled(config.gps_enabled);
    gps::GpsService::getInstance().setCollectionInterval(config.gps_interval_ms);
    gps::GpsService::getInstance().setPowerStrategy(config.gps_strategy);
    gps::GpsService::getInstance().setGnssConfig(config.gps_mode, config.gps_sat_mask);
    gps::GpsService::getInstance().setExternalNmeaConfig(config.external_nmea_output_hz,
                                                         config.external_nmea_sentence_mask);
}

void init_track_recorder(const app::AppConfig& config)
{
    auto& recorder = gps::TrackRecorder::getInstance();
    recorder.setFormat(static_cast<gps::TrackFormat>(config.map_track_format));
    if (config.map_track_interval == 99)
    {
        recorder.setDistanceOnly(true);
        recorder.setIntervalSeconds(0);
    }
    else
    {
        recorder.setDistanceOnly(false);
        recorder.setIntervalSeconds(static_cast<uint32_t>(config.map_track_interval));
    }
    if (recorder.restoreActiveSession())
    {
        Serial.printf("[Tracker] active session restored path=%s\n",
                      recorder.currentPath().c_str());
    }
    recorder.setAutoRecording(config.map_track_enabled);
}

void set_team_mode_active(bool active)
{
    gps::GpsService::getInstance().setTeamModeActive(active);
}

std::unique_ptr<chat::IChatStore> create_chat_store()
{
    const bool sd_available = (SD.cardType() != CARD_NONE);
    if (sd_available)
    {
        auto log_store = std::unique_ptr<chat::LogStore>(new chat::LogStore());
        if (log_store->begin(SD))
        {
            Serial.printf("[AppContext] chat store=LogStore (SD)\n");
            return log_store;
        }
    }

    Serial.printf("[AppContext] chat store=RamStore\n");
    return std::unique_ptr<chat::IChatStore>(new chat::RamStore());
}

std::unique_ptr<chat::IMeshAdapter> create_mesh_backend(chat::MeshProtocol protocol,
                                                        LoraBoard& lora_board)
{
    return chat::ProtocolFactory::createAdapter(protocol, lora_board);
}

app::ContactServicesBundle create_contact_services()
{
    app::ContactServicesBundle bundle;
    bundle.node_store = std::unique_ptr<chat::contacts::INodeStore>(new chat::meshtastic::NodeStore());
    bundle.contact_store = std::unique_ptr<chat::contacts::IContactStore>(new chat::contacts::ContactStore());
    if (!bundle.node_store || !bundle.contact_store)
    {
        return bundle;
    }

    bundle.service = std::unique_ptr<chat::contacts::ContactService>(
        new chat::contacts::ContactService(*bundle.node_store, *bundle.contact_store));
    if (bundle.service)
    {
        bundle.service->begin();
        const std::vector<chat::contacts::NodeInfo> contacts = bundle.service->getContacts();
        const std::vector<chat::contacts::NodeInfo> nearby = bundle.service->getNearby();
        const std::vector<chat::contacts::NodeInfo> ignored = bundle.service->getIgnoredNodes();
        Serial.printf("[ContactService] startup nodes=%u nicknames=%u contacts=%u nearby=%u ignored=%u\n",
                      static_cast<unsigned>(bundle.node_store->getEntries().size()),
                      static_cast<unsigned>(bundle.contact_store->getCount()),
                      static_cast<unsigned>(contacts.size()),
                      static_cast<unsigned>(nearby.size()),
                      static_cast<unsigned>(ignored.size()));
    }
    return bundle;
}

std::unique_ptr<chat::ChatService::IncomingMessageObserver> create_chat_message_observer(chat::ChatService& service)
{
    return std::unique_ptr<chat::ChatService::IncomingMessageObserver>(new chat::infra::ChatEventBusBridge(service));
}

app::ChatServicesBundle create_chat_services(const app::AppConfig& config,
                                             LoraBoard* lora_board,
                                             bool use_mock_adapter)
{
    (void)use_mock_adapter;

    app::ChatServicesBundle bundle;
    bundle.model = std::unique_ptr<chat::ChatModel>(new chat::ChatModel());
    if (!bundle.model)
    {
        return bundle;
    }
    bundle.model->setPolicy(config.chat_policy);

    bundle.store = create_chat_store();
    bundle.mesh_runtime = create_mesh_runtime();
    if (!bundle.store || !bundle.mesh_runtime)
    {
        return bundle;
    }

    if (lora_board)
    {
        std::unique_ptr<chat::IMeshAdapter> backend = create_mesh_backend(config.mesh_protocol, *lora_board);
        if (backend)
        {
            backend->applyConfig(config.activeMeshConfig());
            if (!bundle.mesh_runtime->installBackend(config.mesh_protocol, std::move(backend)))
            {
                Serial.printf("[APP] WARNING: Failed to install mesh adapter backend\n");
            }
        }
    }

    bundle.service = std::unique_ptr<chat::ChatService>(
        new chat::ChatService(*bundle.model, *bundle.mesh_runtime, *bundle.store, config.mesh_protocol));
    if (!bundle.service)
    {
        return bundle;
    }

    bundle.incoming_message_observer = create_chat_message_observer(*bundle.service);
    return bundle;
}

std::unique_ptr<team::ITeamCrypto> create_team_crypto()
{
    return std::unique_ptr<team::ITeamCrypto>(new team::infra::TeamCrypto());
}

std::unique_ptr<team::ITeamEventSink> create_team_event_sink()
{
    return std::unique_ptr<team::ITeamEventSink>(new team::infra::TeamEventBusSink());
}

std::unique_ptr<team::TeamService::UnhandledAppDataObserver> create_team_app_data_observer()
{
    return std::unique_ptr<team::TeamService::UnhandledAppDataObserver>(new team::infra::TeamAppDataEventBusBridge());
}

std::unique_ptr<team::ITeamPairingEventSink> create_team_pairing_event_sink()
{
    return std::unique_ptr<team::ITeamPairingEventSink>(new team::infra::TeamPairingEventBusSink());
}

app::TeamServicesBundle create_team_services(chat::IMeshAdapter& mesh_adapter)
{
    app::TeamServicesBundle bundle;
    bundle.crypto = create_team_crypto();
    bundle.event_sink = create_team_event_sink();
    bundle.app_data_observer = create_team_app_data_observer();
    bundle.pairing_event_sink = create_team_pairing_event_sink();
    if (!bundle.crypto || !bundle.event_sink || !bundle.app_data_observer || !bundle.pairing_event_sink)
    {
        return bundle;
    }

    auto platform_bundle = platform::esp::arduino_common::createTeamPlatformBundle(*bundle.pairing_event_sink);
    bundle.runtime = std::move(platform_bundle.runtime);
    bundle.track_source = std::move(platform_bundle.track_source);
    bundle.pairing_transport = std::move(platform_bundle.pairing_transport);
    bundle.pairing_service = std::move(platform_bundle.pairing_service);
    if (!bundle.runtime || !bundle.track_source || !bundle.pairing_transport || !bundle.pairing_service)
    {
        return bundle;
    }

    bundle.service = std::unique_ptr<team::TeamService>(
        new team::TeamService(*bundle.crypto, mesh_adapter, *bundle.event_sink, *bundle.runtime));
    if (!bundle.service)
    {
        return bundle;
    }
    bundle.service->setUnhandledAppDataObserver(bundle.app_data_observer.get());

    bundle.controller = std::unique_ptr<team::TeamController>(new team::TeamController(*bundle.service));
    bundle.track_sampler = std::unique_ptr<team::TeamTrackSampler>(
        new team::TeamTrackSampler(*bundle.runtime, *bundle.track_source));
    return bundle;
}

void finalize_startup(app::IAppFacade& app_facade)
{
    (void)ui_get_timezone_offset_min();

    team::TeamController* team_controller = app_facade.getTeamController();
    if (team_controller)
    {
        team::ui::TeamUiSnapshot snap;
        if (team::ui::team_ui_get_store().load(snap) &&
            snap.has_team_id && snap.has_team_psk && snap.security_round > 0)
        {
            if (team_controller->setKeysFromPsk(snap.team_id,
                                                snap.security_round,
                                                snap.team_psk.data(),
                                                snap.team_psk.size()))
            {
                Serial.printf("[Team] keys restored from store key_id=%lu\n",
                              static_cast<unsigned long>(snap.security_round));
            }
            else
            {
                Serial.printf("[Team] keys restore failed key_id=%lu\n",
                              static_cast<unsigned long>(snap.security_round));
            }
        }
    }

    team::TeamService* team_service = app_facade.getTeamService();
    app_facade.setTeamModeActive(team_service && team_service->hasKeys());
}

chat::NodeId get_self_node_id()
{
    return platform::esp::arduino_common::device_identity::getSelfNodeId();
}

} // namespace

namespace platform::esp::arduino_common
{

app::AppContextPlatformBindings makeAppContextPlatformBindings()
{
    app::AppContextPlatformBindings bindings{};
    bindings.load_app_config = app::loadAppConfig;
    bindings.save_app_config = app::saveAppConfig;
    bindings.load_message_tone_volume = app::loadMessageToneVolume;
    bindings.init_gps_runtime = init_gps_runtime;
    bindings.apply_position_config = apply_position_config;
    bindings.init_track_recorder = init_track_recorder;
    bindings.set_team_mode_active = set_team_mode_active;
    bindings.finalize_startup = finalize_startup;
    bindings.create_chat_services = create_chat_services;
    bindings.create_mesh_backend = create_mesh_backend;
    bindings.create_contact_services = create_contact_services;
    bindings.create_team_services = create_team_services;
    bindings.get_self_node_id = get_self_node_id;
    return bindings;
}

} // namespace platform::esp::arduino_common
