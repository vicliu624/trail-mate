#pragma once
#include "platform/esp/arduino_common/geocaching/sd_indexed_saved_cache.h"
#include "platform/memory/psram_ptr.h"
#include <memory>
#include <new>

namespace platform::esp::arduino_common::geocaching
{
// One head-index pass, with metadata reads only for the requested window.
// The caller pins the root and workspace, and owns the four output rows.
class SdIndexedSavedPage
{
  public:
    SdIndexedSavedPage(const ::geocaching::storage::VolumeInstance& volume, ::geocaching::protocol::RecordCrypto& crypto)
        : volume_(volume), crypto_(crypto), heads_(volume, &read_session_) {}
    bool begin(const ::geocaching::storage::IndexRootView& root, size_t offset, size_t limit,
               std::array<::geocaching::storage::SavedCacheEntry, 4>& rows, size_t& total,
               uint8_t* frame, size_t capacity, uint8_t* verification, size_t verification_capacity)
    {
        if (!limit || limit > rows.size() || !heads_.begin(root, 2, frame, capacity)) return false;
        root_ = root;
        offset_ = offset;
        limit_ = limit;
        rows_ = &rows;
        total_ = &total;
        total = 0;
        frame_ = frame;
        capacity_ = capacity;
        verification_ = verification;
        verification_capacity_ = verification_capacity;
        return true;
    }
    bool matches(size_t offset, size_t limit, const std::array<::geocaching::storage::SavedCacheEntry, 4>& rows, const size_t& total) const
    {
        return offset == offset_ && limit == limit_ && &rows == rows_ && &total == total_;
    }
    bool unavailable() const { return unavailable_; }
    IndexScanStep step()
    {
        using namespace ::geocaching;
        using namespace ::geocaching::storage;
        if (entry_)
        {
            const auto status = entry_->step();
            if (status == IndexScanStep::Working) return status;
            ByteView signed_cache;
            if (status != IndexScanStep::Item) return status == IndexScanStep::End ? IndexScanStep::Invalid : status;
            if (!entry_->result(record_, signed_cache)) return IndexScanStep::Invalid;
            // Old cards can still derive metadata from the retained signed
            // response. New objects already carry its verified projection.
            if (signed_cache.size)
            {
                const auto result = verifySavedCache(signed_cache, crypto_, verification_, verification_capacity_, record_);
                if (result != protocol::VerificationResult::Valid)
                {
                    unavailable_ = result == protocol::VerificationResult::CryptoUnavailable;
                    return result == protocol::VerificationResult::WorkspaceTooSmall ? IndexScanStep::WorkspaceTooSmall : IndexScanStep::Invalid;
                }
            }
            (*rows_)[count_++] = record_;
            entry_.reset();
            return heads_.advance() ? IndexScanStep::Working : IndexScanStep::Invalid;
        }
        const auto status = heads_.step();
        if (status != IndexScanStep::Item) return status;
        MutationView row;
        CacheHeadView head;
        if (!heads_.item(row) || !decodeCacheHead(row.key, row.value, head)) return IndexScanStep::Invalid;
        if (!head.current_hash.size) return heads_.advance() ? IndexScanStep::Working : IndexScanStep::Invalid;
        const auto ordinal = (*total_)++;
        if (ordinal < offset_) return heads_.advance() ? IndexScanStep::Working : IndexScanStep::Invalid;
        if (count_ == limit_) return IndexScanStep::End;
        entry_.reset(::platform::memory::createPsram<SdIndexedSavedCache>(volume_, &read_session_));
        if (!entry_)
        {
            unavailable_ = true;
            return IndexScanStep::Invalid;
        }
        if (!entry_->beginFromHead(root_, row.key, row.value, frame_, capacity_)) return IndexScanStep::Invalid;
        return IndexScanStep::Working;
    }

  private:
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::protocol::RecordCrypto& crypto_;
    ::geocaching::storage::IndexRootView root_;
    SdVolumeReadSession read_session_;
    SdIndexScan heads_;
    ::platform::memory::PsramPtr<SdIndexedSavedCache> entry_;
    ::geocaching::storage::SavedCacheRecord record_;
    std::array<::geocaching::storage::SavedCacheEntry, 4>* rows_ = nullptr;
    size_t* total_ = nullptr;
    uint8_t *frame_ = nullptr, *verification_ = nullptr;
    size_t offset_ = 0, limit_ = 0, count_ = 0, capacity_ = 0, verification_capacity_ = 0;
    bool unavailable_ = false;
};
} // namespace platform::esp::arduino_common::geocaching
