#pragma once

#include "app/app_config.h"
#include "app/app_facades.h"
#include "app/linux_app_services.h"

#include <memory>

namespace sys
{
struct Event;
}
namespace ui::geocaching
{
class Source;
}

namespace platform::linux_runtime
{
class LinuxGeocachingSource;
}

namespace trailmate::cardputer_zero::linux_ui
{

using TeamUiEventDispatcher = bool (*)(sys::Event*);

void setTeamUiEventDispatcher(TeamUiEventDispatcher dispatcher);

class MinimalLinuxAppFacade final : public ::app::IAppFacade
{
  public:
    MinimalLinuxAppFacade();
    explicit MinimalLinuxAppFacade(::trailmate::linux_app::LinuxAppServicesOptions options);
    ~MinimalLinuxAppFacade() override;

    MinimalLinuxAppFacade(const MinimalLinuxAppFacade&) = delete;
    MinimalLinuxAppFacade& operator=(const MinimalLinuxAppFacade&) = delete;

    bool initialize();
    ::ui::geocaching::Source* geocachingSource();
    ::platform::linux_runtime::IVoiceSource* voiceSource();
    void shutdown();
    bool is_initialized() const noexcept;

  protected:
    const ::app::AppConfig& getConfig() const override;

  public:
    ::app::AppConfigEdit beginConfigEdit() override;
    void saveConfig() override;
    void saveConfig(::app::AppConfigChangeSet changes) override;
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
    void setPhonePairingPresenter(std::function<bool(const ::platform::linux_runtime::PhonePairingDisplay&)>);
    void restartDevice() override;
    ::chat::ui::IChatUiRuntime* getChatUiRuntime() override;
    void setChatUiRuntime(::chat::ui::IChatUiRuntime* runtime) override;
    ::BoardBase* getBoard() override;
    const ::BoardBase* getBoard() const override;

    void updateCoreServices() override;
    void tickEventRuntime() override;
    void dispatchPendingEvents(std::size_t max_events = 32) override;

  private:
    static void commitConfigEdit(void* context, ::app::AppConfigChangeSet changes);
    static void cancelConfigEdit(void* context);

    ::trailmate::linux_app::LinuxAppServices services_;
    std::unique_ptr<::platform::linux_runtime::LinuxGeocachingSource> geocaching_;
    ::chat::ui::IChatUiRuntime* chat_ui_runtime_ = nullptr;
};

} // namespace trailmate::cardputer_zero::linux_ui
