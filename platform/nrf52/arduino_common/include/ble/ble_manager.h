#pragma once

#include "app/app_facades.h"
#include "chat/domain/chat_types.h"

#include <memory>
#include <string>

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
    explicit BleManager(app::IAppBleFacade& ctx);
    ~BleManager();

    void begin();
    void setEnabled(bool enabled);
    bool isEnabled() const;
    void update();
    void applyProtocol(chat::MeshProtocol protocol);

  private:
    void restartService(chat::MeshProtocol protocol);
    std::string buildDeviceName(chat::MeshProtocol protocol) const;

    app::IAppBleFacade& ctx_;
    chat::MeshProtocol active_protocol_;
    std::unique_ptr<BleService> service_;
};

} // namespace ble
