#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "platform/linux/oneseg_decoder_process.h"
#include "platform/linux/oneseg_ofdm_environment.h"
#include "platform/linux/oneseg_ofdm_wire.h"
#include "platform/linux/oneseg_pipe_sink.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

namespace platform::linux_runtime
{
namespace
{
using Clock = std::chrono::steady_clock;
static_assert(std::atomic_bool::is_always_lock_free, "Decoder health must be safe in the FIFO cancellation path");
struct Pipe
{
    int fd[2]{-1, -1};
    ~Pipe()
    {
        for (int value : fd)
            if (value >= 0) ::close(value);
    }
    bool create()
    {
        if (::pipe2(fd, O_CLOEXEC) != 0) return false;
        // Spawn mappings must also work when a standard descriptor is closed.
        for (int& value : fd)
            if (value < 4)
            {
                const int elevated = fcntl(value, F_DUPFD_CLOEXEC, 4);
                ::close(value);
                value = elevated;
                if (value < 0) return false;
            }
        return true;
    }
    int take(unsigned end)
    {
        const int result = fd[end];
        fd[end] = -1;
        return result;
    }
};
bool absolute(const std::string& path)
{
    return !path.empty() && path.front() == '/' && path.size() <= 4096 && path.find('\0') == std::string::npos;
}
} // namespace

struct OneSegDecoderProcess::Impl
{
    Config config;
    std::vector<std::string> original_reception_environment;
    bool reception_overridden = false;
    bool source_overridden = false;
    std::vector<std::string> original_source_environment;
    OneSegLabSource original_diagnostic_source = OneSegLabSource::Unavailable;
    void resetInputSource()
    {
        if (!source_overridden) return;
        config.environment = std::move(original_source_environment);
        config.diagnostic_source = original_diagnostic_source;
        source_overridden = false;
    }
    mutable std::mutex mutex;
    std::condition_variable changed;
    Snapshot state;
    std::atomic_bool ok{false}, error_ready{false}, stopping{false};
    std::atomic<uint16_t> requested_program{0};
    std::thread reader;
    pid_t pid = -1; // Protected with mutex, including waitpid and kill (PID reuse).
    bool ready = false;
    std::array<int, 3> outputs{{-1, -1, -1}}; // Ready, TS, diagnostics; reader owns.
    std::array<uint8_t, 4096> bytes{};
    std::array<uint8_t, 4096> diagnostic_text{};
    OneSegOfdmWire ofdm_wire;
    OneSegCarrierCollector carrier_collector;
    std::array<uint8_t, 188> packet{};
    size_t packet_used = 0;
    uint64_t generation = 0;
    platform::shared::radio::TransportStreamPrograms programmes{
        platform::shared::radio::TransportStreamPrograms::Discovery::AribPartial};
    OneSegServiceInformation service_information;
    OneSegPipeSink pipe;

