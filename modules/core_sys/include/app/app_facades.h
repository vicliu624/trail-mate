#pragma once

#include <cstddef>
#include <cstdint>

#include "chat/domain/chat_types.h"

class BoardBase;

namespace app
{
struct AppConfig;
struct AppEventRuntimeHooks;
} // namespace app

namespace ble
{
class BleManager;
}

namespace chat
{
class ChatService;
class IMeshAdapter;
namespace contacts
{
class ContactService;
class INodeStore;
} // namespace contacts
namespace ui
{
class IChatUiRuntime;
} // namespace ui
} // namespace chat

namespace team
{
class TeamController;
class TeamPairingService;
class TeamService;
class TeamTrackSampler;
} // namespace team

namespace app
{

class IAppConfigFacade
{
  public:
    virtual ~IAppConfigFacade() = default;
    virtual AppConfig& getConfig() = 0;
    virtual const AppConfig& getConfig() const = 0;
    virtual void saveConfig() = 0;
    virtual void applyMeshConfig() = 0;
    virtual void applyUserInfo() = 0;
    virtual void applyPositionConfig() = 0;
    virtual void applyNetworkLimits() = 0;
    virtual void applyPrivacyConfig() = 0;
    virtual void applyChatDefaults() = 0;
    virtual chat::MeshProtocol getMeshProtocol() const = 0;
    virtual void getEffectiveUserInfo(char* out_long, std::size_t long_len, char* out_short, std::size_t short_len) const = 0;
    virtual bool switchMeshProtocol(chat::MeshProtocol protocol, bool persist = true) = 0;
};

class IAppMessagingFacade
{
  public:
    virtual ~IAppMessagingFacade() = default;
    virtual chat::ChatService& getChatService() = 0;
    virtual chat::contacts::ContactService& getContactService() = 0;
    virtual chat::IMeshAdapter* getMeshAdapter() = 0;
    virtual const chat::IMeshAdapter* getMeshAdapter() const = 0;
    virtual chat::NodeId getSelfNodeId() const = 0;
};

class IAppTeamFacade
{
  public:
    virtual ~IAppTeamFacade() = default;
    virtual team::TeamController* getTeamController() = 0;
    virtual team::TeamPairingService* getTeamPairing() = 0;
    virtual team::TeamService* getTeamService() = 0;
    virtual const team::TeamService* getTeamService() const = 0;
    virtual team::TeamTrackSampler* getTeamTrackSampler() = 0;
    virtual void setTeamModeActive(bool active) = 0;
};

class IAppAdminFacade
{
  public:
    virtual ~IAppAdminFacade() = default;
    virtual void broadcastNodeInfo() = 0;
    virtual void clearNodeDb() = 0;
    virtual void clearMessageDb() = 0;
};

class IAppRuntimeFacade
{
  public:
    virtual ~IAppRuntimeFacade() = default;
    virtual ble::BleManager* getBleManager() = 0;
    virtual const ble::BleManager* getBleManager() const = 0;
    virtual bool isBleEnabled() const = 0;
    virtual void setBleEnabled(bool enabled) = 0;
    virtual void restartDevice() = 0;
    virtual chat::ui::IChatUiRuntime* getChatUiRuntime() = 0;
    virtual void setChatUiRuntime(chat::ui::IChatUiRuntime* runtime) = 0;
    virtual BoardBase* getBoard() = 0;
    virtual const BoardBase* getBoard() const = 0;
};

class IAppLifecycleFacade
{
  public:
    virtual ~IAppLifecycleFacade() = default;
    virtual void updateCoreServices() = 0;
    virtual void tickEventRuntime() = 0;
    virtual void dispatchPendingEvents(std::size_t max_events = 32) = 0;
};

class IAppFacade : public IAppConfigFacade,
                   public IAppMessagingFacade,
                   public IAppTeamFacade,
                   public IAppAdminFacade,
                   public IAppRuntimeFacade,
                   public IAppLifecycleFacade
{
  public:
    ~IAppFacade() override = default;
};

class IAppBleFacade : public IAppFacade
{
  public:
    ~IAppBleFacade() override = default;
    virtual chat::contacts::INodeStore* getNodeStore() = 0;
    virtual const chat::contacts::INodeStore* getNodeStore() const = 0;
    virtual bool getDeviceMacAddress(uint8_t out_mac[6]) const = 0;
    virtual bool syncCurrentEpochSeconds(uint32_t epoch_seconds) = 0;
    virtual void resetMeshConfig() = 0;
};

} // namespace app
