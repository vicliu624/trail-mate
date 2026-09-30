#pragma once
#include "geocaching/storage/cache_head.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_commit.h"

namespace platform::esp::arduino_common::geocaching
{
// Removes only the selected offline revision. Retaining and advancing the
// installation generation prevents old download tasks from reinstalling it.
// The owner leases the pinned root/frame and allocates this operation in PSRAM.
class SdIndexedRemoveSaved
{
  public:
    explicit SdIndexedRemoveSaved(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    bool begin(const ::geocaching::storage::IndexRootView& root, unsigned copy,
               const std::array<uint8_t, 32>& cache, const std::array<uint8_t, 32>& hash,
               uint8_t* frame, size_t capacity, ::geocaching::storage::IndexRootBytes& candidate)
    {
        if (result_ != IndexedCommitStep::Idle || copy > 1) return false;
        root_ = root;
        copy_ = copy;
        cache_ = cache;
        hash_ = hash;
        frame_ = frame;
        capacity_ = capacity;
        candidate_ = &candidate;
        if (!io_.emplace<SdIndexGet>(volume_).begin(root_, 2, {cache_.data(), cache_.size()}, frame, capacity)) return false;
        result_ = IndexedCommitStep::Working;
        return true;
    }
    IndexedCommitStep step()
    {
        using namespace ::geocaching::storage;
        if (result_ != IndexedCommitStep::Working) return result_;
        if (committing_) return result_ = std::get<SdIndexedCommit>(io_).step();
        auto& read = std::get<SdIndexGet>(io_);
        const auto status = read.step();
        if (status == IndexGetStep::Working) return result_;
        if (status != IndexGetStep::Ready)
            return result_ = status == IndexGetStep::IoError ? IndexedCommitStep::IoError : status == IndexGetStep::VolumeChanged ? IndexedCommitStep::VolumeChanged
                                                                                                                                  : IndexedCommitStep::Invalid;
        CacheHeadView head;
        const ::geocaching::ByteView key{cache_.data(), cache_.size()};
        if (!decodeCacheHead(key, read.value(), head) || head.current_hash.size != hash_.size() ||
            std::memcmp(head.current_hash.data, hash_.data(), hash_.size()) || head.install_generation == UINT64_MAX)
            return result_ = IndexedCommitStep::Invalid;
        head.current_hash = {};
        head.conflict_state = 0;
        ++head.install_generation;
        size_t size = 0;
        if (!encodeCacheHead(key, head, value_.data(), value_.size(), size)) return result_ = IndexedCommitStep::Invalid;
        mutation_ = {2, key, {value_.data(), size}, false};
        if (!io_.emplace<SdIndexedCommit>(volume_).begin(root_, copy_, &mutation_, 1, frame_, capacity_, *candidate_))
            return result_ = IndexedCommitStep::Invalid;
        committing_ = true;
        return result_;
    }
    bool committed(::geocaching::storage::IndexRootView& out) const
    {
        out = {};
        return result_ == IndexedCommitStep::Verified && committing_ && std::get<SdIndexedCommit>(io_).committed(out);
    }

  private:
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootView root_;
    std::array<uint8_t, 32> cache_{}, hash_{};
    std::array<uint8_t, 64> value_{};
    ::geocaching::storage::MutationView mutation_;
    uint8_t* frame_ = nullptr;
    size_t capacity_ = 0;
    unsigned copy_ = 0;
    ::geocaching::storage::IndexRootBytes* candidate_ = nullptr;
    std::variant<std::monostate, SdIndexGet, SdIndexedCommit> io_;
    bool committing_ = false;
    IndexedCommitStep result_ = IndexedCommitStep::Idle;
};
static_assert(sizeof(SdIndexedRemoveSaved) <= 1536, "Offline removal owns metadata only, in PSRAM");
} // namespace platform::esp::arduino_common::geocaching
