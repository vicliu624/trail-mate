#pragma once

#include <cstddef>
#include <memory>

#include "app/app_config.h"
#include "chat/domain/chat_types.h"
#include "platform/linux/runtime_mode.h"

namespace chat
{
class ChatService;
class IMeshAdapter;
namespace contacts
{
class ContactService;
}
} // namespace chat

namespace team
{
class TeamController;
class TeamPairingService;
class TeamService;
class TeamTrackSampler;
} // namespace team

namespace sys
{
struct Event;
}

namespace trailmate::linux_app
{

using UiEventDispatcher = bool (*)(void* context, ::sys::Event* event);

struct LinuxAppServicesOptions
{
    const char* default_node_name = "Trail Mate Linux";
    const char* default_short_name = "TL";
    const char* demo_broadcast_text = "Broadcast: Linux local mesh online.";
    platform::linux_runtime::LinuxRuntimeMode runtime_mode =
        platform::linux_runtime::resolve_runtime_mode();
    bool ble_supported = false;
};

// UI-independent Linux application service entrypoint.
//
// This is intentionally narrower than the legacy IAppFacade adapter: desktop
// shells should depend on app services and presentation models, not on compact
// LVGL page contracts.
class LinuxAppServices final
{
  public:
    explicit LinuxAppServices(LinuxAppServicesOptions options = {});
    ~LinuxAppServices();

    LinuxAppServices(const LinuxAppServices&) = delete;
    LinuxAppServices& operator=(const LinuxAppServices&) = delete;

    bool initialize();
    void shutdown();
    [[nodiscard]] bool isInitialized() const noexcept;

    void setUiEventDispatcher(UiEventDispatcher dispatcher,
                              void* context = nullptr) noexcept;
    bool dispatchUiEvent(::sys::Event* event);

    void tick(std::size_t max_events = 32);
    void updateCoreServices();
    void tickEventRuntime();
    void dispatchPendingEvents(std::size_t max_events = 32);

    [[nodiscard]] ::app::AppConfig& config();
    [[nodiscard]] const ::app::AppConfig& config() const;
    [[nodiscard]] ::app::AppConfig& getConfig();
    [[nodiscard]] const ::app::AppConfig& getConfig() const;
    void saveConfig();
    void applyMeshConfig();
    void applyUserInfo();
    void applyPositionConfig();
    void applyNetworkLimits();
    void applyPrivacyConfig();
    void applyChatDefaults();

    [[nodiscard]] ::chat::MeshProtocol meshProtocol() const;
    [[nodiscard]] ::chat::MeshProtocol getMeshProtocol() const;
    bool switchMeshProtocol(::chat::MeshProtocol protocol, bool persist = true);

    [[nodiscard]] ::chat::ChatService& chat();
    [[nodiscard]] ::chat::ChatService& getChatService();
    [[nodiscard]] ::chat::contacts::ContactService& contacts();
    [[nodiscard]] ::chat::contacts::ContactService& getContactService();
    [[nodiscard]] ::chat::IMeshAdapter* meshAdapter();
    [[nodiscard]] ::chat::IMeshAdapter* getMeshAdapter();
    [[nodiscard]] const ::chat::IMeshAdapter* meshAdapter() const;
    [[nodiscard]] const ::chat::IMeshAdapter* getMeshAdapter() const;
    [[nodiscard]] ::chat::NodeId selfNodeId() const;
    [[nodiscard]] ::chat::NodeId getSelfNodeId() const;

    [[nodiscard]] ::team::TeamController* teamController();
    [[nodiscard]] ::team::TeamController* getTeamController();
    [[nodiscard]] ::team::TeamPairingService* teamPairing();
    [[nodiscard]] ::team::TeamPairingService* getTeamPairing();
    [[nodiscard]] ::team::TeamService* teamService();
    [[nodiscard]] ::team::TeamService* getTeamService();
    [[nodiscard]] const ::team::TeamService* teamService() const;
    [[nodiscard]] const ::team::TeamService* getTeamService() const;
    [[nodiscard]] ::team::TeamTrackSampler* teamTrackSampler();
    [[nodiscard]] ::team::TeamTrackSampler* getTeamTrackSampler();
    void setTeamModeActive(bool active);

    void broadcastNodeInfo();
    void clearNodeDb();
    void clearMessageDb();

    bool isBleEnabled() const;
    void setBleEnabled(bool enabled);
    void restartDevice();

  private:
    struct Implementation;

    Implementation& impl();
    const Implementation& impl() const;
    void loadPersistedConfig();
    void seedDefaultIdentity();
    void syncLocalIdentity();
    void ensureServicesReady();

    LinuxAppServicesOptions options_{};
    ::app::AppConfig config_{};
    std::unique_ptr<Implementation> impl_;
    UiEventDispatcher ui_event_dispatcher_ = nullptr;
    void* ui_event_context_ = nullptr;
    bool initialized_ = false;
};

} // namespace trailmate::linux_app
