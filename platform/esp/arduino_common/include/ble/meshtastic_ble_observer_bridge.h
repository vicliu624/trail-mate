#pragma once

#include "app/app_facades.h"
#include "chat/usecase/chat_service.h"
#include "team/usecase/team_service.h"

namespace ble
{

class MeshtasticBleService;

class MeshtasticBleObserverBridge final : public chat::ChatService::IncomingTextObserver,
                                          public chat::ChatService::OutgoingTextObserver,
                                          public team::TeamService::IncomingDataObserver
{
  public:
    MeshtasticBleObserverBridge(app::IAppBleFacade& app, MeshtasticBleService& service);
    ~MeshtasticBleObserverBridge() override;

    void registerObservers();
    void unregisterObservers();

    void onIncomingText(const chat::MeshIncomingText& msg) override;
    void onOutgoingText(const chat::MeshIncomingText& msg) override;
    void onIncomingData(const chat::MeshIncomingData& msg) override;

  private:
    app::IAppBleFacade& app_;
    MeshtasticBleService& service_;
    bool registered_ = false;
};

} // namespace ble
