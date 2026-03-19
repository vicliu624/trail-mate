#pragma once

#include "chat/ble/meshtastic_phone_core.h"

namespace ble
{

class MeshtasticPhoneSession
{
  public:
    MeshtasticPhoneSession(app::IAppBleFacade& ctx, MeshtasticPhoneTransport& transport, MeshtasticPhoneHooks* hooks);

    void close();
    void pumpIncomingAppData();
    bool isSendingPackets() const;
    bool isConfigFlowActive() const;
    bool handleToRadio(const uint8_t* buf, size_t len);
    bool popToPhone(MeshtasticBleFrame* out);
    void onIncomingText(const chat::MeshIncomingText& msg);
    void onIncomingData(const chat::MeshIncomingData& msg);

  private:
    MeshtasticPhoneCore core_;
};

} // namespace ble
