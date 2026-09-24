#include "platform/esp/arduino_common/app_context_platform_bindings.h"
#include "platform/esp/arduino_common/app_config_store.h"

#include <Arduino.h>

#include "app/app_facades.h"
#include "board/GpsBoard.h"
#include "board/MotionBoard.h"
#include "chat/infra/store/ram_store.h"
#include "chat/usecase/contact_service.h"
#include "platform/esp/arduino_common/chat/infra/auto_reply_observer.h"
#include "platform/esp/arduino_common/chat/infra/chat_event_bus_bridge.h"
#include "platform/esp/arduino_common/chat/infra/mesh_adapter_router.h"
#include "platform/esp/arduino_common/chat/infra/protocol_factory.h"
#include "platform/esp/arduino_common/chat/infra/store/sd_protocol_peer_repository.h"
#include "platform/esp/arduino_common/chat/infra/store/sd_store.h"
#include "platform/esp/arduino_common/device_identity.h"
#include "platform/esp/arduino_common/geocaching/browse_runtime.h"
#include "platform/esp/arduino_common/gps/gps_service.h"
#include "platform/esp/arduino_common/gps/track_recorder.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/arduino_common/storage/storage_runtime.h"
#include "platform/esp/arduino_common/team/crypto/team_crypto.h"
#include "platform/esp/arduino_common/team/event/team_app_data_event_bus_bridge.h"
#include "platform/esp/arduino_common/team/event/team_event_bus_sink.h"
#include "platform/esp/arduino_common/team/event/team_pairing_event_bus_sink.h"
#include "platform/esp/arduino_common/team_platform_bundle.h"
#include "platform/esp/arduino_common/voice/vmp_pager_session.h"
#include "platform/ui/team_ui_store_runtime.h"
#include "team/usecase/team_controller.h"
#include "team/usecase/team_track_sampler.h"
#include "ui/ui_common.h"

#include "freertos/FreeRTOS.h"

#include <new>

