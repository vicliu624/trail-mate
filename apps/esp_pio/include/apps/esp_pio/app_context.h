/**
 * @file app_context.h
 * @brief Application context (dependency injection)
 */

#pragma once

#include "app/app_config.h"
#include "app/app_context_platform_bindings.h"
#include "app/app_event_runtime.h"
#include "app/app_facades.h"
#include <cstddef>
#include <cstdint>
#include <memory>

class BoardBase;
class GpsBoard;
class LoraBoard;
class MotionBoard;

namespace ble
{
class BleManager;
}

namespace chat
{
class ChatModel;
class ChatService;
class IChatStore;
class IMeshAdapter;
namespace contacts
{
class INodeStore;
class IContactStore;
class ContactService;
} // namespace contacts
namespace ui
{
class IChatUiRuntime;
class GlobalChatUiRuntime;
} // namespace ui
} // namespace chat

namespace team
{
class TeamController;
class TeamPairingService;
class TeamService;
class TeamTrackSampler;
class ITeamCrypto;
class ITeamEventSink;
class ITeamRuntime;
class ITeamTrackSource;
class ITeamPairingTransport;
class ITeamPairingEventSink;
} // namespace team

namespace app
{

/**
 * @brief Application context
 * Manages all dependencies and provides singleton access
 */
class AppContext final : public IAppBleFacade
{
  public:
    static AppContext& getInstance();

    bool init(BoardBase& board, LoraBoard* lora_board = nullptr, GpsBoard* gps_board = nullptr,
              MotionBoard* motion_board = nullptr, bool use_mock_adapter = true, uint32_t disable_hw_init = 0);

    chat::ChatService& getChatService() override
    {
        return *chat_service_;
    }

    chat::contacts::ContactService& getContactService() override
    {
        return *contact_service_;
    }
    chat::ui::IChatUiRuntime* getChatUiRuntime() override;

    void setChatUiRuntime(chat::ui::IChatUiRuntime* runtime) override;

    team::TeamController* getTeamController() override
    {
        return team_controller_.get();
    }

    team::TeamPairingService* getTeamPairing() override
    {
        return team_pairing_service_.get();
    }

    team::TeamService* getTeamService() override
    {
        return team_service_.get();
    }

    const team::TeamService* getTeamService() const override
    {
        return team_service_.get();
    }

    team::TeamTrackSampler* getTeamTrackSampler() override
    {
        return team_track_sampler_.get();
    }

    void setTeamModeActive(bool active) override
    {
        if (platform_bindings_.set_team_mode_active)
        {
            platform_bindings_.set_team_mode_active(active);
        }
    }

    AppConfig& getConfig() override
    {
        return config_;
    }

    const AppConfig& getConfig() const override
    {
        return config_;
    }

    chat::NodeId getSelfNodeId() const override;

    chat::IMeshAdapter* getMeshAdapter() override;

    const chat::IMeshAdapter* getMeshAdapter() const override;

    LoraBoard* getLoraBoard()
    {
        return lora_board_;
    }

    const LoraBoard* getLoraBoard() const
    {
        return lora_board_;
    }

    BoardBase* getBoard() override
    {
        return board_;
    }

    const BoardBase* getBoard() const override
    {
        return board_;
    }

    chat::contacts::INodeStore* getNodeStore() override
    {
        return node_store_.get();
    }

    const chat::contacts::INodeStore* getNodeStore() const override
    {
        return node_store_.get();
    }

    bool getDeviceMacAddress(uint8_t out_mac[6]) const override;
    bool syncCurrentEpochSeconds(uint32_t epoch_seconds) override;
    void restartDevice() override;

    chat::contacts::IContactStore* getContactStore()
    {
        return contact_store_.get();
    }

    const chat::contacts::IContactStore* getContactStore() const
    {
        return contact_store_.get();
    }

    void saveConfig() override;

    void applyMeshConfig() override;
    void applyUserInfo() override;

    /** Apply position/GPS config to GpsService (interval, GNSS mode). Call after changing config_.gps_* */
    void applyPositionConfig() override;

    void broadcastNodeInfo() override;

    void getEffectiveUserInfo(char* out_long, size_t long_len,
                              char* out_short, size_t short_len) const override;

    void applyNetworkLimits() override;
    void applyPrivacyConfig() override;

