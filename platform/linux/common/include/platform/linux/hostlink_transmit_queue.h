#pragma once
#include "hostlink/hostlink_codec.h"
#include <array>
#include <cstddef>

namespace platform::linux_runtime
{
// One transport worker owns the queue. No per-frame allocation; partial writes
// retain their exact offset until completion, backpressure, or disconnection.
class HostlinkTransmitQueue
{
  public:
    enum class WriteState
    {
        Progress,
        Retry,
        WouldBlock,
        Failed
    };
    struct WriteResult
    {
        WriteState state;
        size_t bytes = 0;
    };
    struct FlushResult
    {
        bool connected = true;
        size_t completed = 0;
    };
    bool empty() const { return count_ == 0; }
    ~HostlinkTransmitQueue() { reset(); }
    void reset()
    {
        for (auto& slot : slots_) clear(slot);
        head_ = count_ = offset_ = 0;
    }
    bool push(uint8_t type, uint16_t sequence, const uint8_t* payload, size_t bytes)
    {
        if (count_ == slots_.size()) return false;
        auto& slot = slots_[(head_ + count_) % slots_.size()];
        if (!::hostlink::encode_frame_into(type, sequence, payload, bytes, slot.bytes.data(), slot.bytes.size(), slot.size))
        {
            clear(slot);
            return false;
        }
        ++count_;
        return true;
    }
    template <class Writer>
    FlushResult flush(Writer&& writer)
    {
        FlushResult result;
        while (count_)
        {
            auto& slot = slots_[head_];
            const size_t remaining = slot.size - offset_;
            const auto written = writer(slot.bytes.data() + offset_, remaining);
            if (written.state == WriteState::WouldBlock) return result;
            if (written.state == WriteState::Retry) continue;
            if (written.state != WriteState::Progress || !written.bytes || written.bytes > remaining)
            {
                result.connected = false;
                return result;
            }
            offset_ += written.bytes;
            if (offset_ == slot.size)
            {
                clear(slot);
                head_ = (head_ + 1) % slots_.size();
                --count_;
                offset_ = 0;
                ++result.completed;
            }
        }
        return result;
    }

  private:
    struct Slot
    {
        std::array<uint8_t, ::hostlink::kHeaderSize + ::hostlink::kMaxFrameLen + ::hostlink::kCrcSize> bytes{};
        size_t size = 0;
    };
    static void clear(Slot& slot)
    {
        volatile uint8_t* bytes = slot.bytes.data();
        for (size_t i = 0; i < slot.bytes.size(); ++i) bytes[i] = 0;
        slot.size = 0;
    }
    std::array<Slot, 8> slots_{};
    size_t head_ = 0, count_ = 0, offset_ = 0;
};
} // namespace platform::linux_runtime