namespace
{

gps::GpsReceiverInitConfig make_receiver_init_config(const app::AppConfig& config)
{
    gps::GpsReceiverInitConfig init{};
    init.baud = config.gps_init_baud;
    init.probe_ms = config.gps_init_probe_ms;
    init.profile = config.gps_init_profile;
    init.rxm_policy = config.gps_init_rxm_policy;
    init.gnss_policy = config.gps_init_gnss_policy;
    init.nmea_policy = config.gps_init_nmea_policy;
    return init;
}

std::unique_ptr<chat::IMeshAdapter> create_mesh_runtime()
{
    return std::unique_ptr<chat::IMeshAdapter>(new chat::MeshAdapterRouter());
}

void init_gps_runtime(GpsBoard* gps_board,
                      MotionBoard* motion_board,
                      uint32_t disable_hw_init,
                      const app::AppConfig& config)
{
    if (!gps_board)
    {
        return;
    }

    auto& gps_service = gps::GpsService::getInstance();
    gps_service.begin(*gps_board,
                      motion_board,
                      disable_hw_init,
                      config.gps_interval_ms,
                      config.motion_config,
                      make_receiver_init_config(config));
    gps_service.setEnabled(config.gps_enabled);
    gps_service.setCollectionInterval(config.gps_interval_ms);
    gps_service.setPowerStrategy(config.gps_strategy);
    gps_service.setGnssConfig(config.gps_mode, config.gps_sat_mask);
    gps_service.setExternalNmeaConfig(config.external_nmea_output_hz, config.external_nmea_sentence_mask);
}

void apply_position_config(const app::AppConfig& config)
{
    gps::GpsService::getInstance().setReceiverInitConfig(make_receiver_init_config(config));
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
    recorder.setAutoRecording(config.map_track_enabled);
}

void deferred_storage_ready(app::IAppFacade& app_facade)
{
    // VMP's local attachment inbox follows the same deferred hydration gate
    // as the authoritative text store. This is a local restore only; it never
    // republishes an attachment to a radio, MQTT, or LXMF carrier.
    ::platform::esp::arduino_common::voice::vmp_session::onPersistentStorageReady();

    auto& recorder = gps::TrackRecorder::getInstance();
    if (recorder.restoreActiveSession())
    {
        Serial.printf("[Tracker] deferred active session restored path=%s\n",
                      recorder.currentPath().c_str());
    }

    team::TeamController* team_controller = app_facade.getTeamController();
    if (team_controller)
    {
        team::ui::TeamUiSnapshot snap;
        if (team::ui::team_ui_snapshot_store().load(snap) &&
            snap.has_team_id && snap.has_team_psk && snap.security_round > 0)
        {
            if (team_controller->setKeysFromPsk(snap.team_id,
                                                snap.security_round,
                                                snap.team_psk.data(),
                                                snap.team_psk.size()))
            {
                Serial.printf("[Team] deferred keys restored key_id=%lu\n",
                              static_cast<unsigned long>(snap.security_round));
            }
        }
    }
    app_facade.setTeamModeActive(
        app_facade.getTeamService() && app_facade.getTeamService()->hasKeys());
}

void set_team_mode_active(bool active)
{
    gps::GpsService::getInstance().setTeamModeActive(active);
}

std::unique_ptr<chat::IChatStore> create_chat_store(void** deferred_context)
{
    if (deferred_context)
    {
        *deferred_context = nullptr;
    }
    if (::platform::esp::arduino_common::storage::sd_card_ready())
    {
        std::unique_ptr<chat::SdStore> sd_store(new chat::SdStore());
        if (sd_store)
        {
            if (deferred_context)
            {
                *deferred_context = sd_store.get();
            }
            Serial.printf("[AppContext] chat store=SdStore hydration=pending backend=%s layout=/data/v2/{mt,mc,rt}/chat\n",
                          ::platform::esp::arduino_common::storage::sd_card_backend_name());
            return std::unique_ptr<chat::IChatStore>(sd_store.release());
        }
        Serial.printf("[AppContext] chat store=RamStore reason=sd_store_alloc_failed\n");
        return std::unique_ptr<chat::IChatStore>(new chat::RamStore());
    }

    Serial.printf("[AppContext] chat store=RamStore reason=sd_not_ready\n");
    return std::unique_ptr<chat::IChatStore>(new chat::RamStore());
}

std::unique_ptr<chat::IProtocolPeerRepository> create_mesh_peer_directory(
    chat::IChatStore& chat_store,
    void** deferred_context)
{
    if (deferred_context)
    {
        *deferred_context = nullptr;
    }
    std::unique_ptr<chat::IProtocolPeerRepository> repository(
        new (std::nothrow) chat::SdProtocolPeerRepository(chat_store));
    if (!repository)
    {
        return repository;
    }
    if (deferred_context)
    {
        *deferred_context = repository.get();
    }
    const auto status = repository->begin();
    Serial.printf("[PeerStoreV2] backend=sd root=/data/v2 status=%u hydration=pending\n",
                  static_cast<unsigned>(status.code));
    return repository;
}

std::unique_ptr<chat::IMeshAdapter> create_mesh_backend(chat::MeshProtocol protocol,
                                                        LoraBoard& lora_board,
                                                        chat::IMeshPeerDirectory* peer_directory)
{
    return chat::ProtocolFactory::createAdapter(protocol, lora_board, peer_directory);
}

app::ContactServicesBundle create_contact_services(
    chat::IProtocolPeerRepository& repository)
{
    app::ContactServicesBundle bundle;
    bundle.service = std::unique_ptr<chat::contacts::ContactService>(
        new chat::contacts::ContactService(repository));
    if (bundle.service)
    {
        bundle.service->begin();
        Serial.printf("[ContactService] unified peer directory ready\n");
    }
    return bundle;
}

std::unique_ptr<chat::ChatService::IncomingMessageObserver> create_chat_message_observer(chat::ChatService& service)
{
    return std::unique_ptr<chat::ChatService::IncomingMessageObserver>(new chat::infra::ChatEventBusBridge(service));
}

void start_deferred_storage(void* store_context,
                            void* peer_directory_context,
                            chat::MeshProtocol active_protocol)
{
    ::platform::esp::arduino_common::storage::start_deferred_storage(
        static_cast<chat::SdStore*>(store_context),
        static_cast<chat::SdProtocolPeerRepository*>(peer_directory_context),
        active_protocol);
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

    bundle.store = create_chat_store(&bundle.deferred_storage_store_context);
    if (!bundle.store)
    {
        return bundle;
    }
    bundle.mesh_peer_directory =
        create_mesh_peer_directory(*bundle.store,
                                   &bundle.deferred_storage_peer_context);
    bundle.mesh_runtime = create_mesh_runtime();
    if (!bundle.store || !bundle.mesh_peer_directory || !bundle.mesh_runtime)
    {
        return bundle;
    }

    if (lora_board)
    {
        std::unique_ptr<chat::IMeshAdapter> backend =
            create_mesh_backend(config.mesh_protocol,
                                *lora_board,
                                bundle.mesh_peer_directory.get());
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
    bundle.auto_reply_observer = chat::infra::create_auto_reply_observer(*bundle.service);
    if (!bundle.incoming_message_observer || !bundle.auto_reply_observer)
    {
        return app::ChatServicesBundle{};
    }
    if (bundle.deferred_storage_store_context ||
        bundle.deferred_storage_peer_context)
    {
        bundle.start_deferred_storage = start_deferred_storage;
    }
    if (lora_board)
        ::platform::esp::arduino_common::geocaching::browse_runtime::configure(
            *static_cast<chat::MeshAdapterRouter*>(bundle.mesh_runtime.get()), *lora_board);
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

    // Team snapshot restore is performed by deferred_storage_ready() after
    // the shell is interactive and maintenance hydration has completed.
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
    bindings.deferred_storage_ready = deferred_storage_ready;
    bindings.create_chat_services = create_chat_services;
    bindings.create_mesh_backend = create_mesh_backend;
    bindings.create_contact_services = create_contact_services;
    bindings.create_team_services = create_team_services;
    bindings.get_self_node_id = get_self_node_id;
    return bindings;
}

} // namespace platform::esp::arduino_common