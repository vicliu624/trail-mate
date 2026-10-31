#include "platform/linux/packet_radio.h"
#include "platform/shared/radio/fm_iq_stream.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

namespace platform::linux_runtime
{
struct PacketRadioCoordinator::Impl
{
    struct Client final : IPacketRadio
    {
        Impl& owner;
        unsigned kind;
        Client(Impl& context, unsigned use) : owner(context), kind(use) {}
        const char* name() const override { return owner.driver->name(); }
        bool hardwareCandidatePresent() const override
        {
            // Backend presence checks only inspect immutable endpoint paths;
            // they must not wait for an in-flight transport operation.
            return owner.driver->hardwareCandidatePresent();
        }
        bool acquire() override
        {
            std::unique_lock<std::mutex> lock(owner.mutex, std::defer_lock);
            // Foreground admission may be called by a page callback. A live IQ
            // stream holds this mutex until cancellation: never wait on it here.
            if (kind == 1)
            {
                if (!lock.try_lock()) return false;
            }
            else lock.lock();
            if (!owner.enabled) return owner.fail(kind, "Radio disabled by user");
            if (kind == 1 && owner.references[1]) return owner.fail(kind, "A foreground radio lease is already active");
            if (!owner.references[0] && !owner.references[1])
            {
                if (!owner.driver->acquire()) return owner.fail(kind, owner.driver->lastError());
                owner.physical_acquired = true;
                owner.restore_failed = false;
                owner.configuration_valid = true;
                owner.configuration_error.clear();
            }
            else if (!owner.physical_acquired || !owner.driver->isOnline()) return owner.fail(kind, "Radio recovery requires releasing existing leases");
            ++owner.references[kind];
            owner.errors[kind].clear();
            return true;
        }
        void release() override
        {
            (void)releaseWithStatus();
        }
        bool releaseWithStatus() override
        {
            std::lock_guard<std::mutex> lock(owner.mutex);
            if (!owner.references[kind]) return false;
            --owner.references[kind];
            if (kind == 1 && owner.references[0] && owner.has_mesh_config)
            {
                owner.restore_failed = !owner.driver->configureLoRa(owner.mesh_config) || !owner.driver->startReceive();
                owner.configuration_valid = !owner.restore_failed;
                if (owner.restore_failed) owner.failConfiguration(0);
                else
                {
                    owner.configuration_error.clear();
                    owner.errors[0].clear();
                }
            }
            if (!owner.references[0] && !owner.references[1] && owner.physical_acquired)
            {
                owner.driver->release();
                owner.physical_acquired = false;
                owner.configuration_valid = false;
            }
            return !owner.restore_failed;
        }
        bool configureLoRa(const RadioSettings& settings) override
        {
            std::lock_guard<std::mutex> lock(owner.mutex);
            if (!owner.enabled) return owner.fail(kind, "Radio disabled by user");
            if (!owner.physical_acquired) return owner.fail(kind, "Radio requires acquisition");
            if (!owner.references[kind]) return owner.fail(kind, "Radio lease required");
            if (kind == 0 && owner.references[1])
            {
                owner.mesh_config = settings;
                owner.has_mesh_config = true;
                owner.errors[kind].clear();
                return true;
            }
            if (!owner.driver->configureLoRa(settings)) return owner.failConfiguration(kind);
            owner.configuration_valid = true;
            owner.configuration_error.clear();
            if (kind == 0)
            {
                owner.mesh_config = settings;
                owner.has_mesh_config = true;
                owner.restore_failed = false;
            }
            owner.errors[kind].clear();
            return true;
        }
        bool startReceive() override
        {
            std::lock_guard<std::mutex> lock(owner.mutex);
            if (!owner.enabled) return owner.fail(kind, "Radio disabled by user");
            if (!owner.physical_acquired) return owner.fail(kind, "Radio requires acquisition");
            if (!owner.references[kind]) return owner.fail(kind, "Radio lease required");
            if (kind == 0 && owner.references[1]) return true;
            if (!owner.allowed(kind)) return false;
            return owner.driver->startReceive() || owner.failConfiguration(kind);
        }
        bool supportsFsk() const override { return kind == 1 && owner.driver->supportsFsk(); }
        bool supportsIqCapture() const override { return kind == 1 && owner.driver->supportsIqCapture(); }
        bool supportsOneSegIq() const override { return kind == 1 && owner.driver->supportsOneSegIq(); }
        bool supportsFmIq() const override { return kind == 1 && owner.driver->supportsFmIq(); }
        bool streamFmIq(const FmIqRequest& request, FmIqResult& result) override
        {
            std::lock_guard<std::mutex> lock(owner.mutex);
            result = {};
            result.capture_error = static_cast<int>(IqCaptureError::State);
            if (kind != 1) return owner.fail(kind, "FM IQ requires a foreground radio lease");
            if (!owner.allowed(kind)) return false;
            if (!owner.driver->supportsFmIq()) return owner.fail(kind, "FM IQ backend is unavailable");
            if (!platform::shared::radio::FmLr2021Rx::frequencySupported(request.frequency_hz) ||
                !request.staging || request.staging_bytes < 1280 || !request.accept || !request.cancelled)
            {
                result.capture_error = static_cast<int>(IqCaptureError::Argument);
                return owner.fail(kind, "FM IQ requires a supported frequency, staging and callbacks");
            }
            const bool completed = owner.driver->streamFmIq(request, result);
            if (result.recovery_error || !owner.driver->isOnline()) return owner.failConfiguration(kind);
            if (!completed || result.capture_error || result.sink_rejected)
            {
                const auto* message = owner.driver->lastError();
                return owner.fail(kind, message && *message ? message : "FM IQ stream failed");
            }
            owner.errors[kind].clear();
            return true;
        }
        bool streamOneSegIq(const OneSegIqRequest& request, OneSegIqResult& result) override
        {
            std::lock_guard<std::mutex> lock(owner.mutex);
            result = {};
            result.capture_error = static_cast<int>(IqCaptureError::State);
            if (kind != 1) return owner.fail(kind, "TV IQ requires a foreground radio lease");
            if (!owner.allowed(kind)) return false;
            if (!owner.driver->supportsOneSegIq()) return owner.fail(kind, "TV IQ backend is unavailable");
            if (request.channel < 13 || request.channel > 62 || !request.staging ||
                request.staging_bytes < 1280 || !request.accept || !request.cancelled)
            {
                result.capture_error = static_cast<int>(IqCaptureError::Argument);
                return owner.fail(kind, "TV IQ requires a valid channel, staging and callbacks");
            }
            const bool completed = owner.driver->streamOneSegIq(request, result);
            if (result.recovery_error || !owner.driver->isOnline()) return owner.failConfiguration(kind);
            if (!completed || result.capture_error || result.sink_rejected)
            {
                const auto* message = owner.driver->lastError();
                return owner.fail(kind, message && *message ? message : "TV IQ stream failed");
            }
            owner.errors[kind].clear();
            return true;
        }
        bool captureIq(uint32_t* words, uint32_t* scratch, size_t capacity, RadioIqCaptureResult& result) override
        {
            std::lock_guard<std::mutex> lock(owner.mutex);
            result = {};
            result.capture_error = static_cast<int>(IqCaptureError::State);
            if (kind != 1) return owner.fail(kind, "IQ requires a foreground radio lease");
            if (!owner.allowed(kind)) return false;
            if (!owner.driver->supportsIqCapture()) return owner.fail(kind, "IQ backend is unavailable");
            const auto a = reinterpret_cast<std::uintptr_t>(words);
            const auto b = reinterpret_cast<std::uintptr_t>(scratch);
            if (!words || !scratch || capacity < 256 || (a > b ? a - b : b - a) < 1024)
            {
                result.capture_error = static_cast<int>(IqCaptureError::Argument);
                return owner.fail(kind, "IQ requires two non-overlapping 256-word buffers");
            }
            const bool captured = owner.driver->captureIq(words, scratch, capacity, result);
            if (result.restore_error || result.recovery_error || !owner.driver->isOnline())
            {
                result.bytes = 0;
                result.nominal_rate_hz = 0;
                return owner.failConfiguration(kind);
            }
            if (!captured || result.capture_error || result.bytes != 1024)
            {
                if (!result.capture_error) result.capture_error = static_cast<int>(IqCaptureError::Data);
                result.bytes = 0;
                result.nominal_rate_hz = 0;
                const auto* message = owner.driver->lastError();
                return owner.fail(kind, message && *message ? message : "IQ capture failed");
            }
            owner.errors[kind].clear();
            return true;
        }
        bool supportsFskConfiguration(const FskSettings& settings) const override
        {
            return kind == 1 && owner.driver->supportsFskConfiguration(settings);
        }
        bool configureFsk(const FskSettings& settings) override
        {
            std::lock_guard<std::mutex> lock(owner.mutex);
            if (kind != 1) return owner.fail(kind, "FSK requires a foreground radio lease");
            // Configuration must remain available as the recovery operation.
            if (!owner.enabled || !owner.physical_acquired || !owner.references[kind])
                return owner.fail(kind, "FSK requires an enabled, acquired radio lease");
            if (!owner.driver->supportsFsk()) return owner.fail(kind, "FSK backend is unavailable");
            if (!owner.driver->configureFsk(settings)) return owner.failConfiguration(kind);
            owner.configuration_valid = true;
            owner.configuration_error.clear();
            owner.errors[kind].clear();
            return true;
        }
        bool transmit(const uint8_t* data, std::size_t size) override
        {
            std::lock_guard<std::mutex> lock(owner.mutex);
            if (!owner.allowed(kind)) return false;
            return owner.driver->transmit(data, size) || owner.fail(kind, owner.driver->lastError());
        }
        bool pollReceive(RadioPacket* packet) override
        {
            std::lock_guard<std::mutex> lock(owner.mutex);
            return owner.allowed(kind) && owner.driver->pollReceive(packet);
        }
        float readRssi() override
        {
            std::lock_guard<std::mutex> lock(owner.mutex);
            return owner.allowed(kind) ? owner.driver->readRssi() : -127.0f;
        }
        bool isOnline() const override
        {
            std::unique_lock<std::mutex> operation(owner.mutex, std::try_to_lock);
            if (operation.owns_lock())
            {
                const bool online = owner.enabled && owner.physical_acquired && owner.configuration_valid && owner.references[kind] && owner.driver->isOnline() && !(kind == 0 && owner.restore_failed);
                std::lock_guard<std::mutex> cache(owner.cache_mutex);
                owner.cached_online[kind] = online;
                return online;
            }
            std::lock_guard<std::mutex> cache(owner.cache_mutex);
            return owner.enabled && owner.cached_online[kind];
        }
        const char* lastError() const override
        {
            thread_local std::string message;
            std::unique_lock<std::mutex> operation(owner.mutex, std::try_to_lock);
            if (operation.owns_lock())
            {
                message = owner.errors[kind].empty() ? owner.driver->lastError() : owner.errors[kind];
                std::lock_guard<std::mutex> cache(owner.cache_mutex);
                owner.cached_errors[kind] = message;
            }
            else
            {
                std::lock_guard<std::mutex> cache(owner.cache_mutex);
                message = owner.cached_errors[kind];
            }
            return message.c_str();
        }
        RadioSettings appliedLoRaConfig() const override
        {
            std::unique_lock<std::mutex> operation(owner.mutex, std::try_to_lock);
            if (operation.owns_lock())
            {
                const auto settings = kind == 0 && owner.has_mesh_config ? owner.mesh_config : owner.driver->appliedLoRaConfig();
                std::lock_guard<std::mutex> cache(owner.cache_mutex);
                owner.cached_settings[kind] = settings;
                return settings;
            }
            std::lock_guard<std::mutex> cache(owner.cache_mutex);
            return owner.cached_settings[kind];
        }
        RadioStatistics stats() const override
        {
            std::unique_lock<std::mutex> operation(owner.mutex, std::try_to_lock);
            if (operation.owns_lock())
            {
                auto result = owner.driver->stats();
                result.online = owner.enabled && owner.physical_acquired && owner.configuration_valid && owner.references[kind] && result.online && !(kind == 0 && owner.restore_failed);
                if (kind == 0 && owner.has_mesh_config) result.lora_config = owner.mesh_config;
                std::lock_guard<std::mutex> cache(owner.cache_mutex);
                owner.cached_stats[kind] = result;
                return result;
            }
            std::lock_guard<std::mutex> cache(owner.cache_mutex);
            auto result = owner.cached_stats[kind];
            if (!owner.enabled) result.online = false;
            return result;
        }
    };

