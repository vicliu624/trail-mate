#pragma once

#include "app/app_config.h"
#include "chat/domain/chat_types.h"

#include <memory>
#include <string>

namespace app
{
class IAppBleFacade;
}

namespace ble
{

class IBleRuntimeContext
{
  public:
    virtual ~IBleRuntimeContext() = default;
    virtual const app::AppConfig& bleConfig() const = 0;
    virtual bool bleEnabled() const = 0;
    virtual void bleEffectiveUserInfo(char* out_long, std::size_t long_len,
                                      char* out_short, std::size_t short_len) const = 0;
    virtual chat::NodeId bleSelfNodeId() const = 0;
    virtual app::IAppBleFacade& bleAppFacade() = 0;
};

struct BlePairingStatus
{
    bool available = false;
    bool requires_passkey = false;
    bool is_fixed_pin = false;
    bool is_pairing_active = false;
    bool is_connected = false;
    uint32_t passkey = 0;
};

class BleService
{
  public:
    virtual ~BleService() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void update() = 0;
    virtual bool isRunning() const { return false; }
    virtual void setDeviceName(const std::string& name)
    {
        (void)name;
    }
    virtual bool getPairingStatus(BlePairingStatus* out) const
    {
        (void)out;
        return false;
    }
};

class BleManager
{
  public:
    explicit BleManager(IBleRuntimeContext& ctx);
    ~BleManager();

    void begin();
    void setEnabled(bool enabled);
    bool isEnabled() const;
    void update();
    void applyProtocol(chat::MeshProtocol protocol);
    bool getPairingStatus(BlePairingStatus* out) const;

  private:
    void restartService(chat::MeshProtocol protocol);
    std::string buildDeviceName(chat::MeshProtocol protocol) const;

    IBleRuntimeContext& ctx_;
    chat::MeshProtocol active_protocol_;
    std::unique_ptr<BleService> service_;
};

} // namespace ble
