#include "platform/ui/lora_receive_session.h"

#include "platform/linux/packet_radio.h"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

namespace platform::ui::lora
{
namespace
{
struct BufferedPacket
{
    std::array<uint8_t, 255> bytes{};
    ReceivedPacket metadata{};
};
class ReceiveWorker;
std::atomic<ReceiveWorker*> instance{nullptr};

class ReceiveWorker
{
  public:
    ReceiveWorker() : worker_([this]
                              { loop(); }) { instance = this; }
    ~ReceiveWorker()
    {
        instance = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_one();
        worker_.join();
    }
    uint32_t request(float frequency, const ReceiveConfig& config)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (++state_.generation == 0) ++state_.generation;
        state_.state = ReceiveSessionState::Configuring;
        state_.transmit_pending = false;
        active_ = true;
        frequency_ = frequency;
        config_ = config;
        head_ = count_ = 0;
        wake_.notify_one();
        return state_.generation;
    }
    ReceiveSessionSnapshot snapshot()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }
    void release(uint32_t generation)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!generation || generation != state_.generation) return;
        active_ = false;
        state_.state = ReceiveSessionState::Idle;
        state_.transmit_pending = false;
        head_ = count_ = 0;
        wake_.notify_one();
    }
    bool poll(uint32_t generation, uint8_t* buffer, size_t capacity, ReceivedPacket* packet)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!generation || generation != state_.generation || state_.state != ReceiveSessionState::Ready ||
            !buffer || !packet || !count_) return false;
        const auto& slot = received_[head_];
        if (slot.metadata.size > capacity) return false;
        std::memcpy(buffer, slot.bytes.data(), slot.metadata.size);
        *packet = slot.metadata;
        head_ = (head_ + 1) % received_.size();
        --count_;
        return true;
    }
    bool transmit(uint32_t generation, const uint8_t* bytes, size_t size)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!generation || generation != state_.generation || state_.state != ReceiveSessionState::Ready ||
            state_.transmit_pending || !bytes || !size || size > tx_.size()) return false;
        std::memcpy(tx_.data(), bytes, size);
        tx_size_ = size;
        state_.transmit_pending = true;
        wake_.notify_one();
        return true;
    }

  private:
    void loop()
    {
        bool acquired = false;
        uint32_t configured = 0;
        for (;;)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (stopping_) break;
            if (!active_)
            {
                lock.unlock();
                if (acquired) lora::release();
                acquired = false;
                configured = 0;
                lock.lock();
                wake_.wait(lock, [this]
                           { return stopping_ || active_; });
                continue;
            }
            const auto generation = state_.generation;
            if (configured != generation)
            {
                const auto frequency = frequency_;
                const auto config = config_;
                lock.unlock();
                if (!acquired) acquired = lora::acquire();
                const bool ready = acquired && lora::configure_receive(frequency, config);
                lock.lock();
                if (active_ && state_.generation == generation)
                {
                    state_.state = ready ? ReceiveSessionState::Ready : ReceiveSessionState::Failed;
                    if (!ready) active_ = false;
                    configured = ready ? generation : 0;
                }
                continue;
            }
            if (state_.transmit_pending)
            {
                // Only the worker writes this scratch slot. The UI may replace
                // the queued TX buffer after a retune while I/O is in flight.
                tx_scratch_ = tx_;
                const auto size = tx_size_;
                lock.unlock();
                const bool sent = lora::transmit_packet(tx_scratch_.data(), size);
                lock.lock();
                if (active_ && state_.generation == generation)
                {
                    state_.transmit_pending = false;
                    if (!sent)
                    {
                        state_.state = ReceiveSessionState::Failed;
                        active_ = false;
                        head_ = count_ = 0;
                    }
                }
                continue;
            }
            lock.unlock();
            const bool received = lora::poll_received_packet(rx_scratch_.bytes.data(), rx_scratch_.bytes.size(), &rx_scratch_.metadata);
            const bool online = ::platform::linux_runtime::foreground_packet_radio().isOnline();
            lock.lock();
            if (active_ && state_.generation == generation && !online)
            {
                state_.state = ReceiveSessionState::Failed;
                active_ = false;
                head_ = count_ = 0;
                continue;
            }
            if (received && active_ && state_.generation == generation && rx_scratch_.metadata.size &&
                rx_scratch_.metadata.size <= rx_scratch_.bytes.size())
            {
                if (count_ == received_.size())
                {
                    head_ = (head_ + 1) % received_.size(); // Drop oldest on full.
                    --count_;
                }
                received_[(head_ + count_) % received_.size()] = rx_scratch_;
                ++count_;
            }
            wake_.wait_for(lock, std::chrono::milliseconds(5), [this, generation]
                           { return stopping_ || !active_ || state_.generation != generation || state_.transmit_pending; });
        }
        if (acquired) lora::release();
    }
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_ = false, active_ = false;
    ReceiveSessionSnapshot state_;
    float frequency_ = 0;
    ReceiveConfig config_;
    std::array<BufferedPacket, 16> received_{};
    size_t head_ = 0, count_ = 0, tx_size_ = 0;
    std::array<uint8_t, 255> tx_{}, tx_scratch_{};
    BufferedPacket rx_scratch_;
    std::thread worker_;
};

ReceiveWorker& worker()
{
    // Construct the concrete radio owner first so static destruction joins this
    // worker before the coordinator/driver is destroyed. No lease is acquired.
    (void)::platform::linux_runtime::foreground_packet_radio();
    static ReceiveWorker value;
    return value;
}
} // namespace

uint32_t request_receive_session(float frequency, const ReceiveConfig& config) { return worker().request(frequency, config); }
ReceiveSessionSnapshot receive_session_snapshot()
{
    auto* value = instance.load();
    return value ? value->snapshot() : ReceiveSessionSnapshot{};
}
bool poll_receive_session(uint32_t generation, uint8_t* buffer, size_t capacity, ReceivedPacket* packet)
{
    auto* value = instance.load();
    return value && value->poll(generation, buffer, capacity, packet);
}
bool transmit_receive_session(uint32_t generation, const uint8_t* bytes, size_t size)
{
    auto* value = instance.load();
    return value && value->transmit(generation, bytes, size);
}
void release_receive_session(uint32_t generation)
{
    if (auto* value = instance.load()) value->release(generation);
}
} // namespace platform::ui::lora