    explicit Impl(Config value) : config(std::move(value)), pipe({this, [](void* context, unsigned, uint64_t, const std::atomic_bool& cancel)
                                                                  { return static_cast<Impl*>(context)->launch(cancel); },
                                                                  config.automatic_parameters ? config.startup_timeout_ms : 0}) {}
    void fault(const char* message)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!error_ready.load(std::memory_order_relaxed))
        {
            std::snprintf(state.message, sizeof(state.message), "%s", message);
            state.phase = Phase::Error;
            error_ready.store(true, std::memory_order_release);
        }
        ok.store(false, std::memory_order_release);
        changed.notify_all();
    }
    bool validConfig() const
    {
        if (!absolute(config.python) || !absolute(config.decoder) ||
            config.startup_timeout_ms < 100 || config.startup_timeout_ms > 30000 || config.environment.size() > 64) return false;
        size_t total = 0;
        for (const auto& entry : config.environment)
        {
            const size_t split = entry.find('=');
            if (!split || split == std::string::npos || entry.find('\0') != std::string::npos) return false;
            for (size_t i = 0; i < split; ++i)
                if (!(entry[i] == '_' || (entry[i] >= 'A' && entry[i] <= 'Z') ||
                      (entry[i] >= 'a' && entry[i] <= 'z') || (i && entry[i] >= '0' && entry[i] <= '9'))) return false;
            total += entry.size();
            if (total > 65536) return false;
        }
        return true;
    }
    int launch(const std::atomic_bool& cancel)
    {
        if (!validConfig())
        {
            fault("Invalid decoder process configuration");
            return -1;
        }
        if (cancel.load()) return -1;
        std::vector<std::string> env;
        for (char** entry = environ; entry && *entry; ++entry) env.emplace_back(*entry);
        for (const auto& entry : config.environment)
        {
            const std::string key = entry.substr(0, entry.find('=') + 1);
            bool replaced = false;
            for (auto& existing : env)
                if (existing.compare(0, key.size(), key) == 0)
                {
                    existing = entry;
                    replaced = true;
                    break;
                }
            if (!replaced) env.push_back(entry);
        }
        std::vector<char*> environment;
        for (auto& entry : env) environment.push_back(entry.data());
        environment.push_back(nullptr);
        std::array<char*, 7> arguments{{config.python.data(), config.decoder.data(),
                                        const_cast<char*>("--profile"), const_cast<char*>(config.automatic_parameters ? "auto" : "onesegcast-mode3-gi16-qpsk23-ti4"),
                                        const_cast<char*>("--ready-fd"), const_cast<char*>("3"), nullptr}};
        Pipe input, transport, diagnostics, handshake;
        if (!input.create() || !transport.create() || !diagnostics.create() || !handshake.create())
        {
            fault("Cannot create decoder pipes");
            return -1;
        }
        for (int value : {transport.fd[0], diagnostics.fd[0], handshake.fd[0]})
        {
            const int flags = fcntl(value, F_GETFL);
            if (flags < 0 || fcntl(value, F_SETFL, flags | O_NONBLOCK) != 0)
            {
                fault("Cannot configure decoder output pipes");
                return -1;
            }
        }
        posix_spawn_file_actions_t actions;
        posix_spawnattr_t attributes;
        const int action_init = posix_spawn_file_actions_init(&actions);
        if (action_init)
        {
            fault("Cannot initialize decoder spawn actions");
            return -1;
        }
        const int attribute_init = posix_spawnattr_init(&attributes);
        if (attribute_init)
        {
            posix_spawn_file_actions_destroy(&actions);
            fault("Cannot initialize decoder spawn attributes");
            return -1;
        }
        int result = 0;
        auto check = [&result](int code)
        { if (!result) result = code; };
        check(posix_spawn_file_actions_adddup2(&actions, input.fd[0], 0));
        check(posix_spawn_file_actions_adddup2(&actions, transport.fd[1], 1));
        check(posix_spawn_file_actions_adddup2(&actions, diagnostics.fd[1], 2));
        check(posix_spawn_file_actions_adddup2(&actions, handshake.fd[1], 3));
#if defined(__GLIBC__)
#if __GLIBC_PREREQ(2, 34)
        check(posix_spawn_file_actions_addclosefrom_np(&actions, 4));
#else
        result = ENOTSUP; // Do not leak application/radio descriptors into a child.
#endif
#else
        result = ENOTSUP;
#endif
        sigset_t mask, defaults;
        sigemptyset(&mask);
        sigemptyset(&defaults);
        for (int value : {SIGTERM, SIGINT, SIGPIPE}) sigaddset(&defaults, value);
        check(posix_spawnattr_setsigmask(&attributes, &mask));
        check(posix_spawnattr_setsigdefault(&attributes, &defaults));
        check(posix_spawnattr_setpgroup(&attributes, 0));
        check(posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETPGROUP));
        pid_t child = -1;
        if (!result) result = posix_spawn(&child, config.python.c_str(), &actions, &attributes,
                                          arguments.data(), environment.data());
        posix_spawnattr_destroy(&attributes);
        posix_spawn_file_actions_destroy(&actions);
        if (result)
        {
            fault("Cannot spawn isolated decoder process");
            return -1;
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            pid = child;
        }
        outputs = {{handshake.take(0), transport.take(0), diagnostics.take(0)}};
        try
        {
            reader = std::thread([this]
                                 { readLoop(); });
        }
        catch (...)
        {
            fault("Cannot start decoder monitor thread");
            signalChild(SIGKILL);
            int status = 0;
            while (waitpid(child, &status, 0) < 0 && errno == EINTR)
            {
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                pid = -1;
            }
            closeOutputs();
            return -1;
        }
        const auto deadline = Clock::now() + std::chrono::milliseconds(config.startup_timeout_ms);
        std::unique_lock<std::mutex> lock(mutex);
        while (!ready && !error_ready.load() && pid > 0 && !cancel.load() && Clock::now() < deadline)
            changed.wait_for(lock, std::chrono::milliseconds(10));
        const bool started = ready && !error_ready.load() && pid > 0 && !cancel.load();
        lock.unlock();
        if (!started)
        {
            if (!cancel.load() && !error_ready.load()) fault("Decoder startup handshake timed out");
            return -1;
        }
        return input.take(1);
    }
    void closeOutputs()
    {
        for (int& value : outputs)
        {
            if (value >= 0) ::close(value);
            value = -1;
        }
    }
    void signalChild(int signal)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (pid > 0) (void)::kill(pid, signal);
    }
    bool waitExit(unsigned milliseconds)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, std::chrono::milliseconds(milliseconds), [this]
                                { return pid < 0; });
    }
    bool reap()
    {
        bool exited = false, bad = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (pid < 0) return true;
            int status = 0;
            const pid_t result = waitpid(pid, &status, WNOHANG);
            if (result > 0)
            {
                if (WIFEXITED(status)) state.child_exit_code = WEXITSTATUS(status);
                if (WIFSIGNALED(status)) state.child_signal = WTERMSIG(status);
                bad = !stopping.load() || (state.child_exit_code != 0 && state.child_signal != SIGTERM);
                pid = -1;
                exited = true;
            }
            else if (result < 0 && errno != EINTR)
            {
                pid = -1;
                exited = bad = true;
            }
        }
        if (bad) fault("Decoder exited unexpectedly or lost child ownership");
        if (exited) changed.notify_all();
        return exited;
    }
    void transport(const uint8_t* data, size_t count)
    {
        const auto received_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
        for (size_t i = 0; i < count; ++i)
        {
            packet[packet_used++] = data[i];
            if (packet_used != packet.size()) continue;
            packet_used = 0;
            if (packet[0] != 0x47)
            {
                fault("Decoder emitted unaligned TS packets");
                return;
            }
            (void)programmes.accept(packet.data(), packet.size());
            service_information.select(programmes.snapshot().selected_program, programmes.snapshot().transport_id);
            (void)service_information.accept(packet.data(), packet.size(), received_ms);
            if (config.accept_ts && !config.accept_ts(config.ts_context, packet.data(), packet.size(), generation))
            {
                fault("TV transport queue rejected a packet");
                return;
            }
        }
        std::lock_guard<std::mutex> lock(mutex);
        state.ts_bytes += count;
        state.programmes = programmes.snapshot();
        state.service_information = service_information.snapshot();
    }
    void diagnostic(const uint8_t* data, size_t count)
    {
        std::lock_guard<std::mutex> lock(mutex);
        size_t plain = 0;
        OneSegSignalMeasurement signal;
        OneSegNativeConfiguration native_configuration;
        OneSegFecStatistics fec;
        OneSegCarrierChunk carrier;
        for (size_t index = 0; index < count; ++index)
        {
            OneSegPhaseObservation value;
            const auto result = ofdm_wire.feed(data[index], value, &signal, &native_configuration, &fec, &carrier);
            if (result == OneSegOfdmWire::Result::Ordinary)
            {
                if (plain < diagnostic_text.size()) diagnostic_text[plain++] = data[index];
            }
            else if (result == OneSegOfdmWire::Result::Invalid) ++state.rejected_ofdm_records;
            else if (result == OneSegOfdmWire::Result::Carrier)
            {
                const auto now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
                const auto accepted = carrier_collector.accept(carrier, config.diagnostic_source, now);
                if (accepted == OneSegCarrierCollector::Result::Rejected) ++state.rejected_ofdm_records;
                else if (accepted == OneSegCarrierCollector::Result::Burst) state.ofdm_carriers = carrier_collector.latest();
            }
            else if (result == OneSegOfdmWire::Result::Fec && config.diagnostic_source != OneSegLabSource::Unavailable)
            {
                const auto now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
                if (!state.fec.append(fec, config.diagnostic_source, now)) ++state.rejected_ofdm_records;
            }
            else if (result == OneSegOfdmWire::Result::Configuration && config.diagnostic_source != OneSegLabSource::Unavailable)
            {
                if (state.native_configuration.available) ++state.rejected_ofdm_records;
                else
                {
                    native_configuration.available = true;
                    native_configuration.source = config.diagnostic_source;
                    native_configuration.received_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
                    state.native_configuration = native_configuration;
                }
            }
            else if (result == OneSegOfdmWire::Result::Signal && config.diagnostic_source != OneSegLabSource::Unavailable)
            {
                auto& previous = state.ofdm_signal[signal.kind - 1];
                if (previous.count && signal.sequence <= previous.sequence) ++state.rejected_ofdm_records;
                else
                {
                    signal.source = config.diagnostic_source;
                    signal.updated_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
                    previous = signal;
                }
            }
            else if (result == OneSegOfdmWire::Result::Sample && config.diagnostic_source != OneSegLabSource::Unavailable)
            {
                const auto now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
                if (!state.ofdm_phase.append(value, config.diagnostic_source, now)) ++state.rejected_ofdm_records;
            }
        }
        data = diagnostic_text.data();
        count = plain;
        if (!count) return;
        constexpr size_t capacity = sizeof(state.stderr_tail) - 1;
        size_t previous = std::strlen(state.stderr_tail);
        if (count >= capacity)
        {
            data += count - capacity;
            count = capacity;
            previous = 0;
        }
        else if (previous + count > capacity)
        {
            const size_t discard = previous + count - capacity;
            std::memmove(state.stderr_tail, state.stderr_tail + discard, previous - discard);
            previous -= discard;
        }
        std::memcpy(state.stderr_tail + previous, data, count);
        state.stderr_tail[previous + count] = 0;
    }
    void readLoop()
    {
        (void)pthread_setname_np(pthread_self(), "oneseg-ts-read");
        bool child_exited = false;
        auto drain_deadline = Clock::time_point::max();
        for (;;)
        {
            try
            {
                if (const uint16_t selection = requested_program.exchange(0))
                {
                    (void)programmes.selectProgram(selection);
                    service_information.select(programmes.snapshot().selected_program, programmes.snapshot().transport_id);
                    std::lock_guard<std::mutex> lock(mutex);
                    state.programmes = programmes.snapshot();
                    state.service_information = service_information.snapshot();
                }
                pollfd descriptors[3];
                for (size_t i = 0; i < outputs.size(); ++i) descriptors[i] = {outputs[i], POLLIN | POLLHUP | POLLERR, 0};
                if (::poll(descriptors, 3, 10) < 0 && errno != EINTR) fault("Decoder monitor poll failed");
                for (size_t i = 0; i < outputs.size(); ++i)
                {
                    if (outputs[i] < 0 || !descriptors[i].revents) continue;
                    for (unsigned batch = 0; batch < 16; ++batch)
                    {
                        const ssize_t count = ::read(outputs[i], bytes.data(), bytes.size());
                        if (count > 0)
                        {
                            if (i == 0)
                            {
                                if (count != 1 || bytes[0] != 'R') fault("Invalid decoder ready handshake");
                                else
                                {
                                    std::lock_guard<std::mutex> lock(mutex);
                                    ready = true;
                                    changed.notify_all();
                                }
                            }
                            else if (i == 1 && !error_ready.load()) transport(bytes.data(), static_cast<size_t>(count));
                            else if (i == 2) diagnostic(bytes.data(), static_cast<size_t>(count));
                            continue;
                        }
                        if (count < 0 && errno == EINTR) continue;
                        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
                        if (i == 0)
                        {
                            bool was_ready;
                            {
                                std::lock_guard<std::mutex> lock(mutex);
                                was_ready = ready;
                            }
                            if (!was_ready && !stopping.load()) fault("Decoder closed its startup handshake");
                        }
                        else if (i == 1 && (packet_used || !stopping.load())) fault("Decoder closed TS output prematurely");
                        ::close(outputs[i]);
                        outputs[i] = -1;
                        break;
                    }
                }
            }
            catch (...)
            {
                fault("Decoder output processing failed");
            }
            if (!child_exited && reap())
            {
                child_exited = true;
                drain_deadline = Clock::now() + std::chrono::milliseconds(200);
            }
            if (child_exited && ((outputs[0] < 0 && outputs[1] < 0 && outputs[2] < 0) || Clock::now() >= drain_deadline)) break;
        }
        closeOutputs();
        changed.notify_all();
    }
};

