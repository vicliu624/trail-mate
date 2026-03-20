#include "apps/gat562_mesh_evb_pro/app_facade_runtime.h"

#include "app/app_facade_access.h"
#include "apps/gat562_mesh_evb_pro/debug_console.h"
#include "apps/gat562_mesh_evb_pro/protocol_factory.h"
#include "ble/ble_manager.h"
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
#include "platform/nrf52/arduino_common/chat/infra/meshtastic/node_store.h"
#include "platform/nrf52/arduino_common/chat/infra/radio_packet_io.h"
#include "platform/nrf52/arduino_common/chat/infra/store/internal_fs_store.h"
#include "platform/nrf52/arduino_common/device_identity.h"
#include "platform/nrf52/arduino_common/self_identity_bridge.h"

#include <Arduino.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace apps::gat562_mesh_evb_pro
{
namespace
{

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

const char* protocolLabel(chat::MeshProtocol protocol)
{
    return protocol == chat::MeshProtocol::MeshCore ? "MC" : "MT";
}

} // namespace

AppFacadeRuntime& AppFacadeRuntime::instance()
{
    static AppFacadeRuntime runtime;
    return runtime;
}

AppFacadeRuntime::AppFacadeRuntime() = default;

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
                                                   static_cast<platform::nrf52::arduino_common::chat::meshtastic::NodeStore*>(node_store_.get())));
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
    ::boards::gat562_mesh_evb_pro::settings_store::normalizeConfig(config_);
    (void)::boards::gat562_mesh_evb_pro::settings_store::saveAppConfig(config_);
    refreshEffectiveIdentity();
    applyMeshConfig();
    applyUserInfo();
    applyPositionConfig();
    applyNetworkLimits();
    applyPrivacyConfig();
    applyChatDefaults();
}

void AppFacadeRuntime::applyMeshConfig()
{
    ::boards::gat562_mesh_evb_pro::settings_store::normalizeConfig(config_);
    if (mesh_router_)
    {
        auto* router = static_cast<chat::MeshAdapterRouterCore*>(mesh_router_.get());
        router->setActiveProtocol(config_.mesh_protocol);
    }
    if (mesh_router_)
    {
        mesh_router_->applyConfig(config_.activeMeshConfig());
    }
    if (board_)
    {
        board_->applyRadioConfig(config_.mesh_protocol, config_.activeMeshConfig());
    }
    if (chat_service_)
    {
        chat_service_->setActiveProtocol(config_.mesh_protocol);
    }
    if (ble_manager_)
    {
        ble_manager_->applyProtocol(config_.mesh_protocol);
    }

    const chat::MeshConfig& mesh = config_.activeMeshConfig();
    debug_console::printf("[gat562] radio cfg %s region=%u preset=%u ch=%u tx=%d hop=%u\n",
                          protocolLabel(config_.mesh_protocol),
                          static_cast<unsigned>(mesh.region),
                          static_cast<unsigned>(mesh.modem_preset),
                          static_cast<unsigned>(mesh.channel_num),
                          static_cast<int>(mesh.tx_power),
                          static_cast<unsigned>(mesh.hop_limit));
}

void AppFacadeRuntime::applyUserInfo()
{
    refreshEffectiveIdentity();
    if (mesh_router_)
    {
        mesh_router_->setUserInfo(effective_identity_.long_name,
                                  effective_identity_.short_name);
    }
    if (ble_manager_ && ble_manager_->isEnabled())
    {
        ble_manager_->setEnabled(false);
        ble_manager_->setEnabled(true);
    }
}

void AppFacadeRuntime::applyPositionConfig()
{
    if (board_)
    {
        board_->applyGpsConfig(config_);
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
    if (mesh_router_)
    {
        mesh_router_->setNetworkLimits(config_.net_duty_cycle, config_.net_channel_util);
    }
}

void AppFacadeRuntime::applyPrivacyConfig()
{
    if (mesh_router_)
    {
        mesh_router_->setPrivacyConfig(config_.privacy_encrypt_mode, config_.privacy_pki);
    }
}

void AppFacadeRuntime::applyChatDefaults()
{
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

    config_.mesh_protocol = protocol;
    applyMeshConfig();
    applyUserInfo();
    applyNetworkLimits();
    applyPrivacyConfig();

    if (persist)
    {
        saveConfig();
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
    (void)::boards::gat562_mesh_evb_pro::settings_store::saveAppConfig(config_);
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
    if (chat_service_)
    {
        chat_service_->processIncoming();
    }
    if (ble_manager_)
    {
        ble_manager_->update();
    }
}

void AppFacadeRuntime::tickEventRuntime()
{
}

void AppFacadeRuntime::dispatchPendingEvents(std::size_t max_events)
{
    (void)max_events;
}

const chat::runtime::EffectiveSelfIdentity& AppFacadeRuntime::effectiveIdentity() const
{
    return effective_identity_;
}

} // namespace apps::gat562_mesh_evb_pro
