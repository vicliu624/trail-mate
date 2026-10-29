#pragma once
#include "phone/common/phone_app_facade.h"
#include <array>
#include <deque>

namespace chat::meshcore
{
class IMeshCoreBleBackend;
}
namespace trailmate::linux_app
{
class LinuxAppServices;
// Application-thread adapter. No D-Bus types and no reference to legacy UI facades.
class LinuxPhoneFacade final : public phone::IPhoneAppFacade
{
  public:
    explicit LinuxPhoneFacade(LinuxAppServices& services);
    void setDeviceMac(const std::array<uint8_t, 6>& address);
    bool enqueueIncomingData(const chat::MeshIncomingData& data);
    void clearIncomingData();
    phone::MeshtasticPhoneConfigSnapshot getMeshtasticPhoneConfig() const override;
    void setMeshtasticPhoneConfig(const phone::MeshtasticPhoneConfigSnapshot&) override;
    phone::MeshCorePhoneConfigSnapshot getMeshCorePhoneConfig() const override;
    void setMeshCorePhoneConfig(const phone::MeshCorePhoneConfigSnapshot&) override;
    void saveConfig() override;
    void applyMeshConfig() override;
    void applyUserInfo() override;
    void applyPositionConfig() override;
    void getEffectiveUserInfo(char*, size_t, char*, size_t) const override;
    chat::NodeId getSelfNodeId() const override;
    bool sendPhoneText(chat::ChannelId, const std::string&, chat::MessageId, chat::NodeId, chat::MessageId&) override;
    bool sendPhoneAppData(chat::ChannelId, uint32_t, const uint8_t*, size_t, chat::NodeId, bool, chat::MessageId, bool) override;
    bool pollIncomingPhoneData(chat::MeshIncomingData&) override;
    size_t phoneNodeCount() const override;
    bool getPhoneNodeByIndex(size_t, phone::PhoneNodeView&) const override;
    bool findPhoneNode(chat::NodeId, phone::PhoneNodeView&) const override;
    bool isMeshPkiReady() const override;
    bool isBleEnabled() const override;
    void setBleEnabled(bool) override;
    bool getDeviceMacAddress(uint8_t[6]) const override;
    bool syncCurrentEpochSeconds(uint32_t) override;
    void resetMeshConfig() override;
    void clearNodeDb() override;
    void clearMessageDb() override;
    void restartDevice() override;
    bool meshCoreExportIdentityPublicKey(uint8_t*, size_t) override;
    bool meshCoreExportIdentityPrivateKey(uint8_t*, size_t) override;
    bool meshCoreImportIdentityPrivateKey(const uint8_t*, size_t) override;
    bool meshCoreSignPayload(const uint8_t*, size_t, uint8_t*, size_t) override;
    bool meshCoreSendSelfAdvert(bool) override;
    bool meshCoreSendPeerRequestType(const uint8_t*, size_t, uint8_t, uint32_t*, uint32_t*, bool*) override;
    bool meshCoreSendPeerRequestPayload(const uint8_t*, size_t, const uint8_t*, size_t, bool, uint32_t*, uint32_t*, bool*) override;
    bool meshCoreSendAnonRequestPayload(const uint8_t*, size_t, const uint8_t*, size_t, uint32_t*, bool*) override;
    bool meshCoreSendTracePath(const uint8_t*, size_t, uint32_t, uint32_t, uint8_t, uint32_t*) override;
    bool meshCoreSendControlData(const uint8_t*, size_t) override;
    bool meshCoreSendRawData(const uint8_t*, size_t, const uint8_t*, size_t, uint32_t*) override;
    bool meshCoreSendRawDataEx(uint8_t, const uint8_t*, size_t, const uint8_t*, size_t, uint32_t*) override;
    void meshCoreSetFloodScopeKey(const uint8_t*, size_t) override;

  private:
    chat::meshcore::IMeshCoreBleBackend* meshCoreBackend() const;
    LinuxAppServices& services_;
    std::array<uint8_t, 6> mac_{};
    bool has_mac_ = false;
    std::deque<chat::MeshIncomingData> incoming_;
};
} // namespace trailmate::linux_app