OneSegDecoderProcess::OneSegDecoderProcess(Config config) : impl_(std::make_unique<Impl>(std::move(config))) {}
OneSegDecoderProcess::~OneSegDecoderProcess() { (void)finish(); }
bool OneSegDecoderProcess::open(unsigned channel, uint64_t generation, const std::atomic_bool& cancel)
{
    auto& s = *impl_;
    if (s.reader.joinable()) return false;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        s.state = {};
        s.state.phase = Phase::Starting;
        s.state.generation = s.generation = generation;
        s.ready = false;
        s.error_ready = s.ok = s.stopping = false;
    }
    s.programmes.reset();
    s.service_information.reset();
    s.requested_program = 0;
    s.packet_used = 0;
    s.ofdm_wire = {};
    s.carrier_collector.reset();
    if (!s.pipe.open(channel, generation, cancel))
    {
        if (!cancel.load() && !s.error_ready.load()) s.fault(s.pipe.lastError());
        (void)finish();
        return false;
    }
    bool active;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        active = s.pid > 0 && !s.error_ready.load() && !cancel.load();
        if (active)
        {
            s.state.phase = Phase::Running;
            s.ok.store(true, std::memory_order_release);
        }
    }
    if (!active) (void)finish();
    return active;
}
bool OneSegDecoderProcess::write(const uint8_t* data, size_t bytes, uint64_t first_pair, uint64_t generation,
                                 const std::atomic_bool& cancel)
{
    auto& s = *impl_;
    if (!healthy()) return false;
    if (s.pipe.write(data, bytes, first_pair, generation, cancel)) return true;
    if (!cancel.load()) s.fault(s.pipe.lastError());
    return false;
}
bool OneSegDecoderProcess::finish()
{
    auto& s = *impl_;
    s.stopping = true;
    s.ok = false;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.error_ready.load()) s.state.phase = Phase::Stopping;
    }
    // Automatic acquisition distinguishes no signal from explicit cancellation.
    // Notify it before EOF, which otherwise means an insufficient capture.
    if (s.config.automatic_parameters && !s.error_ready.load(std::memory_order_acquire)) s.signalChild(SIGTERM);
    if (!s.pipe.finish()) s.fault(s.pipe.lastError());
    if (s.reader.joinable())
    {
        if (!s.waitExit(500))
        {
            s.signalChild(SIGTERM);
            if (!s.waitExit(1000))
            {
                {
                    std::lock_guard<std::mutex> lock(s.mutex);
                    s.state.forced_kill = true;
                }
                s.fault("Decoder did not stop and required SIGKILL");
                s.signalChild(SIGKILL);
            }
        }
        s.reader.join();
    }
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        s.state.phase = s.error_ready.load() ? Phase::Error : Phase::Stopped;
    }
    return !s.error_ready.load(std::memory_order_acquire);
}
bool OneSegDecoderProcess::configureExperiment(const OneSegOfdmExperiment& experiment, uint32_t input_rate)
{
    auto& s = *impl_;
    if (s.reader.joinable() || !experiment.valid(input_rate)) return false;
    s.resetInputSource();
    if (s.reception_overridden)
    {
        s.config.environment = std::move(s.original_reception_environment);
        s.reception_overridden = false;
    }
    auto environment = s.config.environment;
    const std::string prefix = "TRAIL_MATE_OFDM_";
    environment.erase(std::remove_if(environment.begin(), environment.end(), [&](const auto& item)
                                     { return item.compare(0, prefix.size(), prefix) == 0; }),
                      environment.end());
    for (auto& entry : oneseg_ofdm_environment(experiment)) environment.push_back(std::move(entry));
    s.config.environment = std::move(environment);
    s.config.automatic_parameters = experiment.automatic_waveform;
    return true;
}
bool OneSegDecoderProcess::configureReception(const OneSegReceptionSettings& reception)
{
    if (impl_->reader.joinable()) return false;
    impl_->resetInputSource();
    auto& environment = impl_->config.environment;
    if (!reception.enabled)
    {
        if (impl_->reception_overridden)
        {
            environment = std::move(impl_->original_reception_environment);
            impl_->reception_overridden = false;
        }
        return true;
    }
    if (!reception.valid()) return false;
    if (!impl_->reception_overridden)
    {
        impl_->original_reception_environment = environment;
        impl_->reception_overridden = true;
    }
    const std::string prefix = "TRAIL_MATE_ONESEG_INPUT_RATE=";
    environment.erase(std::remove_if(environment.begin(), environment.end(), [&](const auto& value)
                                     { return value.compare(0, prefix.size(), prefix) == 0; }),
                      environment.end());
    environment.push_back(prefix + std::to_string(reception.sample_rate_hz));
    return true;
}
bool OneSegDecoderProcess::configureRecordedIq(uint32_t rate)
{
    auto& s = *impl_;
    if (s.reader.joinable() || (rate != 450000 && rate != 500000 && rate != 576000)) return false;
    if (!configureReception({})) return false;
    s.original_source_environment = s.config.environment;
    s.original_diagnostic_source = s.config.diagnostic_source;
    s.source_overridden = true;
    s.config.diagnostic_source = OneSegLabSource::RecordedIq;
    auto& environment = s.config.environment;
    const std::string rate_key = "TRAIL_MATE_ONESEG_INPUT_RATE=", diagnostic_key = "TRAIL_MATE_OFDM_DIAGNOSTICS=";
    environment.erase(std::remove_if(environment.begin(), environment.end(), [&](const auto& value)
                                     { return value.compare(0, rate_key.size(), rate_key) == 0 || value.compare(0, diagnostic_key.size(), diagnostic_key) == 0; }),
                      environment.end());
    environment.push_back(rate_key + std::to_string(rate));
    environment.emplace_back("TRAIL_MATE_OFDM_DIAGNOSTICS=1");
    return true;
}
bool OneSegDecoderProcess::healthy() const { return impl_->ok.load(std::memory_order_acquire); }
bool OneSegDecoderProcess::configureSynthetic()
{
    auto& s = *impl_;
    if (s.reader.joinable() || !configureReception({})) return false;
    s.original_source_environment = s.config.environment;
    s.original_diagnostic_source = s.config.diagnostic_source;
    s.source_overridden = true;
    s.config.diagnostic_source = OneSegLabSource::Synthetic;
    s.config.environment.emplace_back("TRAIL_MATE_OFDM_SYNTHETIC=1");
    return true;
}
const char* OneSegDecoderProcess::lastError() const
{
    return impl_->error_ready.load(std::memory_order_acquire) ? impl_->state.message : "";
}
OneSegDecoderProcess::Snapshot OneSegDecoderProcess::snapshot() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto result = impl_->state;
    if (result.phase != Phase::Running) result.service_information = {};
    else OneSegServiceInformation::expire(result.service_information, static_cast<uint64_t>(
                                                                          std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count()));
    return result;
}
bool OneSegDecoderProcess::selectProgram(uint16_t number)
{
    auto& s = *impl_;
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.state.phase != Phase::Running) return false;
    for (size_t i = 0; i < s.state.programmes.program_count; ++i)
        if (s.state.programmes.programs[i].number == number)
        {
            s.requested_program.store(number);
            return true;
        }
    return false;
}
} // namespace platform::linux_runtime
