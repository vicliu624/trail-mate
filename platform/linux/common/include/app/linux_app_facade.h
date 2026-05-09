#pragma once

#include "app/app_config.h"
#include "app/app_facades.h"

#include <memory>

namespace sys
{
struct Event;
}

namespace trailmate::cardputer_zero::linux_ui
{

using TeamUiEventDispatcher = bool (*)(sys::Event*);

void setTeamUiEventDispatcher(TeamUiEventDispatcher dispatcher);

class MinimalLinuxAppFacade final : public ::app::IAppFacade
{
  public:
    MinimalLinuxAppFacade();
    ~MinimalLinuxAppFacade() override;

    MinimalLinuxAppFacade(const MinimalLinuxAppFacade&) = delete;
    MinimalLinuxAppFacade& operator=(const MinimalLinuxAppFacade&) = delete;

    bool initialize();
    void shutdown();
    bool is_initialized() const noexcept;

    ::app::AppConfig& getConfig() override;
    const ::app::AppConfig& getConfig() const override;
    void saveConfig() override;
    void applyMeshConfig() override;
    void applyUserInfo() override;
    void applyPositionConfig() override;
    void applyNetworkLimits() override;
    void applyPrivacyConfig() override;
    void applyChatDefaults() override;
    ::chat::MeshProtocol getMeshProtocol() const override;
    void getEffectiveUserInfo(char* out_long,
                              std::size_t long_len,
                              char* out_short,
                              std::size_t short_len) const override;
    bool switchMeshProtocol(::chat::MeshProtocol protocol, bool persist = true) override;

    ::chat::ChatService& getChatService() override;
    ::chat::contacts::ContactService& getContactService() override;
    ::chat::IMeshAdapter* getMeshAdapter() override;
    const ::chat::IMeshAdapter* getMeshAdapter() const override;
    ::chat::NodeId getSelfNodeId() const override;

    ::team::TeamController* getTeamController() override;
    ::team::TeamPairingService* getTeamPairing() override;
    ::team::TeamService* getTeamService() override;
    const ::team::TeamService* getTeamService() const override;
    ::team::TeamTrackSampler* getTeamTrackSampler() override;
    void setTeamModeActive(bool active) override;

    void broadcastNodeInfo() override;
    void clearNodeDb() override;
    void clearMessageDb() override;

    ::ble::BleManager* getBleManager() override;
    const ::ble::BleManager* getBleManager() const override;
    bool isBleEnabled() const override;
    void setBleEnabled(bool enabled) override;
    void restartDevice() override;
    ::chat::ui::IChatUiRuntime* getChatUiRuntime() override;
    void setChatUiRuntime(::chat::ui::IChatUiRuntime* runtime) override;
    ::BoardBase* getBoard() override;
    const ::BoardBase* getBoard() const override;

    void updateCoreServices() override;
    void tickEventRuntime() override;
    void dispatchPendingEvents(std::size_t max_events = 32) override;

  private:
    struct Implementation;

    Implementation& impl();
    const Implementation& impl() const;
    void loadPersistedConfig();
    void seedDefaultIdentity();
    void syncLocalIdentity();
    void ensureServicesReady();

    ::app::AppConfig config_{};
    std::unique_ptr<Implementation> impl_;
    ::chat::ui::IChatUiRuntime* chat_ui_runtime_ = nullptr;
    bool initialized_ = false;
};

} // namespace trailmate::cardputer_zero::linux_ui
