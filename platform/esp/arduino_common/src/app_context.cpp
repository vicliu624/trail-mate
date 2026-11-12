/**
 * @file app_context.cpp
 * @brief ESP Arduino application context implementation.
 */

#include "app/app_context.h"

#include <Arduino.h>

#include "ble/ble_manager.h"
#include "board/BoardBase.h"
#include "board/GpsBoard.h"
#include "board/LoraBoard.h"
#include "board/MotionBoard.h"
#include "chat/infra/lxmf/lxmf_announce_defaults.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/runtime/self_identity_policy.h"
#include "chat/usecase/contact_service.h"
#include "platform/esp/arduino_common/app_tasks.h"
#include "platform/esp/arduino_common/chat/infra/mesh_adapter_router.h"
#include "platform/esp/arduino_common/memory_diag.h"
#include "platform/esp/arduino_common/storage/storage_runtime.h"
#include "platform/esp/arduino_common/voice/vmp_pager_session.h"
#include "platform/ui/quick_controls_runtime.h"
#include "platform/ui/reticulum_call_runtime.h"
#include "platform/ui/reticulum_directory_runtime.h"
#include "sys/event_bus.h"
#include "ui/chat_ui_runtime_proxy.h"
#include "ui/chat_voice_runtime.h"
#include "ui/ui_boot.h"

#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <new>

