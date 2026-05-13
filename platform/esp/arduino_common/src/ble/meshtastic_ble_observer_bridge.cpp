#include "ble/meshtastic_ble_observer_bridge.h"

#include "ble/meshtastic_ble.h"

namespace ble
{

MeshtasticBleObserverBridge::MeshtasticBleObserverBridge(app::IAppBleFacade& app, MeshtasticBleService& service)
    : app_(app), service_(service)
{
}

MeshtasticBleObserverBridge::~MeshtasticBleObserverBridge()
{
    unregisterObservers();
}

void MeshtasticBleObserverBridge::registerObservers()
{
    if (registered_)
    {
        return;
    }

    app_.getChatService().addIncomingTextObserver(this);
    app_.getChatService().addOutgoingTextObserver(this);
    if (auto* team = app_.getTeamService())
    {
        team->addIncomingDataObserver(this);
    }
    registered_ = true;
}

void MeshtasticBleObserverBridge::unregisterObservers()
{
    if (!registered_)
    {
        return;
    }

    app_.getChatService().removeIncomingTextObserver(this);
    app_.getChatService().removeOutgoingTextObserver(this);
    if (auto* team = app_.getTeamService())
    {
        team->removeIncomingDataObserver(this);
    }
    registered_ = false;
}

void MeshtasticBleObserverBridge::onIncomingText(const chat::MeshIncomingText& msg)
{
    service_.handleIncomingTextFromApp(msg);
}

void MeshtasticBleObserverBridge::onOutgoingText(const chat::MeshIncomingText& msg)
{
    service_.handleOutgoingTextFromApp(msg);
}

void MeshtasticBleObserverBridge::onIncomingData(const chat::MeshIncomingData& msg)
{
    service_.handleIncomingDataFromApp(msg);
}

} // namespace ble
