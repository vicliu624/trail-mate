#include "apps/gat562_mesh_evb_pro/app_facade_runtime.h"

#include "app/app_facade_access.h"
#include "apps/gat562_mesh_evb_pro/debug_console.h"
#include "apps/gat562_mesh_evb_pro/protocol_factory.h"
#include "apps/gat562_mesh_evb_pro/runtime_apply_service.h"
#include "boards/gat562_mesh_evb_pro/gat562_board.h"
#include "boards/gat562_mesh_evb_pro/settings_store.h"
#include "chat/domain/chat_model.h"
#include "chat/infra/mesh_adapter_router_core.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/infra/store/ram_store.h"
#include "chat/runtime/self_identity_provider.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "platform/nrf52/arduino_common/chat/infra/contact_store.h"
#include "platform/nrf52/arduino_common/chat/infra/meshtastic/meshtastic_radio_adapter.h"
#include "platform/nrf52/arduino_common/chat/infra/meshtastic/node_store.h"
#include "platform/nrf52/arduino_common/chat/infra/radio_packet_io.h"
#include "platform/nrf52/arduino_common/chat/infra/store/internal_fs_store.h"
#include "platform/nrf52/arduino_common/device_identity.h"
#include "platform/nrf52/arduino_common/self_identity_bridge.h"
#include "sys/clock.h"

