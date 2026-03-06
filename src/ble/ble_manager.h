#pragma once

#include "../app/app_config.h"
#include "../chat/domain/chat_types.h"
#include <memory>
#include <string>

namespace app
{
class AppContext;
}

namespace ble
{

class BleService
{
  public:
    virtual ~BleService() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void update() = 0;
};

class BleManager
{
  public:
    explicit BleManager(app::AppContext& ctx);
    ~BleManager();

    void begin();
    void setEnabled(bool enabled);
    bool isEnabled() const { return service_ != nullptr; }
    void update();
    void applyProtocol(chat::MeshProtocol protocol);

  private:
    app::AppContext& ctx_;
    chat::MeshProtocol active_protocol_;
    std::unique_ptr<BleService> service_;
    bool nimble_initialized_ = false;

    void restartService(chat::MeshProtocol protocol);
    void shutdownNimble();
    std::string buildDeviceName(chat::MeshProtocol protocol) const;
};

} // namespace ble
