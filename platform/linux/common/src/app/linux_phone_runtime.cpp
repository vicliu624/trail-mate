#include "app/linux_phone_runtime.h"
#include "app/linux_phone_protocol.h"
#include "platform/linux/bluez_adapters.h"
#include "platform/linux/phone_dispatch_queue.h"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace trailmate::linux_app
{
using namespace platform::linux_runtime;
namespace
{
bool sameOptions(const BluezPhoneGattOptions& left, const BluezPhoneGattOptions& right)
{
    return left.protocol == right.protocol && left.adapter_address == right.adapter_address &&
           left.local_name == right.local_name && left.encrypted == right.encrypted && left.bus_address == right.bus_address &&
           left.enable_from_radio_sync == right.enable_from_radio_sync && left.pairing_mode == right.pairing_mode && left.fixed_pin == right.fixed_pin;
}
std::array<uint8_t, 6> controllerMac(const std::string& address)
{
    std::array<uint8_t, 6> result{};
    unsigned values[6]{};
    if (std::sscanf(address.c_str(), "%2x:%2x:%2x:%2x:%2x:%2x", &values[0], &values[1], &values[2], &values[3], &values[4], &values[5]) != 6)
        return result;
    for (size_t i = 0; i < result.size(); ++i) result[i] = static_cast<uint8_t>(values[i]);
    return result;
}
} // namespace
struct LinuxPhoneRuntime::Impl
{
    LinuxPhoneProtocol protocol;
    PhoneDispatchQueue requests;
    std::function<bool(const PhonePairingDisplay&)> presenter;
    mutable std::mutex mutex;
    std::condition_variable changed;
    BluezPhoneGattOptions desired;
    std::string desired_error;
    bool enabled = false;
    uint64_t generation = 0;
    std::atomic<bool> ending{false};
    Status published;
    std::thread worker;

    explicit Impl(LinuxAppServices& services, std::function<bool(const PhonePairingDisplay&)> display) : protocol(services), presenter(std::move(display)), worker([this]
                                                                                                                                                                   { run(); }) {}
    ~Impl() { stop(); }
    void publish(bool ready, bool connected, const std::string& error, uint64_t revision = 0)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (revision && revision != generation) return;
        published = {ready, connected, error};
    }
    void stop()
    {
        ending = true;
        requests.stop(); // Release a callback waiting for a UI tick before joining.
        changed.notify_all();
        if (worker.joinable()) worker.join();
        protocol.disconnect();
        publish(false, false, {});
    }
    void run()
    {
        try
        {
            runTransport();
        }
        catch (const std::exception& error)
        {
            publish(false, false, error.what());
        }
        catch (...)
        {
            publish(false, false, "Phone worker failed");
        }
    }
    void runTransport()
    {
        uint64_t applied = 0, awaiting_commit = 0;
        bool connected = false, wanted = false;
        BluezPhoneGattOptions options;
        std::string configuration_error;
        auto retry = std::chrono::steady_clock::now();
        // Transport callbacks capture these locals; destroy the transport first
        // during exception unwinding as well as during normal shutdown.
        BluezPhoneGatt transport;
        while (!ending)
        {
            bool reconfigure;
            {
                std::lock_guard<std::mutex> lock(mutex);
                reconfigure = applied != generation;
                if (reconfigure)
                {
                    applied = generation;
                    wanted = enabled;
                    options = desired;
                    configuration_error = desired_error;
                }
            }
            if (reconfigure)
            {
                publish(false, false, configuration_error, applied);
                transport.stop();
                connected = false;
                awaiting_commit = 0;
                (void)requests.call([&]
                                    { protocol.disconnect(); return true; });
                publish(false, false, configuration_error, applied);
                retry = std::chrono::steady_clock::now();
            }
            if (ending) break;
            if (wanted && !transport.ready() && std::chrono::steady_clock::now() >= retry)
            {
                const auto selection = selectBluezAdapter(queryBluezAdapters(options.bus_address), options.adapter_address);
                if (selection.status != BluezSelectionStatus::Ready)
                {
                    publish(false, false, "No unique powered Bluetooth adapter with phone-service support", applied);
                }
                else
                {
                    const auto address = controllerMac(selection.adapter.address);
                    auto selected = options;
                    selected.adapter_address = selection.adapter.address;
                    BluezPhoneGattHooks hooks;
                    hooks.connected = [&, address, selected](const auto&)
                    {
                        if (!requests.call([&]
                                           { return protocol.connect(selected.protocol, address); }))
                            throw std::runtime_error("Application did not accept phone connection");
                        connected = true;
                        publish(transport.ready(), true, {}, applied);
                    };
                    hooks.disconnected = [&]
                    {
                        connected = false;
                        awaiting_commit = 0;
                        (void)requests.call([&]
                                            { protocol.disconnect(); return true; });
                        publish(transport.ready(), false, transport.error(), applied);
                    };
                    hooks.subscription = [&](auto channel, bool subscribe)
                    {
                        if (!requests.call([&]
                                           { protocol.subscribe(channel, subscribe); return true; }))
                            throw std::runtime_error("Application did not accept phone subscription");
                    };
                    hooks.read = [&](auto channel, auto& bytes)
                    {
                        return requests.call([&]
                                             { return protocol.read(channel, bytes); });
                    };
                    hooks.write = [&](auto channel, const auto& bytes)
                    {
                        return requests.call([&]
                                             { return protocol.write(channel, bytes); });
                    };
                    hooks.pairing = [&](const PhonePairingDisplay& display)
                    {
                        return requests.call([&]
                                             {
                            if (!display.active) protocol.setPairingDisplay(display);
                            const bool shown = presenter ? presenter(display) : !display.active;
                            if (shown && display.active) protocol.setPairingDisplay(display);
                            return shown; });
                    };
                    const bool started = transport.start(selected, std::move(hooks));
                    publish(started, false, transport.error(), applied);
                }
                retry = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            }
            transport.poll();
            if (wanted && !transport.ready())
            {
                connected = false;
                // Preserve the discovery error when registration was never attempted.
                if (!transport.error().empty()) publish(false, false, transport.error(), applied);
            }
            if (transport.ready() && connected)
            {
                if (awaiting_commit)
                {
                    if (requests.call([&]
                                      { protocol.commitNotification(awaiting_commit); return true; })) awaiting_commit = 0;
                }
                else
                {
                    LinuxPhoneProtocol::Notification notification;
                    bool disconnect = false;
                    const bool available = requests.call([&]
                                                         {
                        disconnect = protocol.takeDisconnectRequest();
                        return !disconnect && protocol.nextNotification(notification); });
                    if (disconnect)
                    {
                        if (!transport.disconnectPhone()) transport.stop();
                    }
                    else if (available && transport.notify(notification.channel, notification.bytes))
                    {
                        // Do not resend a delivered frame if its UI acknowledgement
                        // was queued during a temporary pause in application ticks.
                        awaiting_commit = notification.token;
                    }
                }
            }
            std::unique_lock<std::mutex> lock(mutex);
            auto pending = [&]
            { return ending || generation != applied; };
            if (!wanted) changed.wait(lock, pending);
            else if (!transport.ready()) changed.wait_until(lock, retry, pending);
            else changed.wait_for(lock, std::chrono::milliseconds(10), pending);
        }
        transport.stop();
    }
};