#include <Arduino.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace apps::gat562_mesh_evb_pro
{
namespace
{
constexpr uint32_t kChatStoreFlushIntervalMs = 2000UL;
constexpr uint8_t kSkipApplyMesh = 1U << 0;
constexpr uint8_t kSkipApplyUser = 1U << 1;
constexpr uint8_t kSkipApplyPosition = 1U << 2;
constexpr uint8_t kSkipApplyNetwork = 1U << 3;
constexpr uint8_t kSkipApplyPrivacy = 1U << 4;
constexpr uint8_t kSkipApplyChatDefaults = 1U << 5;
constexpr uint8_t kSkipApplyMaskAll = kSkipApplyMesh | kSkipApplyUser | kSkipApplyPosition | kSkipApplyNetwork |
                                      kSkipApplyPrivacy | kSkipApplyChatDefaults;

class ScopedGpsSuspend
{
  public:
    explicit ScopedGpsSuspend(::boards::gat562_mesh_evb_pro::Gat562Board* board)
        : board_(board),
          resume_(board_ && board_->gpsEnabled())
    {
        if (resume_)
        {
            board_->suspendGps();
        }
    }

    ~ScopedGpsSuspend()
    {
        if (resume_ && board_)
        {
            board_->resumeGps();
        }
    }

    ScopedGpsSuspend(const ScopedGpsSuspend&) = delete;
    ScopedGpsSuspend& operator=(const ScopedGpsSuspend&) = delete;

  private:
    ::boards::gat562_mesh_evb_pro::Gat562Board* board_ = nullptr;
    bool resume_ = false;
};

platform::nrf52::arduino_common::chat::meshtastic::MeshtasticRadioAdapter* getMeshtasticBackend(chat::IMeshAdapter* adapter)
{
    if (!adapter)
    {
        return nullptr;
    }

    chat::IMeshAdapter* backend = adapter->backendForProtocol(chat::MeshProtocol::Meshtastic);
    return backend
               ? static_cast<platform::nrf52::arduino_common::chat::meshtastic::MeshtasticRadioAdapter*>(backend)
               : nullptr;
}

template <typename T>
void copyString(const char* src, T* dst, size_t dst_len)
{
    if (!dst || dst_len == 0)
    {
        return;
    }

    if (!src)
    {
        dst[0] = '\0';
        return;
    }

    const size_t copy_len = std::min(std::strlen(src), dst_len - 1);
    std::memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
}

bool buildNodePositionFromGpsState(const ::gps::GpsState& gps_state,
                                   ::chat::contacts::NodePosition* out_position)
{
    if (!out_position || !gps_state.valid)
    {
        return false;
    }

    ::chat::contacts::NodePosition position{};
    position.valid = true;
    position.latitude_i = static_cast<int32_t>(std::lround(gps_state.lat * 1e7));
    position.longitude_i = static_cast<int32_t>(std::lround(gps_state.lng * 1e7));
    position.has_altitude = gps_state.has_alt;
    position.altitude = gps_state.has_alt ? static_cast<int32_t>(std::lround(gps_state.alt_m)) : 0;
    position.timestamp = ::sys::epoch_seconds_now();
    *out_position = position;
    return true;
}

} // namespace

AppFacadeRuntime& AppFacadeRuntime::instance()
{
    static AppFacadeRuntime runtime;
    return runtime;
}

AppFacadeRuntime::AppFacadeRuntime()
    : apply_service_(new RuntimeApplyService())
{
}

AppFacadeRuntime::~AppFacadeRuntime() = default;

bool AppFacadeRuntime::initialize()
{
    if (initialized_)
    {
        return true;
    }

    board_ = &::boards::gat562_mesh_evb_pro::Gat562Board::instance();
    if (board_)
    {
        (void)board_->begin();
    }
    (void)::boards::gat562_mesh_evb_pro::settings_store::loadAppConfig(config_);
    ::boards::gat562_mesh_evb_pro::settings_store::normalizeConfig(config_);
    initializeStores();
    const chat::NodeId resolved_self_node_id = resolveSelfNodeId();
    platform::nrf52::arduino_common::device_identity::setResolvedSelfNodeId(resolved_self_node_id);
    identity_bridge_ = std::unique_ptr<platform::nrf52::arduino_common::SelfIdentityBridge>(
        new platform::nrf52::arduino_common::SelfIdentityBridge(config_,
                                                                NRF_FICR->DEVICEADDR[0],
                                                                NRF_FICR->DEVICEADDR[1],
                                                                board_->defaultLongName(),
                                                                board_->defaultShortName()));
    identity_bridge_->setNodeId(resolved_self_node_id);
    refreshEffectiveIdentity();
    initializeChatRuntime();

    app::bindAppFacade(*this);
    ble_manager_ = std::unique_ptr<ble::BleManager>(new ble::BleManager(*this));
    if (config_.ble_enabled && ble_manager_)
    {
        ble_manager_->begin();
    }
    initialized_ = true;
    debug_console::printf("[gat562] app facade ready node=%08lX\n",
                          static_cast<unsigned long>(effective_identity_.node_id));
    return true;
}

bool AppFacadeRuntime::isInitialized() const
{
    return initialized_;
}

bool AppFacadeRuntime::installMeshBackend(chat::MeshProtocol protocol,
                                          std::unique_ptr<chat::IMeshAdapter> backend)
{
    if (!mesh_router_ || !backend)
    {
        return false;
    }

    if (!mesh_router_->installBackend(protocol, std::move(backend)))
    {
        return false;
    }

    if (protocol == config_.mesh_protocol)
    {
        applyMeshConfig();
        applyUserInfo();
        applyNetworkLimits();
        applyPrivacyConfig();
    }
    return true;
}

void AppFacadeRuntime::initializeStores()
{
    if (node_store_ && contact_store_ && contact_service_)
    {
        return;
    }

    auto node_store = std::unique_ptr<platform::nrf52::arduino_common::chat::meshtastic::NodeStore>(
        new platform::nrf52::arduino_common::chat::meshtastic::NodeStore());
    auto contact_store = std::unique_ptr<platform::nrf52::arduino_common::chat::infra::ContactStore>(
        new platform::nrf52::arduino_common::chat::infra::ContactStore());
    platform::nrf52::arduino_common::chat::infra::ContactStore* contact_store_ptr = contact_store.get();
    node_store->setProtectedNodeChecker([contact_store_ptr](uint32_t node_id)
                                        { return contact_store_ptr && contact_store_ptr->hasContactNode(node_id); });
    node_store_ = std::move(node_store);
    contact_store_ = std::move(contact_store);
    contact_service_ = std::unique_ptr<chat::contacts::ContactService>(
        new chat::contacts::ContactService(*node_store_, *contact_store_));

    if (node_store_)
    {
        node_store_->begin();
    }
    if (contact_store_)
    {
        contact_store_->begin();
    }
    if (contact_service_)
    {
        contact_service_->begin();
    }
}

void AppFacadeRuntime::initializeChatRuntime()
{
    chat_model_ = std::unique_ptr<chat::ChatModel>(new chat::ChatModel());
    chat_store_ = std::unique_ptr<chat::IChatStore>(
        new platform::nrf52::arduino_common::chat::infra::store::InternalFsStore());
    mesh_router_ = std::unique_ptr<chat::IMeshAdapter>(new chat::MeshAdapterRouterCore());
    chat_service_ = std::unique_ptr<chat::ChatService>(
        new chat::ChatService(*chat_model_, *mesh_router_, *chat_store_, config_.mesh_protocol));

    if (chat_model_)
    {
        chat_model_->setPolicy(config_.chat_policy);
    }

    (void)installMeshBackend(chat::MeshProtocol::Meshtastic,
                             createProtocolAdapter(chat::MeshProtocol::Meshtastic,
                                                   identityProvider(),
                                                   static_cast<platform::nrf52::arduino_common::chat::meshtastic::NodeStore*>(node_store_.get()),
                                                   contact_service_.get()));
    (void)installMeshBackend(chat::MeshProtocol::MeshCore,
                             createProtocolAdapter(chat::MeshProtocol::MeshCore, identityProvider()));

    applyMeshConfig();
    applyUserInfo();
    applyNetworkLimits();
    applyPrivacyConfig();
    applyChatDefaults();
}

void AppFacadeRuntime::refreshEffectiveIdentity()
{
    effective_identity_ = chat::runtime::EffectiveSelfIdentity{};
    if (!identity_bridge_)
    {
        return;
    }

    chat::runtime::SelfIdentityInput input{};
    if (!identity_bridge_->readSelfIdentityInput(&input))
    {
        return;
    }

    (void)chat::runtime::resolveEffectiveSelfIdentity(input, &effective_identity_);
}

const chat::runtime::SelfIdentityProvider* AppFacadeRuntime::identityProvider() const
{
    return identity_bridge_.get();
}

app::AppConfig& AppFacadeRuntime::getConfig()
{
    return config_;
}

const app::AppConfig& AppFacadeRuntime::getConfig() const
{
    return config_;
}

void AppFacadeRuntime::saveConfig()
{
    ScopedGpsSuspend suspend_gps(board_);
    clearPostSaveApplySkips();
    debug_console::printf("[gat562][cfg] save start proto=%u ok_to_mqtt=%u ignore_mqtt=%u ble=%u\n",
                          static_cast<unsigned>(config_.mesh_protocol),
                          config_.meshtastic_config.config_ok_to_mqtt ? 1U : 0U,
                          config_.meshtastic_config.ignore_mqtt ? 1U : 0U,
                          config_.ble_enabled ? 1U : 0U);
    ::boards::gat562_mesh_evb_pro::settings_store::normalizeConfig(config_);
    debug_console::printf("[gat562][cfg] save post-normalize ok_to_mqtt=%u ignore_mqtt=%u\n",
                          config_.meshtastic_config.config_ok_to_mqtt ? 1U : 0U,
                          config_.meshtastic_config.ignore_mqtt ? 1U : 0U);
    refreshEffectiveIdentity();
    debug_console::printf("[gat562][cfg] save post-identity\n");
    applyMeshConfig();
    debug_console::printf("[gat562][cfg] save post-applyMesh\n");
    applyUserInfo();
    debug_console::printf("[gat562][cfg] save post-applyUser\n");
    applyPositionConfig();
    debug_console::printf("[gat562][cfg] save post-applyPos\n");
    applyNetworkLimits();
    debug_console::printf("[gat562][cfg] save post-applyLimits\n");
    applyPrivacyConfig();
    debug_console::printf("[gat562][cfg] save post-applyPrivacy\n");
    applyChatDefaults();
    markPostSaveApplySkips(kSkipApplyMaskAll);
    ::boards::gat562_mesh_evb_pro::settings_store::queueSaveAppConfig(config_);
    config_save_pending_ = true;
    debug_console::printf("[gat562][cfg] save deferred-store queued\n");
}

void AppFacadeRuntime::applyMeshConfig()
{
    if (consumePostSaveApplySkip(kSkipApplyMesh, "applyMesh"))
    {
        return;
    }
    if (apply_service_)
    {
        apply_service_->applyMesh(config_,
                                  mesh_router_.get(),
                                  chat_service_.get(),
                                  ble_manager_.get(),
                                  board_);
    }
}

void AppFacadeRuntime::applyUserInfo()
{
    if (consumePostSaveApplySkip(kSkipApplyUser, "applyUser"))
    {
        return;
    }
    const chat::runtime::EffectiveSelfIdentity previous_identity = effective_identity_;
    refreshEffectiveIdentity();
    if (apply_service_)
    {
        apply_service_->applyUserInfo(previous_identity,
                                      effective_identity_,
                                      mesh_router_.get(),
                                      ble_manager_.get());
    }
}

void AppFacadeRuntime::applyPositionConfig()
{
    if (consumePostSaveApplySkip(kSkipApplyPosition, "applyPos"))
    {
        return;
    }
    if (apply_service_)
    {
        apply_service_->applyPosition(config_, board_);
    }
}

chat::NodeId AppFacadeRuntime::resolveSelfNodeId() const
{
    return platform::nrf52::arduino_common::device_identity::resolveNodeId(
        NRF_FICR->DEVICEADDR[0],
        NRF_FICR->DEVICEADDR[1],
        NRF_FICR->DEVICEID[0],
        NRF_FICR->DEVICEID[1],
        node_store_.get());
}

void AppFacadeRuntime::applyNetworkLimits()
{
    if (consumePostSaveApplySkip(kSkipApplyNetwork, "applyLimits"))
    {
        return;
    }
    if (mesh_router_)
    {
        mesh_router_->setNetworkLimits(config_.net_duty_cycle, config_.net_channel_util);
    }
}

void AppFacadeRuntime::applyPrivacyConfig()
{
    if (consumePostSaveApplySkip(kSkipApplyPrivacy, "applyPrivacy"))
    {
        return;
    }
    if (mesh_router_)
    {
        mesh_router_->setPrivacyConfig(config_.privacy_encrypt_mode, config_.privacy_pki);
    }
}

void AppFacadeRuntime::applyChatDefaults()
{
    if (consumePostSaveApplySkip(kSkipApplyChatDefaults, "applyChatDefaults"))
    {
        return;
    }
    if (!chat_service_)
    {
        return;
    }

    const chat::ChannelId channel = (config_.chat_channel == 1)
                                        ? chat::ChannelId::SECONDARY
                                        : chat::ChannelId::PRIMARY;
    chat_service_->switchChannel(channel);
}

void AppFacadeRuntime::getEffectiveUserInfo(char* out_long, std::size_t long_len,
                                            char* out_short, std::size_t short_len) const
{
    const char* long_name = effective_identity_.long_name[0] != '\0'
                                ? effective_identity_.long_name
                                : (board_ ? board_->defaultLongName() : "");
    const char* short_name = effective_identity_.short_name[0] != '\0'
                                 ? effective_identity_.short_name
                                 : (board_ ? board_->defaultShortName() : "");
    copyString(long_name, out_long, long_len);
    copyString(short_name, out_short, short_len);
}

bool AppFacadeRuntime::switchMeshProtocol(chat::MeshProtocol protocol, bool persist)
{
    if (!chat::infra::isValidMeshProtocol(protocol))
    {
        return false;
    }

    ScopedGpsSuspend suspend_gps(board_);
    const bool protocol_changed = (config_.mesh_protocol != protocol);
    debug_console::printf("[gat562][cfg] switch proto=%u persist=%u changed=%u\n",
                          static_cast<unsigned>(protocol),
                          persist ? 1U : 0U,
                          protocol_changed ? 1U : 0U);
    config_.mesh_protocol = protocol;
    ::boards::gat562_mesh_evb_pro::settings_store::cacheAppConfig(config_);

    if (persist)
    {
        ::boards::gat562_mesh_evb_pro::settings_store::normalizeConfig(config_);
        if (::boards::gat562_mesh_evb_pro::settings_store::saveAppConfig(config_))
        {
            config_save_pending_ = ::boards::gat562_mesh_evb_pro::settings_store::hasDeferredSavePending();
            debug_console::printf("[gat562][cfg] switch persist save=ok deferred=%u\n",
                                  config_save_pending_ ? 1U : 0U);
            if (protocol_changed)
            {
                debug_console::printf("[gat562][cfg] switch persist rebooting for proto=%u\n",
                                      static_cast<unsigned>(protocol));
                restartDevice();
            }
        }
        else
        {
            ::boards::gat562_mesh_evb_pro::settings_store::queueSaveAppConfig(config_);
            config_save_pending_ = true;
            debug_console::printf("[gat562][cfg] switch persist save=deferred\n");
        }
    }

    if (protocol_changed)
    {
        applyMeshConfig();
        applyUserInfo();
        applyNetworkLimits();
        applyPrivacyConfig();
    }
    return true;
}

chat::ChatService& AppFacadeRuntime::getChatService()
{
    return *chat_service_;
}

chat::contacts::ContactService& AppFacadeRuntime::getContactService()
{
    return *contact_service_;
}

chat::IMeshAdapter* AppFacadeRuntime::getMeshAdapter()
{
    return mesh_router_.get();
}

const chat::IMeshAdapter* AppFacadeRuntime::getMeshAdapter() const
{
    return mesh_router_.get();
}

chat::NodeId AppFacadeRuntime::getSelfNodeId() const
{
    return effective_identity_.node_id;
}

team::TeamController* AppFacadeRuntime::getTeamController()
{
    return nullptr;
}

team::TeamPairingService* AppFacadeRuntime::getTeamPairing()
{
    return nullptr;
}

team::TeamService* AppFacadeRuntime::getTeamService()
{
    return nullptr;
}

const team::TeamService* AppFacadeRuntime::getTeamService() const
{
    return nullptr;
}

team::TeamTrackSampler* AppFacadeRuntime::getTeamTrackSampler()
{
    return nullptr;
}

void AppFacadeRuntime::setTeamModeActive(bool active)
{
    (void)active;
}

void AppFacadeRuntime::broadcastNodeInfo()
{
    if (mesh_router_)
    {
        (void)mesh_router_->requestNodeInfo(0xFFFFFFFFUL, false);
    }
}

void AppFacadeRuntime::clearNodeDb()
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

void AppFacadeRuntime::clearMessageDb()
{
    if (chat_service_)
    {
        chat_service_->clearAllMessages();
    }
}

ble::BleManager* AppFacadeRuntime::getBleManager()
{
    return ble_manager_.get();
}

const ble::BleManager* AppFacadeRuntime::getBleManager() const
{
    return ble_manager_.get();
}

bool AppFacadeRuntime::isBleEnabled() const
{
    return config_.ble_enabled;
}

void AppFacadeRuntime::setBleEnabled(bool enabled)
{
    if (config_.ble_enabled == enabled)
    {
        if (ble_manager_)
        {
            ble_manager_->setEnabled(enabled);
        }
        return;
    }

    config_.ble_enabled = enabled;
    if (ble_manager_)
    {
        ble_manager_->setEnabled(enabled);
    }
    ::boards::gat562_mesh_evb_pro::settings_store::queueSaveAppConfig(config_);
    config_save_pending_ = true;
}

void AppFacadeRuntime::restartDevice()
{
    NVIC_SystemReset();
}

chat::contacts::INodeStore* AppFacadeRuntime::getNodeStore()
{
    return node_store_.get();
}

const chat::contacts::INodeStore* AppFacadeRuntime::getNodeStore() const
{
    return node_store_.get();
}

bool AppFacadeRuntime::getDeviceMacAddress(uint8_t out_mac[6]) const
{
    if (!out_mac)
    {
        return false;
    }

    const auto mac = platform::nrf52::arduino_common::device_identity::getSelfMacAddress();
    std::copy(mac.begin(), mac.end(), out_mac);
    return true;
}

bool AppFacadeRuntime::syncCurrentEpochSeconds(uint32_t epoch_seconds)
{
    if (!board_ || epoch_seconds == 0)
    {
        return false;
    }

    board_->setCurrentEpochSeconds(epoch_seconds);
    return true;
}

void AppFacadeRuntime::resetMeshConfig()
{
    if (config_.mesh_protocol == chat::MeshProtocol::MeshCore)
    {
        config_.meshcore_config = chat::MeshConfig();
        config_.applyMeshCoreFactoryDefaults();
    }
    else
    {
        config_.meshtastic_config = chat::MeshConfig();
        config_.meshtastic_config.region = app::AppConfig::kDefaultRegionCode;
    }
    saveConfig();
    applyMeshConfig();
}

chat::ui::IChatUiRuntime* AppFacadeRuntime::getChatUiRuntime()
{
    return chat_ui_runtime_;
}

void AppFacadeRuntime::setChatUiRuntime(chat::ui::IChatUiRuntime* runtime)
{
    chat_ui_runtime_ = runtime;
}

BoardBase* AppFacadeRuntime::getBoard()
{
    return board_;
}

const BoardBase* AppFacadeRuntime::getBoard() const
{
    return board_;
}

void AppFacadeRuntime::updateCoreServices()
{
    clearPostSaveApplySkips();
    syncSelfPositionFromGps();
    if (chat_service_)
    {
        chat_service_->processIncoming();
        const uint32_t now_ms = ::sys::millis_now();
        if ((now_ms - last_chat_store_flush_ms_) >= kChatStoreFlushIntervalMs)
        {
            chat_service_->flushStore();
            if (node_store_)
            {
                (void)node_store_->flush();
            }
            if (auto* mt = getMeshtasticBackend(getMeshAdapter()))
            {
                mt->flushDeferredPersistence(false);
            }
            last_chat_store_flush_ms_ = now_ms;
        }
    }
    if (ble_manager_)
    {
        ble_manager_->update();
    }
}

bool AppFacadeRuntime::consumePostSaveApplySkip(uint8_t bit, const char* label)
{
    if ((post_save_apply_skip_mask_ & bit) == 0)
    {
        return false;
    }

    post_save_apply_skip_mask_ &= static_cast<uint8_t>(~bit);
    debug_console::printf("[gat562][cfg] %s skipped: already applied in save\n",
                          label ? label : "apply");
    return true;
}

void AppFacadeRuntime::markPostSaveApplySkips(uint8_t mask)
{
    post_save_apply_skip_mask_ |= mask;
}

void AppFacadeRuntime::clearPostSaveApplySkips()
{
    post_save_apply_skip_mask_ = 0;
}

void AppFacadeRuntime::syncSelfPositionFromGps()
{
    if (!contact_service_ || !board_ || effective_identity_.node_id == 0)
    {
        return;
    }

    ::chat::contacts::NodePosition position{};
    if (!buildNodePositionFromGpsState(board_->gpsData(), &position))
    {
        return;
    }

    const ::chat::contacts::NodeInfo* existing = contact_service_->getNodeInfo(effective_identity_.node_id);
    if (existing && existing->position.valid &&
        existing->position.latitude_i == position.latitude_i &&
        existing->position.longitude_i == position.longitude_i &&
        existing->position.has_altitude == position.has_altitude &&
        existing->position.altitude == position.altitude)
    {
        return;
    }

    contact_service_->updateNodePosition(effective_identity_.node_id, position);
}

void AppFacadeRuntime::tickEventRuntime()
{
    if (!config_save_pending_)
    {
        return;
    }

    if (!::boards::gat562_mesh_evb_pro::settings_store::hasDeferredSavePending())
    {
        config_save_pending_ = false;
        return;
    }

    const bool flushed = ::boards::gat562_mesh_evb_pro::settings_store::tickDeferredSave();
    if (flushed)
    {
        debug_console::printf("[gat562][cfg] deferred-store flush ok\n");
    }
    config_save_pending_ = ::boards::gat562_mesh_evb_pro::settings_store::hasDeferredSavePending();
}

void AppFacadeRuntime::dispatchPendingEvents(std::size_t max_events)
{
    (void)max_events;
}

const app::AppConfig& AppFacadeRuntime::bleConfig() const
{
    return config_;
}

bool AppFacadeRuntime::bleEnabled() const
{
    return isBleEnabled();
}

void AppFacadeRuntime::bleEffectiveUserInfo(char* out_long, std::size_t long_len,
                                            char* out_short, std::size_t short_len) const
{
    getEffectiveUserInfo(out_long, long_len, out_short, short_len);
}

chat::NodeId AppFacadeRuntime::bleSelfNodeId() const
{
    return getSelfNodeId();
}

app::IAppBleFacade& AppFacadeRuntime::bleAppFacade()
{
    return *this;
}

const chat::runtime::EffectiveSelfIdentity& AppFacadeRuntime::effectiveIdentity() const
{
    return effective_identity_;
}

} // namespace apps::gat562_mesh_evb_pro