namespace app
{
namespace
{
constexpr TickType_t kConfigSaveMutexWait = pdMS_TO_TICKS(20);

class PagerVoiceMessageRuntime final : public ::ui::chat_voice::IVoiceMessageRuntime
{
  public:
    bool initialize()
    {
        if (metadata_scratch_)
        {
            return true;
        }
        metadata_scratch_ = static_cast<chat::voice::vmp::VoiceMessageMetadata*>(
            heap_caps_malloc(sizeof(chat::voice::vmp::VoiceMessageMetadata) *
                                 chat::voice::vmp::kVoiceInboxCapacity,
                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        return metadata_scratch_ != nullptr;
    }

    bool isAvailable() const override
    {
        return metadata_scratch_ != nullptr &&
               ::platform::esp::arduino_common::voice::vmp_session::inbox() != nullptr;
    }

    bool canRecordAndSend() const override
    {
        return ::platform::esp::arduino_common::voice::vmp_session::canRecordAndSend();
    }

    ::ui::chat_voice::StartResult requestRecordAndSend(
        const ::ui::chat_voice::SendRequest& request) override
    {
        switch (::platform::esp::arduino_common::voice::vmp_session::requestRecordAndSend(
            request.target_id,
            request.presentation_protocol,
            request.presentation_channel))
        {
        case ::platform::esp::arduino_common::voice::vmp_session::StartSendResult::Queued:
            return ::ui::chat_voice::StartResult::Queued;
        case ::platform::esp::arduino_common::voice::vmp_session::StartSendResult::Busy:
            return ::ui::chat_voice::StartResult::Busy;
        case ::platform::esp::arduino_common::voice::vmp_session::StartSendResult::PrivateContactUnverified:
            return ::ui::chat_voice::StartResult::PrivateContactUnverified;
        case ::platform::esp::arduino_common::voice::vmp_session::StartSendResult::ResourceUnavailable:
            return ::ui::chat_voice::StartResult::ResourceUnavailable;
        case ::platform::esp::arduino_common::voice::vmp_session::StartSendResult::Unsupported:
        default:
            return ::ui::chat_voice::StartResult::Unsupported;
        }
    }

    bool requestStopRecording() override
    {
        return ::platform::esp::arduino_common::voice::vmp_session::
            requestStopRecording();
    }

    bool isOutboundActive() const override
    {
        return ::platform::esp::arduino_common::voice::vmp_session::
            isOutboundActive();
    }

    std::size_t listMessages(
        ::ui::chat_voice::MessageSummary* out_messages,
        std::size_t capacity) const override
    {
        if (!out_messages || capacity == 0U || !metadata_scratch_)
        {
            return 0U;
        }
        const std::size_t limit =
            capacity < chat::voice::vmp::kVoiceInboxCapacity
                ? capacity
                : chat::voice::vmp::kVoiceInboxCapacity;
        const std::size_t count =
            ::platform::esp::arduino_common::voice::vmp_session::listInboxMetadata(
                metadata_scratch_, limit);
        for (std::size_t index = 0U; index < count; ++index)
        {
            const auto& metadata = metadata_scratch_[index];
            out_messages[index].local_id = metadata.local_id;
            out_messages[index].sender_id = metadata.sender_id;
            out_messages[index].target_id = metadata.target_id;
            out_messages[index].received_at_seconds = metadata.received_at_seconds;
            // Codec2-1300 frames are fixed at 40 ms and seven encoded bytes.
            // The UI receives duration only, never a pointer to media bytes.
            out_messages[index].duration_ms = static_cast<uint16_t>(
                (metadata.encoded_media_len / 7U) * 40U);
            out_messages[index].private_message =
                metadata.mode == chat::voice::vmp::DeliveryMode::Private;
            out_messages[index].source_unverified =
                chat::voice::vmp::voiceMessageSourceUnverified(metadata);
            out_messages[index].outgoing =
                chat::voice::vmp::voiceMessageOutgoing(metadata);
            out_messages[index].delivery =
                static_cast<::ui::chat_voice::DeliveryState>(metadata.delivery);
            out_messages[index].presentation_protocol =
                static_cast<uint8_t>(metadata.presentation_protocol);
            out_messages[index].presentation_channel = metadata.presentation_channel;
            out_messages[index].read = chat::voice::vmp::voiceMessageRead(metadata);
        }
        return count;
    }

    bool markConversationRead(uint8_t presentation_protocol,
                              uint8_t presentation_channel,
                              uint32_t peer_id,
                              bool broadcast) override
    {
        return ::platform::esp::arduino_common::voice::vmp_session::
            markConversationRead(presentation_protocol,
                                 presentation_channel,
                                 peer_id,
                                 broadcast);
    }

    bool requestPlayback(uint64_t local_id) override
    {
        return ::platform::esp::arduino_common::voice::vmp_session::requestPlayback(
            local_id);
    }

  private:
    mutable chat::voice::vmp::VoiceMessageMetadata* metadata_scratch_ = nullptr;
};

PagerVoiceMessageRuntime s_pager_voice_message_runtime{};

bool deriveVmpVerifiedContactSecret(void* context,
                                    uint32_t peer_id,
                                    uint8_t out_secret[chat::voice::vmp::kPrivateKeySize])
{
    auto* const app_context = static_cast<AppContext*>(context);
    if (!app_context || !out_secret || peer_id == 0U ||
        peer_id == chat::voice::vmp::kBroadcastTargetId)
    {
        return false;
    }

    const chat::MeshProtocol protocol = chat::infra::normalizeMeshProtocol(
        app_context->getMeshProtocol());
    if (protocol != chat::MeshProtocol::Meshtastic &&
        protocol != chat::MeshProtocol::MeshCore &&
        protocol != chat::MeshProtocol::Reticulum)
    {
        return false;
    }

    const chat::contacts::PeerDirectoryItem* const peer =
        app_context->getContactService().getPeerByNodeId(peer_id);
    if (!peer || peer->is_ignored || !peer->has_public_key ||
        ((protocol == chat::MeshProtocol::Meshtastic ||
          protocol == chat::MeshProtocol::MeshCore) &&
         !peer->key_manually_verified) ||
        (protocol == chat::MeshProtocol::Reticulum && !peer->is_contact))
    {
        return false;
    }

    // AppContext owns MeshAdapterRouter for the ESP production runtime. Its
    // VMP-only method intentionally does not widen the IMeshAdapter contract.
    auto* const router = static_cast<chat::MeshAdapterRouter*>(
        app_context->getMeshAdapter());
    return router && router->deriveVmpContactSecret(peer_id, out_secret);
}

bool sendVmpLxmfEnvelope(void* context,
                         uint32_t target_id,
                         const uint8_t* envelope,
                         std::size_t envelope_len)
{
    auto* const app_context = static_cast<AppContext*>(context);
    if (!app_context || !envelope || envelope_len == 0U ||
        target_id == chat::voice::vmp::kBroadcastTargetId ||
        !chat::infra::isReticulumMeshProtocol(
            chat::infra::normalizeMeshProtocol(app_context->getMeshProtocol())))
    {
        return false;
    }
    chat::IMeshAdapter* const adapter = app_context->getMeshAdapter();
    return adapter && adapter->sendAppData(
                          chat::ChannelId::PRIMARY,
                          ::platform::esp::arduino_common::voice::vmp_session::kLxmfAppDataPort,
                          envelope,
                          envelope_len,
                          target_id,
                          false,
                          0U,
                          false);
}

void normalize_reticulum_interface_strategy(AppConfig& config)
{
    chat::MeshConfig& reticulum = config.reticulumConfig();
    switch (reticulum.reticulum_interface_policy)
    {
    case chat::ReticulumInterfacePolicy::LoRaOnly:
        reticulum.reticulum_lora_enabled = true;
        reticulum.reticulum_wifi_gateway_enabled = false;
        break;
    case chat::ReticulumInterfacePolicy::WifiGatewayOnly:
        reticulum.reticulum_lora_enabled = false;
        reticulum.reticulum_wifi_gateway_enabled = true;
        break;
    case chat::ReticulumInterfacePolicy::All:
    default:
        reticulum.reticulum_interface_policy = chat::ReticulumInterfacePolicy::All;
        reticulum.reticulum_lora_enabled = true;
        reticulum.reticulum_wifi_gateway_enabled = true;
        break;
    }
}

void sync_reticulum_group_config(AppConfig& config)
{
    if (!chat::infra::isReticulumMeshProtocol(
            chat::infra::normalizeMeshProtocol(config.mesh_protocol)))
    {
        return;
    }
    normalize_reticulum_interface_strategy(config);
}
} // namespace

AppContext& AppContext::getInstance()
{
    static AppContext* instance = []() -> AppContext*
    {
        void* storage = heap_caps_malloc_prefer(sizeof(AppContext),
                                                2,
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (storage == nullptr)
        {
            storage = ::operator new(sizeof(AppContext));
        }
        return new (storage) AppContext();
    }();
    return *instance;
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

bool AppContext::preloadConfig()
{
    if (config_preloaded_)
    {
        return true;
    }
    if (!platform_bindings_.load_app_config)
    {
        Serial.printf("[AppContext] ERROR: app config loader is not configured\n");
        return false;
    }

    ::ui::boot::set_log_line("Loading app config...");
    if (!platform_bindings_.load_app_config(config_))
    {
        Serial.printf("[AppContext] ERROR: app config preload failed\n");
        return false;
    }
    config_preloaded_ = true;
    return true;
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
    return platform_bindings_.create_mesh_backend(protocol,
                                                  *lora_board_,
                                                  mesh_peer_directory_.get());
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

    deferred_storage_starter_ = chat_services.start_deferred_storage;
    deferred_storage_store_context_ =
        chat_services.deferred_storage_store_context;
    deferred_storage_peer_context_ =
        chat_services.deferred_storage_peer_context;
    chat_model_ = std::move(chat_services.model);
    chat_store_ = std::move(chat_services.store);
    mesh_peer_directory_ = std::move(chat_services.mesh_peer_directory);
    ::platform::ui::reticulum_directory::bind_mesh_peer_directory(
        mesh_peer_directory_.get());
    mesh_router_ = std::move(chat_services.mesh_runtime);
    chat_service_ = std::move(chat_services.service);
    chat_event_bus_bridge_ = std::move(chat_services.incoming_message_observer);
    auto_reply_observer_ = std::move(chat_services.auto_reply_observer);

    applyUserInfo();
    applyNetworkLimits();
    applyPrivacyConfig();
    applyChatDefaults();
#if defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_DECK) || defined(ARDUINO_WIO_TRACKER_L2)
    if (!::platform::esp::arduino_common::voice::vmp_session::initialize(
            getSelfNodeId(), deferred_storage_store_context_ != nullptr))
    {
        Serial.printf("[VMP] voice service unavailable\n");
        ::ui::chat_voice::setRuntime(nullptr);
    }
    else
    {
        ::platform::esp::arduino_common::voice::vmp_session::setPresentationProtocol(
            static_cast<uint8_t>(chat::infra::normalizeMeshProtocol(
                config_.mesh_protocol)));
        ::platform::esp::arduino_common::voice::vmp_session::setVerifiedContactSecretDeriver(
            &deriveVmpVerifiedContactSecret, this);
#if defined(ARDUINO_LILYGO_LORA_LR1121)
        ::platform::esp::arduino_common::voice::vmp_session::setLxmfEnvelopeSender(
            &sendVmpLxmfEnvelope, this);
        ::platform::esp::arduino_common::voice::vmp_session::setLxmfCarrierEnabled(
            chat::infra::isReticulumMeshProtocol(
                chat::infra::normalizeMeshProtocol(config_.mesh_protocol)));
#endif
        if (!s_pager_voice_message_runtime.initialize())
        {
            // This metadata projection is deliberately PSRAM-only. A Pager
            // without its bounded external-media pool must not silently add
            // another permanent internal-RAM VMP buffer.
            Serial.printf("[VMP] UI metadata scratch unavailable in PSRAM\n");
            ::ui::chat_voice::setRuntime(nullptr);
        }
        else
        {
            ::ui::chat_voice::setRuntime(&s_pager_voice_message_runtime);
        }
    }
#else
    ::ui::chat_voice::setRuntime(nullptr);
#endif
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

    if (!mesh_peer_directory_)
    {
        Serial.printf("[AppContext] protocol peer repository unavailable\n");
        return;
    }

    auto contact_services =
        platform_bindings_.create_contact_services(*mesh_peer_directory_);
    if (!contact_services.isValid())
    {
        Serial.printf("[AppContext] contact service bundle invalid\n");
        return;
    }

    contact_service_ = std::move(contact_services.service);
    if (contact_service_)
    {
        contact_service_->setActiveProtocol(config_.mesh_protocol);
    }
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
    enqueueConfigSave(AppConfigChangeSet::none());
}

void AppContext::requestSaveConfig()
{
    enqueueConfigSave(AppConfigChangeSet::none());
}

void AppContext::saveConfig(AppConfigChangeSet changes)
{
    enqueueConfigSave(changes);
}

void AppContext::requestSaveConfig(AppConfigChangeSet changes)
{
    enqueueConfigSave(changes);
}

AppConfigEdit AppContext::beginConfigEdit()
{
    ensureConfigPersistenceLock();
    if (config_state_mutex_ == nullptr ||
        xSemaphoreTake(config_state_mutex_, kConfigSaveMutexWait) != pdTRUE)
    {
        Serial.println("[AppCfg][EDIT] unavailable");
        return AppConfigEdit();
    }

    return AppConfigEdit(&config_,
                         this,
                         &AppContext::commitConfigEdit,
                         &AppContext::cancelConfigEdit);
}

void AppContext::ensureConfigPersistenceLock()
{
    if (config_state_mutex_ == nullptr)
    {
        config_state_mutex_ = xSemaphoreCreateMutex();
    }
}

void AppContext::enqueueConfigSave(AppConfigChangeSet requested_changes)
{
    if (platform_bindings_.save_app_config)
    {
        ensureConfigPersistenceLock();
        if (config_state_mutex_ == nullptr)
        {
            Serial.println("[AppCfg][SAVE_OWNER] unavailable");
            return;
        }

        if (xSemaphoreTake(config_state_mutex_, kConfigSaveMutexWait) != pdTRUE)
        {
            Serial.println("[AppCfg][SAVE_OWNER] submit_busy");
            return;
        }

        uint32_t generation = 0;
        AppConfigChangeSet queued_changes = AppConfigChangeSet::none();
        const bool should_signal = enqueueConfigSaveLocked(requested_changes,
                                                           &generation,
                                                           &queued_changes);
        xSemaphoreGive(config_state_mutex_);

        if (!should_signal)
        {
            Serial.println("[AppCfg][SAVE_OWNER] noop");
            return;
        }

        Serial.printf("[AppCfg][SAVE_OWNER] submitted gen=%lu changes=0x%08lx\n",
                      static_cast<unsigned long>(generation),
                      static_cast<unsigned long>(queued_changes.bits()));
    }
}

bool AppContext::enqueueConfigSaveLocked(AppConfigChangeSet requested_changes,
                                         uint32_t* out_generation,
                                         AppConfigChangeSet* out_changes)
{
    const ConfigPersistenceSubmission submission =
        config_persistence_runtime_.submit(requested_changes,
                                           millis());
    if (!submission.queued)
    {
        return false;
    }

    if (out_generation)
    {
        *out_generation = submission.generation;
    }
    if (out_changes)
    {
        *out_changes = submission.changes;
    }
    return true;
}

void AppContext::finishConfigEdit(AppConfigChangeSet changes)
{
    uint32_t generation = 0;
    AppConfigChangeSet queued_changes = AppConfigChangeSet::none();
    const bool should_signal = enqueueConfigSaveLocked(changes,
                                                       &generation,
                                                       &queued_changes);
    xSemaphoreGive(config_state_mutex_);

    if (!should_signal)
    {
        Serial.println("[AppCfg][EDIT] noop");
        return;
    }

    Serial.printf("[AppCfg][EDIT] submitted gen=%lu changes=0x%08lx\n",
                  static_cast<unsigned long>(generation),
                  static_cast<unsigned long>(queued_changes.bits()));
}

void AppContext::commitConfigEdit(void* context, AppConfigChangeSet changes)
{
    auto* self = static_cast<AppContext*>(context);
    if (self)
    {
        self->finishConfigEdit(changes);
    }
}

void AppContext::cancelConfigEdit(void* context)
{
    auto* self = static_cast<AppContext*>(context);
    if (self && self->config_state_mutex_)
    {
        xSemaphoreGive(self->config_state_mutex_);
    }
}

void AppContext::flushConfigPersistence(uint32_t now_ms)
{
    if (!platform_bindings_.save_app_config || config_state_mutex_ == nullptr)
    {
        return;
    }

    ConfigPersistenceWork work{};
    if (xSemaphoreTake(config_state_mutex_, 0) != pdTRUE)
    {
        return;
    }
    if (!config_persistence_runtime_.takeDue(now_ms, work))
    {
        xSemaphoreGive(config_state_mutex_);
        return;
    }

    Serial.printf("[AppCfg][SAVE_OWNER] flush begin gen=%lu changes=0x%08lx\n",
                  static_cast<unsigned long>(work.generation),
                  static_cast<unsigned long>(work.changes.bits()));
    const bool ok = platform_bindings_.save_app_config(config_, work.changes);
    config_persistence_runtime_.complete(
        work.generation,
        ok ? ConfigPersistenceResultKind::Completed
           : ConfigPersistenceResultKind::IoError,
        millis());
    xSemaphoreGive(config_state_mutex_);
    Serial.printf("[AppCfg][SAVE_OWNER] flush done gen=%lu ok=%u\n",
                  static_cast<unsigned long>(work.generation),
                  ok ? 1U : 0U);
}

void AppContext::applyMeshConfig()
{
    sync_reticulum_group_config(config_);
    if (!chat::infra::isReticulumMeshProtocol(
            chat::infra::normalizeMeshProtocol(config_.mesh_protocol)))
    {
        AppTasks::setRadioReceiveSuppressed(false);
    }
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
#if defined(ARDUINO_T_LORA_PAGER) && defined(ARDUINO_LILYGO_LORA_LR1121)
    ::platform::esp::arduino_common::voice::vmp_session::setLxmfCarrierEnabled(
        chat::infra::isReticulumMeshProtocol(
            chat::infra::normalizeMeshProtocol(config_.mesh_protocol)));
#endif
#if defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_DECK) || defined(ARDUINO_WIO_TRACKER_L2)
    ::platform::esp::arduino_common::voice::vmp_session::setPresentationProtocol(
        static_cast<uint8_t>(chat::infra::normalizeMeshProtocol(config_.mesh_protocol)));
#endif
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
        mesh_router_->broadcastSelfIdentity();
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
#if defined(TRAIL_MATE_ENABLE_BLE) && TRAIL_MATE_ENABLE_BLE
    return config_.ble_enabled;
#else
    return false;
#endif
}

bool AppContext::isInitialStorageHydrationPending() const
{
    return ::platform::esp::arduino_common::storage::initial_hydration_pending();
}

bool AppContext::init(BoardBase& board, LoraBoard* lora_board, GpsBoard* gps_board, MotionBoard* motion_board,
                      bool use_mock_adapter, uint32_t disable_hw_init)
{
    const uint32_t init_started_ms = millis();
    if (!platform_bindings_.isValid())
    {
        Serial.printf("[AppContext] ERROR: platform bindings not configured\n");
        return false;
    }

    assignBoards(board, lora_board, gps_board, motion_board);
    platform::esp::arduino_common::memory_diag::logHeapSnapshot("appctx.after_assign_boards");

    if (!sys::EventBus::init())
    {
        return false;
    }
    platform::esp::arduino_common::memory_diag::logHeapSnapshot("appctx.after_event_bus");

    if (!config_preloaded_)
    {
        if (!preloadConfig())
        {
            return false;
        }
    }
    config_persistence_runtime_.initialize();
    const uint32_t after_config_ms = millis();
    Serial.printf("[AppContext] phase=load_config elapsed_ms=%lu total_ms=%lu\n",
                  static_cast<unsigned long>(after_config_ms - init_started_ms),
                  static_cast<unsigned long>(after_config_ms - init_started_ms));
    platform::esp::arduino_common::memory_diag::logHeapSnapshot("appctx.after_load_config");
    ::ui::boot::set_log_line("Initializing GPS services...");
    initGpsRuntime(disable_hw_init);
    const uint32_t after_gps_ms = millis();
    Serial.printf("[AppContext] phase=gps elapsed_ms=%lu total_ms=%lu\n",
                  static_cast<unsigned long>(after_gps_ms - after_config_ms),
                  static_cast<unsigned long>(after_gps_ms - init_started_ms));
    platform::esp::arduino_common::memory_diag::logHeapSnapshot("appctx.after_gps_runtime");
    initTrackRecorder();
    const uint32_t after_track_ms = millis();
    Serial.printf("[AppContext] phase=track elapsed_ms=%lu total_ms=%lu\n",
                  static_cast<unsigned long>(after_track_ms - after_gps_ms),
                  static_cast<unsigned long>(after_track_ms - init_started_ms));
    platform::esp::arduino_common::memory_diag::logHeapSnapshot("appctx.after_track_recorder");
    ::ui::boot::set_log_line("Initializing chat runtime...");
    initChatRuntime(use_mock_adapter);
    const uint32_t after_chat_ms = millis();
    Serial.printf("[AppContext] phase=chat elapsed_ms=%lu total_ms=%lu\n",
                  static_cast<unsigned long>(after_chat_ms - after_track_ms),
                  static_cast<unsigned long>(after_chat_ms - init_started_ms));
    platform::esp::arduino_common::memory_diag::logHeapSnapshot("appctx.after_chat_runtime");
    ::ui::boot::set_log_line("Initializing team services...");
    initTeamServices();
    const uint32_t after_team_ms = millis();
    Serial.printf("[AppContext] phase=team elapsed_ms=%lu total_ms=%lu\n",
                  static_cast<unsigned long>(after_team_ms - after_chat_ms),
                  static_cast<unsigned long>(after_team_ms - init_started_ms));
    platform::esp::arduino_common::memory_diag::logHeapSnapshot("appctx.after_team_services");
    ::ui::boot::set_log_line("Initializing contacts...");
    initContactServices();
    const uint32_t after_contacts_ms = millis();
    Serial.printf("[AppContext] phase=contacts elapsed_ms=%lu total_ms=%lu\n",
                  static_cast<unsigned long>(after_contacts_ms - after_team_ms),
                  static_cast<unsigned long>(after_contacts_ms - init_started_ms));
    platform::esp::arduino_common::memory_diag::logHeapSnapshot("appctx.after_contact_services");
    if (platform_bindings_.finalize_startup)
    {
        ::ui::boot::set_log_line("Finalizing startup...");
        platform_bindings_.finalize_startup(*this);
    }
    Serial.printf("[AppContext] phase=finalize total_ms=%lu\n",
                  static_cast<unsigned long>(millis() - init_started_ms));
    platform::esp::arduino_common::memory_diag::logHeapSnapshot("appctx.after_finalize_startup");

    return true;
}

void AppContext::startDeferredStorage()
{
    if (deferred_storage_started_)
    {
        return;
    }
    deferred_storage_started_ = true;
    if (deferred_storage_starter_)
    {
        deferred_storage_starter_(deferred_storage_store_context_,
                                  deferred_storage_peer_context_,
                                  config_.mesh_protocol);
    }
}

bool AppContext::switchMeshProtocol(chat::MeshProtocol protocol, bool persist)
{
    if (!mesh_router_ || !lora_board_)
    {
        return false;
    }

    const chat::MeshProtocol normalized = chat::infra::normalizeMeshProtocol(protocol);
    if (!chat::infra::isValidMeshProtocol(normalized))
    {
        return false;
    }

    std::unique_ptr<chat::IMeshAdapter> backend = createMeshBackend(normalized);
    if (!backend)
    {
        return false;
    }
    if (!AppTasks::pauseRadioTasks())
    {
        return false;
    }

    const chat::MeshProtocol previous_protocol = config_.mesh_protocol;
    config_.mesh_protocol = normalized;
    sync_reticulum_group_config(config_);
    if (!chat::infra::isReticulumMeshProtocol(normalized))
    {
        AppTasks::setRadioReceiveSuppressed(false);
    }

    backend->applyConfig(config_.activeMeshConfig());

    char long_name[sizeof(config_.node_name)];
    char short_name[sizeof(config_.short_name)];
    getEffectiveUserInfo(long_name, sizeof(long_name),
                         short_name, sizeof(short_name));
    backend->setUserInfo(long_name, short_name);
    backend->setNetworkLimits(config_.net_duty_cycle, config_.net_channel_util);
    backend->setPrivacyConfig(config_.privacy_encrypt_mode);

    if (!mesh_router_->installBackend(normalized, std::move(backend)))
    {
        config_.mesh_protocol = previous_protocol;
        sync_reticulum_group_config(config_);
        mesh_router_->applyConfig(config_.activeMeshConfig());
        AppTasks::resumeRadioTasks();
        return false;
    }

    if (chat_service_)
    {
        chat_service_->setActiveProtocol(normalized);
    }
    if (contact_service_)
    {
        contact_service_->setActiveProtocol(normalized);
    }
#if defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_DECK) || defined(ARDUINO_WIO_TRACKER_L2)
    ::platform::esp::arduino_common::voice::vmp_session::invalidateContactSecretCache();
    ::platform::esp::arduino_common::voice::vmp_session::setPresentationProtocol(
        static_cast<uint8_t>(normalized));
#if defined(ARDUINO_LILYGO_LORA_LR1121)
    ::platform::esp::arduino_common::voice::vmp_session::setLxmfCarrierEnabled(
        chat::infra::isReticulumMeshProtocol(normalized));
#endif
#endif

    if (persist)
    {
        saveConfig(AppConfigChangeSet::mesh());
    }
    AppTasks::resumeRadioTasks();
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
    if (config_.mesh_protocol == chat::MeshProtocol::Reticulum)
    {
        input.configured_long_name = chat::lxmf::announceDisplayName(config_.node_name, config_.short_name);
    }
    input.configured_short_name = config_.short_name;
    input.fallback_long_prefix = "TM";
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
    ::platform::ui::service_quick_controls();
    flushConfigPersistence(millis());
#if defined(ARDUINO_T_LORA_PAGER) || defined(ARDUINO_T_DECK) || defined(ARDUINO_WIO_TRACKER_L2)
    // Text and VMP attachments share the deferred-storage readiness boundary.
    // After that boundary, this is a rate-limited retry only when an SD I/O
    // failure prevented the local VMP attachment snapshot from restoring.
    ::platform::esp::arduino_common::voice::vmp_session::servicePersistentInbox();
#endif
    if (event_runtime_hooks_.update_core_services)
    {
        event_runtime_hooks_.update_core_services(*this);
    }
    if (mesh_peer_directory_ && !deferred_storage_started_ &&
        !::platform::ui::reticulum_call::resource_preempt_active())
    {
        (void)mesh_peer_directory_->flush();
    }
    if (deferred_storage_started_ &&
        platform_bindings_.deferred_storage_ready &&
        ::platform::esp::arduino_common::storage::consume_hydration_ready())
    {
        platform_bindings_.deferred_storage_ready(*this);
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
#if defined(TRAIL_MATE_ENABLE_BLE) && TRAIL_MATE_ENABLE_BLE
    config_.ble_enabled = enabled;
    if (ble_manager_)
    {
        ble_manager_->setEnabled(enabled);
    }
#else
    (void)enabled;
    config_.ble_enabled = false;
#endif
    saveConfig();
}

chat::NodeId AppContext::getSelfNodeId() const
{
    return platform_bindings_.get_self_node_id ? platform_bindings_.get_self_node_id() : 0;
}

void AppContext::clearNodeDb()
{
    if (mesh_peer_directory_)
    {
        (void)mesh_peer_directory_->clearProtocol(config_.mesh_protocol);
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
