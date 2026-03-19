#include "chat/ble/meshtastic_phone_session.h"

namespace ble
{

MeshtasticPhoneSession::MeshtasticPhoneSession(app::IAppBleFacade& ctx, MeshtasticPhoneTransport& transport,
                                               MeshtasticPhoneHooks* hooks)
    : core_(ctx, transport, hooks)
{
}

void MeshtasticPhoneSession::close()
{
    core_.reset();
}

void MeshtasticPhoneSession::pumpIncomingAppData()
{
    core_.pumpIncomingAppData();
}

bool MeshtasticPhoneSession::isSendingPackets() const
{
    return core_.isSendingPackets();
}

bool MeshtasticPhoneSession::isConfigFlowActive() const
{
    return core_.isConfigFlowActive();
}

bool MeshtasticPhoneSession::handleToRadio(const uint8_t* buf, size_t len)
{
    return core_.handleToRadio(buf, len);
}

bool MeshtasticPhoneSession::popToPhone(MeshtasticBleFrame* out)
{
    return core_.popToPhone(out);
}

void MeshtasticPhoneSession::onIncomingText(const chat::MeshIncomingText& msg)
{
    core_.onIncomingText(msg);
}

void MeshtasticPhoneSession::onIncomingData(const chat::MeshIncomingData& msg)
{
    core_.onIncomingData(msg);
}

} // namespace ble
