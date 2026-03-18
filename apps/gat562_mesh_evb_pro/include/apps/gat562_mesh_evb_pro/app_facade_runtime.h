#pragma once

#include "app/app_config.h"
#include "app/app_facades.h"
#include "chat/runtime/self_identity_policy.h"
#include "chat/runtime/self_identity_provider.h"

#include <memory>

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
}

namespace platform::nrf52::arduino_common
{
class SelfIdentityBridge;
}

namespace boards::gat562_mesh_evb_pro
{
class Gat562Board;
}

namespace apps::gat562_mesh_evb_pro
{

class AppFacadeRuntime final : public app::IAppBleFacade
{
  public:
    static AppFacadeRuntime& instance();
    ~AppFacadeRuntime();

    bool initialize();
    bool isInitialized() const;

    bool installMeshBackend(chat::MeshProtocol protocol,
                            std::unique_ptr<chat::IMeshAdapter> backend);

    app::AppConfig& getConfig() override;
    const app::AppConfig& getConfig() const override;
    void saveConfig() override;
    void applyMeshConfig() override;
    void applyUserInfo() override;
    void applyPositionConfig() override;
    void applyNetworkLimits() override;
    void applyPrivacyConfig() override;
    void applyChatDefaults() override;
    void getEffectiveUserInfo(char* out_long, std::size_t long_len,
                              char* out_short, std::size_t short_len) const override;
    bool switchMeshProtocol(chat::MeshProtocol protocol, bool persist = true) override;

    chat::ChatService& getChatService() override;
    chat::contacts::ContactService& getContactService() override;
    chat::IMeshAdapter* getMeshAdapter() override;
    const chat::IMeshAdapter* getMeshAdapter() const override;
    chat::NodeId getSelfNodeId() const override;

    team::TeamController* getTeamController() override;
    team::TeamPairingService* getTeamPairing() override;
    team::TeamService* getTeamService() override;
    const team::TeamService* getTeamService() const override;
    team::TeamTrackSampler* getTeamTrackSampler() override;
    void setTeamModeActive(bool active) override;

    void broadcastNodeInfo() override;
    void clearNodeDb() override;
    void clearMessageDb() override;

    ble::BleManager* getBleManager() override;
    const ble::BleManager* getBleManager() const override;
    bool isBleEnabled() const override;
    void setBleEnabled(bool enabled) override;
    chat::contacts::INodeStore* getNodeStore() override;
    const chat::contacts::INodeStore* getNodeStore() const override;
    void resetMeshConfig() override;
    chat::ui::IChatUiRuntime* getChatUiRuntime() override;
    void setChatUiRuntime(chat::ui::IChatUiRuntime* runtime) override;
    BoardBase* getBoard() override;
    const BoardBase* getBoard() const override;

    void updateCoreServices() override;
    void tickEventRuntime() override;
    void dispatchPendingEvents(std::size_t max_events = 32) override;

    const chat::runtime::EffectiveSelfIdentity& effectiveIdentity() const;

  private:
    AppFacadeRuntime();

    void initializeStores();
    void initializeChatRuntime();
    void refreshEffectiveIdentity();
    const chat::runtime::SelfIdentityProvider* identityProvider() const;

    bool initialized_ = false;
    app::AppConfig config_{};
    std::unique_ptr<platform::nrf52::arduino_common::SelfIdentityBridge> identity_bridge_;
    mutable chat::runtime::EffectiveSelfIdentity effective_identity_{};

    std::unique_ptr<chat::contacts::INodeStore> node_store_;
    std::unique_ptr<chat::contacts::IContactStore> contact_store_;
    std::unique_ptr<chat::contacts::ContactService> contact_service_;
    std::unique_ptr<chat::ChatModel> chat_model_;
    std::unique_ptr<chat::IChatStore> chat_store_;
    std::unique_ptr<chat::IMeshAdapter> mesh_router_;
    std::unique_ptr<chat::ChatService> chat_service_;
    std::unique_ptr<ble::BleManager> ble_manager_;
    boards::gat562_mesh_evb_pro::Gat562Board* board_ = nullptr;
    chat::ui::IChatUiRuntime* chat_ui_runtime_ = nullptr;
};

} // namespace apps::gat562_mesh_evb_pro
