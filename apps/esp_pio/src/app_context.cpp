/**
 * @file app_context.cpp
 * @brief Application context implementation
 */

#include "apps/esp_pio/app_context.h"
#include "ble/ble_manager.h"
#include "board/BoardBase.h"
#include "board/GpsBoard.h"
#include "board/LoraBoard.h"
#include "board/MotionBoard.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/runtime/self_identity_policy.h"
#include "sys/event_bus.h"
#include "ui/chat_ui_runtime_proxy.h"
#ifdef USING_ST25R3916
#endif
#include <cstdio>
#include <cstring>

namespace app
{

AppContext& AppContext::getInstance()
{
    static AppContext instance;
    return instance;
}

AppContext::AppContext()
    : chat_ui_runtime_proxy_(new chat::ui::GlobalChatUiRuntime())
{
}

AppContext::~AppContext() = default;

void AppContext::configurePlatformBindings(const AppContextPlatformBindings& bindings)
{
    platform_bindings_ = bindings;
}

void AppContext::assignBoards(BoardBase& board, LoraBoard* lora_board, GpsBoard* gps_board,
                              MotionBoard* motion_board)
{
    board_ = &board;
    lora_board_ = lora_board;
    gps_board_ = gps_board;
    motion_board_ = motion_board;
    const uint8_t tone_volume = platform_bindings_.load_message_tone_volume ? platform_bindings_.load_message_tone_volume() : 45;
    board_->setMessageToneVolume(tone_volume);
}

void AppContext::initGpsRuntime(uint32_t disable_hw_init)
{
    if (platform_bindings_.init_gps_runtime)
    {
        platform_bindings_.init_gps_runtime(gps_board_, motion_board_, disable_hw_init, config_);
    }
}

void AppContext::initTrackRecorder()
{
    if (platform_bindings_.init_track_recorder)
    {
        platform_bindings_.init_track_recorder(config_);
    }
}

std::unique_ptr<chat::IMeshAdapter> AppContext::createMeshBackend(chat::MeshProtocol protocol) const
{
    if (!lora_board_ || !platform_bindings_.create_mesh_backend)
    {
        return nullptr;
    }
    return platform_bindings_.create_mesh_backend(protocol, *lora_board_);
}

void AppContext::initChatRuntime(bool use_mock_adapter)
{
    if (!platform_bindings_.create_chat_services)
    {
        Serial.printf("[AppContext] chat platform bindings missing\n");
        return;
    }

    auto chat_services = platform_bindings_.create_chat_services(config_, lora_board_, use_mock_adapter);
    if (!chat_services.isValid())
    {
        Serial.printf("[AppContext] chat service bundle invalid\n");
        return;
    }

    chat_model_ = std::move(chat_services.model);
    chat_store_ = std::move(chat_services.store);
    mesh_router_ = std::move(chat_services.mesh_runtime);
    chat_service_ = std::move(chat_services.service);
    chat_event_bus_bridge_ = std::move(chat_services.incoming_message_observer);

    applyUserInfo();
    applyNetworkLimits();
    applyPrivacyConfig();
    applyChatDefaults();
}

void AppContext::initTeamServices()
{
    if (!mesh_router_)
    {
        Serial.printf("[Team] mesh router unavailable, skip team services\n");
        return;
    }

    if (!platform_bindings_.create_team_services)
    {
        if (platform_bindings_.set_team_mode_active)
        {
            platform_bindings_.set_team_mode_active(false);
        }
        return;
    }

    auto team_services = platform_bindings_.create_team_services(*mesh_router_);
    if (!team_services.isValid())
    {
        Serial.printf("[Team] service bundle invalid\n");
        return;
    }

    team_crypto_ = std::move(team_services.crypto);
    team_event_sink_ = std::move(team_services.event_sink);
    team_app_data_bridge_ = std::move(team_services.app_data_observer);
    team_pairing_event_sink_ = std::move(team_services.pairing_event_sink);
    team_runtime_ = std::move(team_services.runtime);
    team_track_source_ = std::move(team_services.track_source);
    team_pairing_transport_ = std::move(team_services.pairing_transport);
    team_pairing_service_ = std::move(team_services.pairing_service);
    team_service_ = std::move(team_services.service);
    team_controller_ = std::move(team_services.controller);
    team_track_sampler_ = std::move(team_services.track_sampler);
}

void AppContext::initContactServices()
{
    if (!platform_bindings_.create_contact_services)
    {
        Serial.printf("[AppContext] contact platform bindings missing\n");
        return;
    }

    auto contact_services = platform_bindings_.create_contact_services();
    if (!contact_services.isValid())
    {
        Serial.printf("[AppContext] contact service bundle invalid\n");
        return;
    }

    node_store_ = std::move(contact_services.node_store);
    contact_store_ = std::move(contact_services.contact_store);
    contact_service_ = std::move(contact_services.service);
}

chat::IMeshAdapter* AppContext::getMeshAdapter()
{
    return mesh_router_.get();
}

const chat::IMeshAdapter* AppContext::getMeshAdapter() const
{
    return mesh_router_.get();
}

chat::ui::IChatUiRuntime* AppContext::getChatUiRuntime()
{
    return chat_ui_runtime_proxy_.get();
}

void AppContext::setChatUiRuntime(chat::ui::IChatUiRuntime* runtime)
{
    if (chat_ui_runtime_proxy_)
    {
        chat_ui_runtime_proxy_->setActiveRuntime(runtime);
    }
}

void AppContext::saveConfig()
{
    if (platform_bindings_.save_app_config)
    {
        platform_bindings_.save_app_config(config_);
    }
}

void AppContext::applyMeshConfig()
{
    if (mesh_router_)
    {
        if (mesh_router_->backendProtocol() != config_.mesh_protocol)
        {
            (void)switchMeshProtocol(config_.mesh_protocol, false);
        }
        else
        {
            mesh_router_->applyConfig(config_.activeMeshConfig());
        }
    }
    if (chat_service_)
    {
        chat_service_->setActiveProtocol(config_.mesh_protocol);
    }
}

void AppContext::applyUserInfo()
{
    if (mesh_router_)
    {
        char long_name[sizeof(config_.node_name)];
        char short_name[sizeof(config_.short_name)];
        getEffectiveUserInfo(long_name, sizeof(long_name), short_name, sizeof(short_name));
        mesh_router_->setUserInfo(long_name, short_name);
    }
}

void AppContext::broadcastNodeInfo()
{
    if (mesh_router_)
    {
        mesh_router_->requestNodeInfo(0xFFFFFFFF, false);
    }
}

void AppContext::applyNetworkLimits()
{
    if (mesh_router_)
    {
        mesh_router_->setNetworkLimits(config_.net_duty_cycle, config_.net_channel_util);
    }
}

void AppContext::applyPrivacyConfig()
{
    if (mesh_router_)
    {
        mesh_router_->setPrivacyConfig(config_.privacy_encrypt_mode);
    }
}

bool AppContext::isBleEnabled() const
{
    return config_.ble_enabled;
}

bool AppContext::init(BoardBase& board, LoraBoard* lora_board, GpsBoard* gps_board, MotionBoard* motion_board,
                      bool use_mock_adapter, uint32_t disable_hw_init)
{
    if (!platform_bindings_.isValid())
    {
        Serial.printf("[AppContext] ERROR: platform bindings not configured\n");
        return false;
    }

    assignBoards(board, lora_board, gps_board, motion_board);

    if (!sys::EventBus::init())
    {
        return false;
    }

    if (platform_bindings_.load_app_config)
    {
        platform_bindings_.load_app_config(config_);
    }
    initGpsRuntime(disable_hw_init);
    initTrackRecorder();
    initChatRuntime(use_mock_adapter);
    initTeamServices();
    initContactServices();
    if (platform_bindings_.finalize_startup)
    {
        platform_bindings_.finalize_startup(*this);
    }

    return true;
}

bool AppContext::switchMeshProtocol(chat::MeshProtocol protocol, bool persist)
{
    if (!mesh_router_ || !lora_board_)
    {
        return false;
    }

    if (!chat::infra::isValidMeshProtocol(protocol))
    {
        return false;
    }

    std::unique_ptr<chat::IMeshAdapter> backend = createMeshBackend(protocol);
    if (!backend)
    {
        return false;
    }

    const chat::MeshProtocol previous_protocol = config_.mesh_protocol;
    config_.mesh_protocol = protocol;

    backend->applyConfig(config_.activeMeshConfig());

    char long_name[sizeof(config_.node_name)];
    char short_name[sizeof(config_.short_name)];
    getEffectiveUserInfo(long_name, sizeof(long_name),
                         short_name, sizeof(short_name));
    backend->setUserInfo(long_name, short_name);
    backend->setNetworkLimits(config_.net_duty_cycle, config_.net_channel_util);
    backend->setPrivacyConfig(config_.privacy_encrypt_mode);

    if (!mesh_router_->installBackend(protocol, std::move(backend)))
    {
        config_.mesh_protocol = previous_protocol;
        return false;
    }

    if (chat_service_)
    {
        chat_service_->setActiveProtocol(protocol);
    }

    if (persist)
    {
        saveConfig();
    }
    return true;
}

void AppContext::applyPositionConfig()
{
    if (platform_bindings_.apply_position_config)
    {
        platform_bindings_.apply_position_config(config_);
    }
}

void AppContext::getEffectiveUserInfo(char* out_long, size_t long_len,
                                      char* out_short, size_t short_len) const
{
    if (!out_long || long_len == 0 || !out_short || short_len == 0)
    {
        return;
    }

    chat::runtime::SelfIdentityInput input{};
    input.node_id = getSelfNodeId();
    input.configured_long_name = config_.node_name;
    input.configured_short_name = config_.short_name;
    input.fallback_long_prefix = "lilygo";
    input.fallback_ble_prefix = "TrailMate";
    input.allow_short_hex_fallback = true;

    chat::runtime::EffectiveSelfIdentity identity{};
    (void)chat::runtime::resolveEffectiveSelfIdentity(input, &identity);

    strncpy(out_long, identity.long_name, long_len - 1);
    out_long[long_len - 1] = '\0';
    strncpy(out_short, identity.short_name, short_len - 1);
    out_short[short_len - 1] = '\0';
}

void AppContext::updateCoreServices()
{
    if (event_runtime_hooks_.update_core_services)
    {
        event_runtime_hooks_.update_core_services(*this);
    }
}

void AppContext::tickEventRuntime()
{
    if (event_runtime_hooks_.tick)
    {
        event_runtime_hooks_.tick(*this);
    }
}

void AppContext::dispatchPendingEvents(size_t max_events)
{
    sys::Event* event = nullptr;
    for (size_t processed = 0;
         processed < max_events && sys::EventBus::subscribe(&event, 0);)
    {
        if (!event)
        {
            continue;
        }
        ++processed;

        if (event_runtime_hooks_.dispatch_event && event_runtime_hooks_.dispatch_event(*this, event))
        {
            continue;
        }

        if (event_runtime_hooks_.handle_event && event_runtime_hooks_.handle_event(*this, event))
        {
            continue;
        }

        delete event;
    }
}

void AppContext::attachEventRuntimeHooks(const AppEventRuntimeHooks& hooks)
{
    event_runtime_hooks_ = hooks;
}

void AppContext::attachBleManager(std::unique_ptr<ble::BleManager> ble_manager)
{
    ble_manager_ = std::move(ble_manager);
}

void AppContext::setBleEnabled(bool enabled)
{
    config_.ble_enabled = enabled;
    if (ble_manager_)
    {
        ble_manager_->setEnabled(enabled);
    }
    saveConfig();
}

chat::NodeId AppContext::getSelfNodeId() const
{
    return platform_bindings_.get_self_node_id ? platform_bindings_.get_self_node_id() : 0;
}

void AppContext::clearNodeDb()
{
    if (node_store_)
    {
        node_store_->clear();
    }
    if (contact_service_)
    {
        contact_service_->clearCache();
    }
}

void AppContext::clearMessageDb()
{
    if (chat_service_)
    {
        chat_service_->clearAllMessages();
    }
    else if (chat_model_)
    {
        chat_model_->clearAll();
        if (chat_store_)
        {
            chat_store_->clearAll();
        }
    }
}

} // namespace app
