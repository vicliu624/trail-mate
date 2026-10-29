#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace platform::linux_runtime
{
// Worker-owned ATT framing only. Protocol queues and parsing belong to core_phone.
class PhoneGattSession
{
  public:
    enum class Result
    {
        Ok,
        InvalidOffset,
        InvalidLength,
        Busy,
        Failed
    };
    using Bytes = std::vector<uint8_t>;
    using Reader = std::function<bool(Bytes&)>;
    using Writer = std::function<bool(const Bytes&)>;

    PhoneGattSession(size_t read_limit, size_t write_limit)
        : read_limit_(std::min(read_limit, size_t{512})),
          write_limit_(std::min(write_limit, size_t{512})) {}

    Result read(const std::string& peer, uint16_t mtu, size_t offset,
                const Reader& pull, Bytes& output)
    {
        output.clear();
        if (peer.empty() || mtu < 23) return Result::Failed;
        if (!peer_.empty() && peer_ != peer) return Result::Busy;
        if (offset != 0 && (!cache_valid_ || peer_ != peer)) return Result::InvalidOffset;
        if (offset == 0)
        {
            Bytes next;
            bool accepted = false;
            try
            {
                accepted = pull && pull(next);
            }
            catch (...)
            {
                accepted = false;
            }
            if (!accepted || next.size() > read_limit_) return Result::Failed;
            peer_ = peer;
            cache_ = std::move(next);
            cache_valid_ = true;
        }
        if (offset > cache_.size()) return Result::InvalidOffset;
        const size_t count = std::min(cache_.size() - offset, size_t(mtu - 1));
        output.assign(cache_.begin() + offset, cache_.begin() + offset + count);
        return Result::Ok;
    }

    Result write(const std::string& peer, size_t offset, bool prepared,
                 const Bytes& bytes, const Writer& push)
    {
        if (peer.empty()) return Result::Failed;
        if (!peer_.empty() && peer_ != peer) return Result::Busy;
        if (offset != 0 || prepared) return Result::InvalidOffset;
        if (bytes.empty() || bytes.size() > write_limit_) return Result::InvalidLength;
        // Claim before dispatch: a callback may inspect the owning peer.
        const bool newly_claimed = peer_.empty();
        peer_ = peer;
        bool accepted = false;
        try
        {
            accepted = push && push(bytes);
        }
        catch (...)
        {
            accepted = false;
        }
        if (!accepted && newly_claimed) peer_.clear();
        return accepted ? Result::Ok : Result::Failed;
    }

    bool canNotify(size_t length, uint16_t mtu) const
    {
        return !peer_.empty() && mtu >= 23 && length <= read_limit_ && length <= size_t(mtu - 3);
    }

    bool disconnect(const std::string& peer)
    {
        if (peer_.empty() || peer_ != peer) return false;
        peer_.clear();
        cache_.clear();
        cache_valid_ = false;
        return true;
    }

  private:
    const size_t read_limit_;
    const size_t write_limit_;
    std::string peer_;
    Bytes cache_;
    bool cache_valid_ = false;
};
} // namespace platform::linux_runtime