    /**
     * @brief Switch active mesh protocol backend at runtime
     * @param protocol Target protocol backend
     * @param persist Save config to NVS if true
     * @return true on success
     */
    bool switchMeshProtocol(chat::MeshProtocol protocol, bool persist = true) override;

    void applyChatDefaults() override
    {
        if (chat_service_)
        {
            chat::ChannelId ch = (config_.chat_channel == 1) ? chat::ChannelId::SECONDARY
                                                             : chat::ChannelId::PRIMARY;
            chat_service_->switchChannel(ch);
        }
    }

    /**
     * @brief Reset mesh config to defaults and apply
     */
    void resetMeshConfig() override
    {
        if (config_.mesh_protocol == chat::MeshProtocol::MeshCore)
        {
            config_.meshcore_config = chat::MeshConfig();
            config_.applyMeshCoreFactoryDefaults();
        }
        else
        {
            config_.meshtastic_config = chat::MeshConfig();
            config_.meshtastic_config.region = AppConfig::kDefaultRegionCode;
        }
        saveConfig();
        applyMeshConfig();
    }

    void clearNodeDb() override;
    void clearMessageDb() override;
    void updateCoreServices() override;
    void tickEventRuntime() override;
    void dispatchPendingEvents(size_t max_events = 32) override;

    void attachEventRuntimeHooks(const AppEventRuntimeHooks& hooks);
    void configurePlatformBindings(const AppContextPlatformBindings& bindings);

    ble::BleManager* getBleManager() override
    {
        return ble_manager_.get();
    }

    const ble::BleManager* getBleManager() const override
    {
        return ble_manager_.get();
    }

    void attachBleManager(std::unique_ptr<ble::BleManager> ble_manager);

    void setBleEnabled(bool enabled) override;
    bool isBleEnabled() const override;

  private:
    AppContext();
    ~AppContext();
    AppContext(const AppContext&) = delete;
    AppContext& operator=(const AppContext&) = delete;

    void assignBoards(BoardBase& board, LoraBoard* lora_board, GpsBoard* gps_board,
                      MotionBoard* motion_board);
    void initGpsRuntime(uint32_t disable_hw_init);
    void initTrackRecorder();
    std::unique_ptr<chat::IMeshAdapter> createMeshBackend(chat::MeshProtocol protocol) const;
    void initChatRuntime(bool use_mock_adapter);
    void initTeamServices();
    void initContactServices();

    std::unique_ptr<chat::ChatModel> chat_model_;

    std::unique_ptr<chat::IChatStore> chat_store_;
    std::unique_ptr<chat::IMeshAdapter> mesh_router_;
    std::unique_ptr<chat::contacts::INodeStore> node_store_;
    std::unique_ptr<chat::contacts::IContactStore> contact_store_;

    std::unique_ptr<chat::ChatService> chat_service_;
    std::unique_ptr<chat::contacts::ContactService> contact_service_;
    std::unique_ptr<chat::ChatService::IncomingMessageObserver> chat_event_bus_bridge_;
    std::unique_ptr<team::ITeamCrypto> team_crypto_;
    std::unique_ptr<team::ITeamEventSink> team_event_sink_;
    std::unique_ptr<team::TeamService::UnhandledAppDataObserver> team_app_data_bridge_;
    std::unique_ptr<team::ITeamPairingEventSink> team_pairing_event_sink_;
    std::unique_ptr<team::ITeamRuntime> team_runtime_;
    std::unique_ptr<team::ITeamTrackSource> team_track_source_;
    std::unique_ptr<team::TeamService> team_service_;
    std::unique_ptr<team::TeamController> team_controller_;
    std::unique_ptr<team::TeamTrackSampler> team_track_sampler_;
    std::unique_ptr<team::ITeamPairingTransport> team_pairing_transport_;
    std::unique_ptr<team::TeamPairingService> team_pairing_service_;

    std::unique_ptr<chat::ui::GlobalChatUiRuntime> chat_ui_runtime_proxy_;
    std::unique_ptr<ble::BleManager> ble_manager_;
    AppEventRuntimeHooks event_runtime_hooks_{};

    AppConfig config_;
    AppContextPlatformBindings platform_bindings_{};

    BoardBase* board_ = nullptr;
    LoraBoard* lora_board_ = nullptr;
    GpsBoard* gps_board_ = nullptr;
    MotionBoard* motion_board_ = nullptr;
};

} // namespace app
