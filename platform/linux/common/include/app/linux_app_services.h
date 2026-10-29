#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "app/app_config.h"
#include "app/app_config_edit.h"
#include "app/config_persistence_runtime.h"
#include "app/tms_config_codec.h"
#include "chat/domain/chat_types.h"
#include "chat/domain/reticulum_network_config.h"
#include "chat/ports/device_identity_restore.h"
#include "platform/linux/bluez_phone_gatt.h"
#include "platform/linux/runtime_mode.h"
#include <string>

namespace chat
{
class ChatService;
class IMeshAdapter;
namespace contacts
{
class ContactService;
}
} // namespace chat

namespace platform::linux_runtime
{
class IPacketRadio;
class IRadioPowerControl;
class IVoiceSource;
struct IdentityRestoreCommand;
} // namespace platform::linux_runtime

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
class LinuxPhoneRuntime;
struct TmsCommitOptions;

using UiEventDispatcher = bool (*)(void* context, ::sys::Event* event);

struct LinuxAppServicesOptions
{
    const char* default_node_name = "Trail Mate Linux";
    const char* default_short_name = "TL";
    const char* demo_broadcast_text = "Broadcast: Linux local mesh online.";
    platform::linux_runtime::LinuxRuntimeMode runtime_mode =
        platform::linux_runtime::resolve_runtime_mode();
    bool ble_supported = false;
    platform::linux_runtime::BluezPhoneGattOptions phone_ble{};
    std::function<bool(const platform::linux_runtime::PhonePairingDisplay&)> phone_pairing_presenter{};
    // Optional composition-owned radio; it must outlive these services.
    platform::linux_runtime::IPacketRadio* packet_radio = nullptr;
    platform::linux_runtime::IRadioPowerControl* radio_power = nullptr;
    platform::linux_runtime::IPacketRadio* foreground_radio = nullptr;
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
    // Frontend-owned. A quiesced replacement attempt retires the current run;
    // success is activated only after application restart and fresh proof.
    chat::DeviceIdentityRestoreResult restoreDeviceIdentity(const platform::linux_runtime::IdentityRestoreCommand&);
    platform::linux_runtime::IVoiceSource* voiceSource();
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
    [[nodiscard]] ::app::AppConfigEdit beginConfigEdit();
    void saveConfig();
    void saveConfig(::app::AppConfigChangeSet changes);
    void applyMeshConfig();
    void applyUserInfo();
    void applyPositionConfig();
    void applyNetworkLimits();
    void applyPrivacyConfig();
    void applyChatDefaults();

    [[nodiscard]] ::chat::MeshProtocol meshProtocol() const;
    [[nodiscard]] ::chat::MeshProtocol getMeshProtocol() const;
    bool switchMeshProtocol(::chat::MeshProtocol protocol, bool persist = true);
    // A complete explicit network list, including an intentionally empty list.
    // Storage admission precedes runtime activation. UI callers own this API.
    bool setReticulumNetworkConfig(const ::chat::reticulum::ReticulumNetworkConfig& config);
    bool getReticulumNetworkConfig(::chat::reticulum::ReticulumNetworkConfig* output) const;
    enum class SettingsImportResult
    {
        RestartPending,
        Busy,
        Unavailable,
        InvalidDocument,
        StorageError,
        Conflict
    };
    // Frontend-thread entry points. Import retains call exclusion and freezes
    // old configuration persistence until the composition tears down and execs.
    bool exportSettingsDocument(::app::tms::DocumentKind, std::string& output);
    SettingsImportResult importSettingsDocument(const char*, size_t, ::app::tms::DocumentKind,
                                                const TmsCommitOptions* commit = nullptr);

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
    void setPhonePairingPresenter(std::function<bool(const platform::linux_runtime::PhonePairingDisplay&)>);
    void restartDevice();

  private:
    struct Implementation;

    Implementation& impl();
    const Implementation& impl() const;
    bool loadPersistedConfig();
    void seedDefaultIdentity();
    void updatePhoneRuntime();
    void syncLocalIdentity();
    void ensureServicesReady();
    bool ensureNodeIdentity();
    void flushConfigPersistence(uint32_t now_ms);
    bool writePersistedConfig(const ::app::AppConfig& config);
    bool prepareSettingsFactoryReset();
    static void commitConfigEdit(void* context,
                                 ::app::AppConfigChangeSet changes);
    static void cancelConfigEdit(void* context);

    LinuxAppServicesOptions options_{};
    ::app::AppConfig config_{};
    ::chat::reticulum::ReticulumNetworkConfig reticulum_network_config_{};
    bool has_reticulum_network_config_ = false;
    ::app::ConfigPersistenceRuntime config_persistence_runtime_{};
    std::unique_ptr<Implementation> impl_;
    std::unique_ptr<LinuxPhoneRuntime> phone_;
    UiEventDispatcher ui_event_dispatcher_ = nullptr;
    void* ui_event_context_ = nullptr;
    bool initialized_ = false;
    bool settings_reset_pending_ = false;
    bool settings_import_pending_ = false;
    bool identity_restore_pending_ = false;
    uint32_t self_node_id_ = 0;
};

} // namespace trailmate::linux_app