    explicit Impl(std::unique_ptr<IPacketRadio> backend) : driver(std::move(backend)), mesh_client(*this, 0), foreground_client(*this, 1)
    {
        if (!driver) throw std::invalid_argument("Packet radio backend is required");
    }
    ~Impl()
    {
        if (physical_acquired) driver->release();
    }
    bool fail(unsigned kind, std::string message)
    {
        errors[kind] = std::move(message);
        return false;
    }
    bool failConfiguration(unsigned kind)
    {
        configuration_valid = false;
        configuration_error = driver->lastError();
        if (configuration_error.empty()) configuration_error = "Radio configuration or receive setup failed";
        return fail(kind, configuration_error);
    }
    bool allowed(unsigned kind)
    {
        if (!enabled) return fail(kind, "Radio disabled by user");
        if (!physical_acquired) return fail(kind, "Radio requires acquisition");
        if (!references[kind]) return fail(kind, "Radio lease required");
        if (kind == 0 && references[1]) return fail(kind, "Radio temporarily owned by a foreground tool");
        if (!configuration_valid) return fail(kind, configuration_error.empty() ? "Radio configuration is not ready" : configuration_error);
        if (kind == 0 && restore_failed) return fail(kind, "Radio configuration restore failed");
        errors[kind].clear();
        return true;
    }
    std::mutex mutex;
    std::mutex cache_mutex;
    bool cached_online[2]{};
    std::string cached_errors[2];
    RadioSettings cached_settings[2];
    RadioStatistics cached_stats[2];
    std::unique_ptr<IPacketRadio> driver;
    unsigned references[2]{};
    std::string errors[2];
    RadioSettings mesh_config;
    bool has_mesh_config = false;
    bool restore_failed = false;
    bool configuration_valid = false;
    std::string configuration_error;
    std::atomic_bool enabled{true};
    bool physical_acquired = false;
    Client mesh_client;
    Client foreground_client;
};

PacketRadioCoordinator::PacketRadioCoordinator(std::unique_ptr<IPacketRadio> driver)
    : impl_(std::make_unique<Impl>(std::move(driver))) {}
PacketRadioCoordinator::~PacketRadioCoordinator() = default;
IPacketRadio& PacketRadioCoordinator::mesh() { return impl_->mesh_client; }
IPacketRadio& PacketRadioCoordinator::foreground() { return impl_->foreground_client; }
bool PacketRadioCoordinator::setEnabled(bool enabled)
{
    // Quick controls must not block the UI behind an open-ended IQ worker.
    std::unique_lock<std::mutex> lock(impl_->mutex, std::try_to_lock);
    if (!lock.owns_lock()) return false;
    if (impl_->references[1]) return false;
    if (!enabled && impl_->physical_acquired)
    {
        impl_->driver->release();
        impl_->physical_acquired = false;
        impl_->configuration_valid = false;
    }
    impl_->enabled = enabled;
    return true;
}
bool PacketRadioCoordinator::enabled() const
{
    return impl_->enabled.load();
}

namespace
{
class Sx126xPacketRadio final : public IPacketRadio
{
  public:
    explicit Sx126xPacketRadio(PacketRadioHardware hardware) : hardware_(std::move(hardware)) {}
    const char* name() const override { return "SX126x"; }
    bool hardwareCandidatePresent() const override
    {
        std::error_code error;
        return std::filesystem::exists(hardware_.bus.spi_device, error) &&
               std::filesystem::exists(hardware_.bus.gpiochip, error);
    }
    bool acquire() override { return driver().acquire(hardware_.bus); }
    void release() override { driver().release(); }
    bool configureLoRa(const RadioSettings& config) override { return driver().configureLoRa(config); }
    bool supportsFsk() const override { return true; }
    bool supportsFskConfiguration(const FskSettings& settings) const override { return valid_sx126x_fsk_settings(settings); }
    bool configureFsk(const FskSettings& config) override { return driver().configureFsk(config); }
    bool startReceive() override { return driver().startReceive(); }
    bool transmit(const uint8_t* data, std::size_t size) override { return driver().transmit(data, size); }
    bool pollReceive(RadioPacket* packet) override { return driver().pollReceive(packet); }
    float readRssi() override { return driver().readRssi(); }
    bool isOnline() const override { return driver().isOnline(); }
    const char* lastError() const override { return driver().lastError(); }
    RadioSettings appliedLoRaConfig() const override { return driver().appliedLoRaConfig(); }
    RadioStatistics stats() const override { return driver().stats(); }

