#pragma once
#include "geocaching/domain/record.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include <array>
#include <algorithm>

namespace platform::esp::arduino_common::geocaching
{
enum class GpxHashStep : uint8_t { Reading, Complete, InvalidSize, IoError };

// One-shot worker operation with a fresh platform Sha256Digest. Path must be
// locally derived by the store, never taken from a remote name or request.
// Computes exact file bytes only; it does not parse GPX or verify signatures.
template<class Digest>
class SdGpxHash
{
  public:
    explicit SdGpxHash(Digest& digest) : digest_(digest) {}
    bool open(const char* path)
    {
        if (started_ || !path) return false;
        started_ = true;
        if (!file_.open(path, "r")) { state_ = GpxHashStep::IoError; return false; }
        length_ = file_.size();
        if (!length_ || length_ > ::geocaching::kMaxGpxBytes)
        { file_.close(); state_ = GpxHashStep::InvalidSize; return false; }
        return true;
    }
    GpxHashStep step()
    {
        if (state_ != GpxHashStep::Reading) return state_;
        if (!file_.is_open()) return state_ = GpxHashStep::IoError;
        if (offset_ == length_)
        {
            // A changed length invalidates this observation. The owning install
            // operation must still recheck volume/generation before mutation.
            const bool unchanged = file_.size() == length_;
            file_.close();
            if (!unchanged || !digest_.finalize(hash_.data(), hash_.size())) return state_ = GpxHashStep::IoError;
            return state_ = GpxHashStep::Complete;
        }
        const auto requested = static_cast<size_t>(std::min<uint64_t>(buffer_.size(), length_ - offset_));
        const int count = file_.read(buffer_.data(), requested);
        if (count <= 0 || static_cast<size_t>(count) > requested)
        { file_.close(); return state_ = GpxHashStep::IoError; }
        digest_.update(buffer_.data(), static_cast<size_t>(count));
        offset_ += static_cast<size_t>(count);
        return state_;
    }
    bool result(std::array<uint8_t, 32>& out) const
    {
        out = {};
        if (state_ != GpxHashStep::Complete) return false;
        out = hash_; return true;
    }

  private:
    Digest& digest_;
    storage::SdRuntimeFile file_;
    std::array<uint8_t, 512> buffer_{};
    std::array<uint8_t, 32> hash_{};
    uint64_t length_ = 0, offset_ = 0;
    bool started_ = false;
    GpxHashStep state_ = GpxHashStep::Reading;
};
} // namespace platform::esp::arduino_common::geocaching
