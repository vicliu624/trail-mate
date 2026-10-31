#include "platform/linux/packet_radio.h"
#include "platform/ui/lora_receive_session.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
namespace lora = platform::ui::lora;
struct FakeIo
{
    std::mutex mutex;
    std::condition_variable wake;
    std::thread::id ui = std::this_thread::get_id();
    bool block_acquire = true, block_poll = false, block_tx = false, block_config = false;
    bool acquire_entered = false, poll_entered = false, tx_entered = false, config_entered = false;
    bool fail_config = false, fail_tx = false;
    bool block_release = false, release_entered = false;
    unsigned releases = 0, remaining = 0, emitted = 0, empty_polls = 0, next_count = 1;
    uint8_t marker = 0;
    std::vector<float> configurations;
    std::vector<uint8_t> transmitted;
} io;
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
void offUi() { require(std::this_thread::get_id() != io.ui, "hardware I/O reached the UI thread"); }
template <typename Predicate>
void await(Predicate predicate, const char* message)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (!predicate())
    {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error(message);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
template <typename Predicate>
auto inspect(Predicate predicate)
{
    std::lock_guard<std::mutex> lock(io.mutex);
    return predicate();
}
void ready(uint32_t generation)
{
    await([&]
          { const auto state = lora::receive_session_snapshot(); return state.generation == generation && state.state == lora::ReceiveSessionState::Ready; },
          "receive configuration did not become ready");
}
// Only establishes concrete-owner construction order. All actual I/O below
// uses the UI LoRa port fakes; no driver is instantiated or acquired.
struct Owner final : platform::linux_runtime::IPacketRadio
{
    std::atomic_bool online{true};
    const char* name() const override { return "no hardware"; }
    bool hardwareCandidatePresent() const override { return false; }
    bool acquire() override { return false; }
    void release() override {}
    bool configureLoRa(const platform::linux_runtime::RadioSettings&) override { return false; }
    bool startReceive() override { return false; }
    bool transmit(const uint8_t*, size_t) override { return false; }
    bool pollReceive(platform::linux_runtime::RadioPacket*) override { return false; }
    float readRssi() override { return -127; }
    bool isOnline() const override { return online; }
    const char* lastError() const override { return ""; }
    platform::linux_runtime::RadioSettings appliedLoRaConfig() const override { return {}; }
    platform::linux_runtime::RadioStatistics stats() const override { return {}; }
} owner;
} // namespace

namespace platform::linux_runtime
{
IPacketRadio& foreground_packet_radio() { return owner; }
} // namespace platform::linux_runtime
namespace platform::ui::lora
{
bool acquire()
{
    offUi();
    std::unique_lock<std::mutex> lock(io.mutex);
    io.acquire_entered = true;
    io.wake.wait(lock, []
                 { return !io.block_acquire; });
    return true;
}
void release()
{
    offUi();
    std::unique_lock<std::mutex> lock(io.mutex);
    io.release_entered = true;
    io.wake.wait(lock, []
                 { return !io.block_release; });
    ++io.releases;
}
bool configure_receive(float frequency, const ReceiveConfig&)
{
    offUi();
    std::unique_lock<std::mutex> lock(io.mutex);
    io.config_entered = true;
    io.wake.wait(lock, []
                 { return !io.block_config; });
    io.configurations.push_back(frequency);
    io.marker = static_cast<uint8_t>(frequency);
    io.remaining = io.next_count;
    io.emitted = 0;
    io.empty_polls = 0;
    return !io.fail_config;
}
bool transmit_packet(const uint8_t* bytes, size_t size)
{
    offUi();
    std::unique_lock<std::mutex> lock(io.mutex);
    io.tx_entered = true;
    io.wake.wait(lock, []
                 { return !io.block_tx; });
    io.transmitted.assign(bytes, bytes + size);
    return !io.fail_tx;
}
bool poll_received_packet(uint8_t* bytes, size_t capacity, ReceivedPacket* packet)
{
    offUi();
    std::unique_lock<std::mutex> lock(io.mutex);
    if (!io.remaining && !io.block_poll)
    {
        ++io.empty_polls;
        return false;
    }
    const auto marker = io.marker;
    io.poll_entered = true;
    io.wake.wait(lock, []
                 { return !io.block_poll; });
    if (io.remaining) --io.remaining;
    require(capacity >= 2, "worker provided undersized RX scratch");
    bytes[0] = marker;
    bytes[1] = static_cast<uint8_t>(++io.emitted);
    packet->size = 2;
    return true;
}
} // namespace platform::ui::lora

