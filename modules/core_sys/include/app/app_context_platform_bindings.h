/**
 * @file app_context_platform_bindings.h
 * @brief Platform-side factories injected into AppContext
 */

#pragma once

#include "app/app_config.h"
#include "chat/domain/chat_model.h"
#include "chat/domain/chat_types.h"
#include "chat/ports/i_chat_store.h"
#include "chat/ports/i_contact_store.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/ports/i_node_store.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "team/ports/i_team_crypto.h"
#include "team/ports/i_team_event_sink.h"
#include "team/ports/i_team_pairing_event_sink.h"
#include "team/ports/i_team_pairing_transport.h"
#include "team/ports/i_team_runtime.h"
#include "team/ports/i_team_track_source.h"
#include "team/usecase/team_controller.h"
#include "team/usecase/team_pairing_service.h"
#include "team/usecase/team_service.h"
#include "team/usecase/team_track_sampler.h"
#include <memory>

class GpsBoard;
class LoraBoard;
class MotionBoard;

namespace app
{

class IAppFacade;

struct ChatServicesBundle
{
    std::unique_ptr<chat::ChatModel> model;
    std::unique_ptr<chat::IChatStore> store;
    std::unique_ptr<chat::IMeshAdapter> mesh_runtime;
    std::unique_ptr<chat::ChatService> service;
    std::unique_ptr<chat::ChatService::IncomingMessageObserver> incoming_message_observer;

    bool isValid() const
    {
        return model && store && mesh_runtime && service && incoming_message_observer;
    }
};

struct ContactServicesBundle
{
    std::unique_ptr<chat::contacts::INodeStore> node_store;
    std::unique_ptr<chat::contacts::IContactStore> contact_store;
    std::unique_ptr<chat::contacts::ContactService> service;

    bool isValid() const
    {
        return node_store && contact_store && service;
    }
};

struct TeamServicesBundle
{
    std::unique_ptr<team::ITeamCrypto> crypto;
    std::unique_ptr<team::ITeamEventSink> event_sink;
    std::unique_ptr<team::TeamService::UnhandledAppDataObserver> app_data_observer;
    std::unique_ptr<team::ITeamPairingEventSink> pairing_event_sink;
    std::unique_ptr<team::ITeamRuntime> runtime;
    std::unique_ptr<team::ITeamTrackSource> track_source;
    std::unique_ptr<team::ITeamPairingTransport> pairing_transport;
    std::unique_ptr<team::TeamPairingService> pairing_service;
    std::unique_ptr<team::TeamService> service;
    std::unique_ptr<team::TeamController> controller;
    std::unique_ptr<team::TeamTrackSampler> track_sampler;

    bool isValid() const
    {
        return crypto && event_sink && app_data_observer && pairing_event_sink &&
               runtime && track_source && pairing_transport && pairing_service &&
               service && controller && track_sampler;
    }
};

struct AppContextPlatformBindings
{
    using AppConfigLoader = bool (*)(AppConfig& config);
    using AppConfigSaver = bool (*)(const AppConfig& config);
    using MessageToneVolumeLoader = uint8_t (*)();
    using GpsRuntimeInitializer = void (*)(GpsBoard* gps_board,
                                           MotionBoard* motion_board,
                                           uint32_t disable_hw_init,
                                           const AppConfig& config);
    using PositionConfigApplier = void (*)(const AppConfig& config);
    using TrackRecorderInitializer = void (*)(const AppConfig& config);
    using TeamModeApplier = void (*)(bool active);
    using StartupFinalizer = void (*)(IAppFacade& app_facade);
    using ChatServicesFactory = ChatServicesBundle (*)(const AppConfig& config,
                                                       LoraBoard* lora_board,
                                                       bool use_mock_adapter);
    using MeshBackendFactory = std::unique_ptr<chat::IMeshAdapter> (*)(chat::MeshProtocol protocol,
                                                                       LoraBoard& lora_board);
    using ContactServicesFactory = ContactServicesBundle (*)();
    using TeamServicesFactory = TeamServicesBundle (*)(chat::IMeshAdapter& mesh_adapter);
    using SelfNodeIdProvider = chat::NodeId (*)();

    AppConfigLoader load_app_config = nullptr;
    AppConfigSaver save_app_config = nullptr;
    MessageToneVolumeLoader load_message_tone_volume = nullptr;
    GpsRuntimeInitializer init_gps_runtime = nullptr;
    PositionConfigApplier apply_position_config = nullptr;
    TrackRecorderInitializer init_track_recorder = nullptr;
    TeamModeApplier set_team_mode_active = nullptr;
    StartupFinalizer finalize_startup = nullptr;
    ChatServicesFactory create_chat_services = nullptr;
    MeshBackendFactory create_mesh_backend = nullptr;
    ContactServicesFactory create_contact_services = nullptr;
    TeamServicesFactory create_team_services = nullptr;
    SelfNodeIdProvider get_self_node_id = nullptr;

    bool isValid() const
    {
        return load_app_config && save_app_config && load_message_tone_volume &&
               init_gps_runtime && apply_position_config && init_track_recorder &&
               set_team_mode_active && finalize_startup && create_chat_services &&
               create_mesh_backend && create_contact_services && get_self_node_id;
    }
};

} // namespace app
