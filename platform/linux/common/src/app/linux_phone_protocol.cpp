#include "app/linux_phone_protocol.h"
#include "app/linux_app_services.h"
#include "app/linux_phone_bluetooth_config.h"
#include "app/linux_phone_facade.h"
#include "chat/usecase/chat_service.h"
#include "phone/meshcore/meshcore_phone_core.h"
#include "phone/meshtastic/meshtastic_phone_config_bridge.h"
#include "phone/meshtastic/meshtastic_phone_session.h"
#include <algorithm>

namespace trailmate::linux_app
{
using Channel = platform::linux_runtime::PhoneGattChannel;
using Protocol = platform::linux_runtime::PhoneGattProtocol;
struct LinuxPhoneProtocol::Impl final : phone::meshtastic::MeshtasticPhoneTransport,
                                        phone::meshtastic::MeshtasticPhoneStatusHooks,
                                        chat::ChatService::IncomingTextObserver,
                                        chat::ChatService::IncomingDataObserver
{
    LinuxAppServices& services;
    LinuxPhoneFacade facade;
    LinuxPhoneBluetoothConfig bluetooth;
    platform::linux_runtime::PhonePairingDisplay pairing;
    std::unique_ptr<phone::meshtastic::MeshtasticPhoneSession> meshtastic;
    std::unique_ptr<phone::meshcore::MeshCorePhoneCore> meshcore;
    phone::meshtastic::MeshtasticBleFrame mt_scratch{};
    phone::meshcore::MeshCoreBleFrame mc_scratch{};
    Protocol protocol = Protocol::Meshtastic;
    bool connected = false, disconnect_requested = false;
    std::array<bool, 7> subscribed{};
    uint32_t from_num = 0;
    uint64_t sequence = 0, counter_token = 0;
    Notification pending;

    explicit Impl(LinuxAppServices& owner) : services(owner), facade(owner), bluetooth([this]
                                                                                       { return services.isBleEnabled(); })
    {
        meshtastic = std::make_unique<phone::meshtastic::MeshtasticPhoneSession>(facade, *this, &bluetooth, nullptr, nullptr, this, nullptr, nullptr);
        meshcore = std::make_unique<phone::meshcore::MeshCorePhoneCore>(facade, "Trail Mate Linux");
        services.chat().addIncomingTextObserver(this);
        services.chat().addIncomingDataObserver(this);
    }
    ~Impl()
    {
        services.chat().removeIncomingDataObserver(this);
        services.chat().removeIncomingTextObserver(this);
    }
    bool matches() const
    {
        return connected && ((protocol == Protocol::Meshtastic && services.meshProtocol() == chat::MeshProtocol::Meshtastic) ||
                             (protocol == Protocol::MeshCore && services.meshProtocol() == chat::MeshProtocol::MeshCore));
    }
    uint64_t nextToken()
    {
        if (++sequence == 0) ++sequence;
        return sequence;
    }
    bool isBleConnected() const override { return matches(); }
    bool loadDeviceConnectionStatus(meshtastic_DeviceConnectionStatus* out) const override
    {
        return phone::meshtastic::config_bridge::fillDeviceConnectionStatus(pairing.active ? pairing.passkey : 0, matches(), out);
    }
    void notifyFromNum(uint32_t value) override
    {
        from_num = value;
        counter_token = nextToken();
    }
    void requestPhoneDisconnect() override { disconnect_requested = true; }
    void onIncomingText(const chat::MeshIncomingText& message) override
    {
        // ChatService dispatches facts from its active adapter on this thread.
        if (!matches()) return;
        if (protocol == Protocol::Meshtastic) meshtastic->onIncomingText(message);
        else meshcore->onIncomingText(message);
    }
    void onIncomingData(const chat::MeshIncomingData& message) override
    {
        if (!matches()) return;
        // Copy the observer fact instead of polling the mesh adapter a second time.
        (void)facade.enqueueIncomingData(message);
    }
    void reset()
    {
        connected = false;
        meshtastic->close();
        meshcore->reset();
        facade.clearIncomingData();
        facade.setDeviceMac({});
        subscribed.fill(false);
        counter_token = 0;
        from_num = 0;
        pending = {};
        disconnect_requested = false;
        pairing = {};
    }
};

LinuxPhoneProtocol::LinuxPhoneProtocol(LinuxAppServices& services) : impl_(std::make_unique<Impl>(services)) {}
LinuxPhoneProtocol::~LinuxPhoneProtocol() = default;
bool LinuxPhoneProtocol::supports(Protocol protocol) const
{
    if (!impl_->bluetooth.ready() || !impl_->services.isInitialized() || !impl_->services.selfNodeId()) return false;
    if (protocol == Protocol::Meshtastic) return impl_->services.meshProtocol() == chat::MeshProtocol::Meshtastic;
    if (impl_->services.meshProtocol() != chat::MeshProtocol::MeshCore) return false;
    std::array<uint8_t, 32> public_key{};
    return impl_->facade.meshCoreExportIdentityPublicKey(public_key.data(), public_key.size()) &&
           std::any_of(public_key.begin(), public_key.end(), [](uint8_t value)
                       { return value != 0; });
}
bool LinuxPhoneProtocol::connect(Protocol protocol, const std::array<uint8_t, 6>& address)
{
    // BlueZ may deliver subscription state before the first read/write claims a
    // peer. Keep those subscriptions only for this newly connected generation.
    const auto subscriptions = impl_->subscribed;
    impl_->reset();
    impl_->subscribed = subscriptions;
    impl_->protocol = protocol;
    impl_->facade.setDeviceMac(address);
    impl_->connected = supports(protocol);
    return impl_->connected;
}
bool LinuxPhoneProtocol::pairingOptions(platform::linux_runtime::BluezPhoneGattOptions& options) const
{
    if (!impl_->bluetooth.ready()) return false;
    const auto& config = impl_->bluetooth.value();
    using Mode = platform::linux_runtime::PhonePairingMode;
    switch (config.mode)
    {
    case meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN:
        options.pairing_mode = Mode::NoPin;
        break;
    case meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN:
        options.pairing_mode = Mode::FixedPin;
        break;
    default:
        options.pairing_mode = Mode::RandomPin;
        break;
    }
    options.fixed_pin = config.fixed_pin;
    options.encrypted = options.pairing_mode != Mode::NoPin;
    return true;
}
const std::string& LinuxPhoneProtocol::pairingError() const { return impl_->bluetooth.error(); }
void LinuxPhoneProtocol::setPairingDisplay(const platform::linux_runtime::PhonePairingDisplay& display) { impl_->pairing = display; }
void LinuxPhoneProtocol::disconnect() { impl_->reset(); }
void LinuxPhoneProtocol::subscribe(Channel channel, bool enabled)
{
    const auto index = static_cast<size_t>(channel);
    if (index < impl_->subscribed.size()) impl_->subscribed[index] = enabled;
}
bool LinuxPhoneProtocol::read(Channel channel, std::vector<uint8_t>& output)
{
    output.clear();
    if (!impl_->matches() || impl_->protocol != Protocol::Meshtastic) return false;
    if (channel == Channel::FromNum)
    {
        output = {uint8_t(impl_->from_num), uint8_t(impl_->from_num >> 8), uint8_t(impl_->from_num >> 16), uint8_t(impl_->from_num >> 24)};
        return true;
    }
    if (channel == Channel::LogRadio) return true; // No log sink is registered.
    if (channel != Channel::FromRadio) return false;
    if (impl_->pending.token && impl_->pending.channel == Channel::FromRadioSync)
    {
        output = std::move(impl_->pending.bytes);
        impl_->pending = {};
        return true;
    }
    if (impl_->meshtastic->popToPhone(&impl_->mt_scratch))
        output.assign(impl_->mt_scratch.buf, impl_->mt_scratch.buf + impl_->mt_scratch.len);
    return true;
}
bool LinuxPhoneProtocol::write(Channel channel, const std::vector<uint8_t>& bytes)
{
    if (!impl_->matches() || bytes.empty()) return false;
    if (impl_->protocol == Protocol::Meshtastic && channel == Channel::ToRadio)
        return impl_->meshtastic->handleToRadio(bytes.data(), bytes.size());
    if (impl_->protocol == Protocol::MeshCore && channel == Channel::MeshCoreRx)
        return impl_->meshcore->handleRxFrame(bytes.data(), bytes.size());
    return false;
}
void LinuxPhoneProtocol::pump()
{
    if (!impl_->matches()) return;
    if (impl_->protocol == Protocol::Meshtastic) impl_->meshtastic->pumpIncomingAppData();
    else impl_->meshcore->pumpIncomingAppData();
}
bool LinuxPhoneProtocol::nextNotification(Notification& output)
{
    output = {};
    if (!impl_->matches()) return false;
    if (impl_->protocol == Protocol::Meshtastic && impl_->counter_token && impl_->subscribed[static_cast<size_t>(Channel::FromNum)])
    {
        output.token = impl_->counter_token;
        output.channel = Channel::FromNum;
        return read(Channel::FromNum, output.bytes);
    }
    const auto channel = impl_->protocol == Protocol::Meshtastic ? Channel::FromRadioSync : Channel::MeshCoreTx;
    if (!impl_->subscribed[static_cast<size_t>(channel)]) return false;
    if (!impl_->pending.token)
    {
        if (impl_->protocol == Protocol::Meshtastic)
        {
            if (!impl_->meshtastic->popToPhone(&impl_->mt_scratch)) return false;
            impl_->pending.bytes.assign(impl_->mt_scratch.buf, impl_->mt_scratch.buf + impl_->mt_scratch.len);
        }
        else
        {
            size_t length = impl_->mc_scratch.buf.size();
            if (!impl_->meshcore->popTxFrame(impl_->mc_scratch.buf.data(), &length)) return false;
            impl_->pending.bytes.assign(impl_->mc_scratch.buf.begin(), impl_->mc_scratch.buf.begin() + length);
        }
        impl_->pending.channel = channel;
        impl_->pending.token = impl_->nextToken();
    }
    output = impl_->pending;
    return true;
}
void LinuxPhoneProtocol::commitNotification(uint64_t token)
{
    if (token && token == impl_->counter_token) impl_->counter_token = 0;
    if (token && token == impl_->pending.token) impl_->pending = {};
}
bool LinuxPhoneProtocol::takeDisconnectRequest()
{
    const bool requested = impl_->disconnect_requested;
    impl_->disconnect_requested = false;
    return requested;
}
} // namespace trailmate::linux_app