  private:
    static Sx126xRadio& driver() { return Sx126xRadio::instance(); }
    PacketRadioHardware hardware_;
};

class UnavailablePacketRadio final : public IPacketRadio
{
  public:
    explicit UnavailablePacketRadio(std::string error) : error_(std::move(error)) {}
    const char* name() const override { return "unavailable"; }
    bool hardwareCandidatePresent() const override { return false; }
    bool acquire() override { return false; }
    void release() override {}
    bool configureLoRa(const RadioSettings&) override { return false; }
    bool startReceive() override { return false; }
    bool transmit(const uint8_t*, std::size_t) override { return false; }
    bool pollReceive(RadioPacket*) override { return false; }
    float readRssi() override { return -127; }
    bool isOnline() const override { return false; }
    const char* lastError() const override { return error_.c_str(); }
    RadioSettings appliedLoRaConfig() const override { return {}; }
    RadioStatistics stats() const override { return {}; }

  private:
    std::string error_;
};

struct RadioRuntime
{
    RadioRuntime() { hardware.bus = Sx126xRadio::defaultConfigFromEnvironment(); }
    std::mutex mutex;
    PacketRadioHardware hardware;
    std::unique_ptr<PacketRadioCoordinator> coordinator;
    PacketRadioCoordinator& get()
    {
        if (!coordinator)
        {
            std::unique_ptr<IPacketRadio> driver;
            if (hardware.driver == "sx126x") driver = std::make_unique<Sx126xPacketRadio>(hardware);
#if defined(TRAIL_MATE_LINUX_HAS_LR2021)
            else if (hardware.driver == "lr2021") driver = make_lr2021_radio(hardware);
#endif
            else driver = std::make_unique<UnavailablePacketRadio>("Radio backend not built or disabled: " + hardware.driver);
            coordinator = std::make_unique<PacketRadioCoordinator>(std::move(driver));
        }
        return *coordinator;
    }
};
RadioRuntime& runtime()
{
    // The legacy singleton must outlive wrappers that may release a lease.
    (void)Sx126xRadio::instance();
    static RadioRuntime instance;
    return instance;
}
} // namespace

std::unique_ptr<IPacketRadio> make_unavailable_packet_radio(const char* reason)
{
    return std::make_unique<UnavailablePacketRadio>(reason ? reason : "Physical radio unavailable");
}
bool configure_packet_radio(const PacketRadioHardware& hardware)
{
#if !defined(TRAIL_MATE_LINUX_HAS_LR2021)
    if (hardware.driver == "lr2021") return false;
#endif
    auto& state = runtime();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.coordinator) return false;
    state.hardware = hardware;
    return true;
}
IPacketRadio& mesh_packet_radio()
{
    auto& state = runtime();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.get().mesh();
}
IPacketRadio& foreground_packet_radio()
{
    auto& state = runtime();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.get().foreground();
}
bool set_packet_radio_enabled(bool enabled)
{
    return packet_radio_power().setEnabled(enabled);
}
bool packet_radio_enabled()
{
    return packet_radio_power().enabled();
}
IRadioPowerControl& packet_radio_power()
{
    auto& state = runtime();
    std::lock_guard<std::mutex> lock(state.mutex);
    // The configured coordinator has process lifetime. Never retain the factory
    // mutex while an operation waits for a physical transaction to complete.
    return state.get();
}
} // namespace platform::linux_runtime