int main()
{
    struct Unblock
    {
        ~Unblock()
        {
            std::lock_guard<std::mutex> lock(io.mutex);
            io.block_acquire = io.block_poll = io.block_tx = io.block_config = false;
            io.wake.notify_all();
        }
    } unblock;
    require(lora::receive_session_snapshot().state == lora::ReceiveSessionState::Idle, "getter initialized receiver");
    lora::ReceiveConfig config;
    const auto start = std::chrono::steady_clock::now();
    const auto first = lora::request_receive_session(41, config);
    await([]
          { return inspect([]
                           { return io.acquire_entered; }); },
          "acquisition not dispatched");
    require(std::chrono::steady_clock::now() - start < std::chrono::seconds(1), "request waited for blocked hardware");
    require(lora::receive_session_snapshot().state == lora::ReceiveSessionState::Configuring, "accepted request claimed readiness");
    const auto second = lora::request_receive_session(42, config);
    lora::release_receive_session(first); // Obsolete cleanup cannot cancel second.
    {
        std::lock_guard<std::mutex> lock(io.mutex);
        io.block_acquire = false;
        io.wake.notify_all();
    }
    ready(second);
    require(!std::async(std::launch::async, []
                        { return lora::wait_receive_session_released(); })
                 .get(),
            "handoff stole an active receive session");
    {
        std::lock_guard<std::mutex> lock(io.mutex);
        require(io.configurations.back() == 42, "old configuration was published as new");
        io.block_poll = true;
        io.poll_entered = false;
    }
    await([]
          { return inspect([]
                           { return io.poll_entered; }); },
          "RX was not dispatched");
    const auto third = lora::request_receive_session(43, config);
    uint8_t bytes[8]{};
    lora::ReceivedPacket packet;
    require(!lora::poll_receive_session(second, bytes, sizeof(bytes), &packet), "old generation remained readable");
    {
        std::lock_guard<std::mutex> lock(io.mutex);
        io.block_poll = false;
        io.wake.notify_all();
    }
    ready(third);
    await([&]
          { return lora::poll_receive_session(third, bytes, sizeof(bytes), &packet); },
          "new-generation packet missing");
    require(bytes[0] == 43, "in-flight old packet crossed retune");
    require(!lora::transmit_receive_session(second, bytes, 1), "stale generation admitted TX");
    {
        std::lock_guard<std::mutex> lock(io.mutex);
        io.block_tx = true;
    }
    bytes[0] = 1;
    bytes[1] = 2;
    require(lora::transmit_receive_session(third, bytes, 2), "TX request rejected");
    bytes[0] = bytes[1] = 9;
    await([]
          { return inspect([]
                           { return io.tx_entered; }); },
          "TX was not dispatched");
    require(lora::receive_session_snapshot().transmit_pending, "queued TX claimed completion");
    require(!lora::transmit_receive_session(third, bytes, 2), "TX slot overcommitted");
    const auto fourth = lora::request_receive_session(44, config);
    {
        std::lock_guard<std::mutex> lock(io.mutex);
        io.block_tx = false;
        io.wake.notify_all();
    }
    ready(fourth);
    require(inspect([]
                    { return io.transmitted == std::vector<uint8_t>({1, 2}); }),
            "caller mutation changed queued TX bytes");
    {
        std::lock_guard<std::mutex> lock(io.mutex);
        io.fail_config = true;
    }
    const auto failed = lora::request_receive_session(45, config);
    await([&]
          { auto state = lora::receive_session_snapshot(); return state.generation == failed && state.state == lora::ReceiveSessionState::Failed; },
          "configuration failure not published");
    require(!lora::poll_receive_session(failed, bytes, sizeof(bytes), &packet), "failed task admitted RX");
    {
        std::lock_guard<std::mutex> lock(io.mutex);
        io.fail_config = false;
        io.next_count = 40;
    }
    const auto ring = lora::request_receive_session(46, config);
    ready(ring);
    await([]
          { return inspect([]
                           { return !io.remaining && io.empty_polls; }); },
          "RX burst not completed");
    unsigned count = 0;
    while (lora::poll_receive_session(ring, bytes, sizeof(bytes), &packet))
    {
        require(bytes[0] == 46 && bytes[1] == 25 + count, "bounded RX ring did not retain latest packets");
        ++count;
    }
    require(count == 16, "RX ring size was not bounded");
    {
        std::lock_guard<std::mutex> lock(io.mutex);
        io.fail_tx = true;
    }
    require(lora::transmit_receive_session(ring, bytes, 1), "failure-case TX not admitted");
    await([]
          { return lora::receive_session_snapshot().state == lora::ReceiveSessionState::Failed; },
          "TX failure not published");
    {
        std::lock_guard<std::mutex> lock(io.mutex);
        io.fail_tx = false;
    }
    const auto disconnected = lora::request_receive_session(48, config);
    ready(disconnected);
    owner.online = false;
    await([]
          { return lora::receive_session_snapshot().state == lora::ReceiveSessionState::Failed; },
          "runtime radio loss not published");
    require(!lora::poll_receive_session(disconnected, bytes, sizeof(bytes), &packet), "lost radio retained buffered RX");
    require(std::async(std::launch::async, []
                       { return lora::wait_receive_session_released(); })
                .get(),
            "lost-radio cleanup did not finish");
    owner.online = true;
    {
        std::lock_guard<std::mutex> lock(io.mutex);
        io.fail_tx = false;
        io.block_config = true;
        io.config_entered = false;
        io.block_release = true;
        io.release_entered = false;
    }
    const auto cancelled = lora::request_receive_session(47, config);
    await([]
          { return inspect([]
                           { return io.config_entered; }); },
          "blocked configuration not dispatched");
    const auto stop = std::chrono::steady_clock::now();
    lora::release_receive_session(cancelled);
    require(std::chrono::steady_clock::now() - stop < std::chrono::milliseconds(100), "UI release waited for hardware");
    require(lora::receive_session_snapshot().state == lora::ReceiveSessionState::Idle, "cancelled task remained visible");
    auto handoff = std::async(std::launch::async, []
                              { return lora::wait_receive_session_released(); });
    const bool waited_for_config = handoff.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout;
    const auto released = inspect([]
                                  { return io.releases; });
    {
        std::lock_guard<std::mutex> lock(io.mutex);
        io.block_config = false;
        io.wake.notify_all();
    }
    await([]
          { return inspect([]
                           { return io.release_entered; }); },
          "hardware release not dispatched");
    const bool waited_for_restore = handoff.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout;
    {
        std::lock_guard<std::mutex> lock(io.mutex);
        io.block_release = false;
        io.wake.notify_all();
    }
    require(handoff.get() && waited_for_config && waited_for_restore, "Idle state bypassed hardware/configuration restoration");
    await([&]
          { return inspect([&]
                           { return io.releases > released; }); },
          "cancelled acquisition did not release lease");
    for (unsigned n = 0; n < 32; ++n)
    {
        const auto generation = lora::request_receive_session(49, config);
        lora::release_receive_session(generation);
        require(std::async(std::launch::async, []
                           { return lora::wait_receive_session_released(); })
                    .get(),
                "immediate request/release lost its handoff wakeup");
    }
    const auto prior = lora::request_receive_session(50, config);
    ready(prior);
    {
        std::lock_guard<std::mutex> lock(io.mutex);
        io.block_release = true;
        io.release_entered = false;
    }
    lora::release_receive_session(prior);
    await([]
          { return inspect([]
                           { return io.release_entered; }); },
          "prior release did not block");
    {
        std::lock_guard<std::mutex> lock(io.mutex);
        io.block_config = true;
        io.config_entered = false;
    }
    const auto replacement = lora::request_receive_session(51, config);
    {
        std::lock_guard<std::mutex> lock(io.mutex);
        io.block_release = false;
        io.wake.notify_all();
    }
    await([]
          { return inspect([]
                           { return io.config_entered; }); },
          "replacement configuration did not block");
    lora::release_receive_session(replacement);
    auto replacement_handoff = std::async(std::launch::async, []
                                          { return lora::wait_receive_session_released(); });
    const bool waited_for_replacement = replacement_handoff.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout;
    {
        std::lock_guard<std::mutex> lock(io.mutex);
        io.block_config = false;
        io.wake.notify_all();
    }
    require(replacement_handoff.get() && waited_for_replacement, "request during prior release published a premature hardware-idle state");
}