LinuxPhoneRuntime::LinuxPhoneRuntime(LinuxAppServices& services, std::function<bool(const PhonePairingDisplay&)> presenter)
    : impl_(std::make_unique<Impl>(services, std::move(presenter))) {}
LinuxPhoneRuntime::~LinuxPhoneRuntime() = default;
void LinuxPhoneRuntime::setPairingPresenter(std::function<bool(const PhonePairingDisplay&)> presenter)
{
    if (impl_->presenter) (void)impl_->presenter({});
    impl_->presenter = std::move(presenter);
}
void LinuxPhoneRuntime::configure(bool enabled, const BluezPhoneGattOptions& options)
{
    auto selected = options;
    const bool configured = impl_->protocol.pairingOptions(selected);
    const bool pin = selected.pairing_mode == PhonePairingMode::RandomPin || selected.pairing_mode == PhonePairingMode::FixedPin;
    std::string error;
    if (enabled && !configured) error = impl_->protocol.pairingError();
    else if (enabled && pin && !impl_->presenter) error = "Bluetooth pairing display unavailable";
    enabled = enabled && configured && (!pin || impl_->presenter) && impl_->protocol.supports(options.protocol);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->ending || (impl_->enabled == enabled && sameOptions(impl_->desired, selected) && impl_->desired_error == error)) return;
    impl_->enabled = enabled;
    impl_->desired = selected;
    impl_->desired_error = error;
    impl_->published = {false, false, error};
    ++impl_->generation;
    impl_->changed.notify_all();
}
void LinuxPhoneRuntime::tick()
{
    impl_->requests.pump();
    impl_->protocol.pump();
}
void LinuxPhoneRuntime::stop() { impl_->stop(); }
LinuxPhoneRuntime::Status LinuxPhoneRuntime::status() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->published;
}
} // namespace trailmate::linux_app
