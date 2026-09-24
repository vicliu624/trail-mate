#pragma once
#include "geocaching/storage/publication_history.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_draft_catalog.h"

namespace platform::esp::arduino_common::geocaching
{
// Active-only operation, sharing one frame across draft, request/task and
// issuance reads. Keep the root and output object pinned until terminal.
class SdIndexedPublicationHistory
{
  public:
    SdIndexedPublicationHistory(const ::geocaching::storage::VolumeInstance& volume, ::geocaching::protocol::RecordCrypto& crypto)
        : volume_(volume), crypto_(crypto) {}
    bool begin(const ::geocaching::storage::IndexRootView& root, ::geocaching::ByteView key, uint64_t generation,
               const ::geocaching::GeocacheId& cache, ::geocaching::ByteView author,
               ::geocaching::storage::PublicationHistory& out, uint8_t* frame, size_t capacity)
    {
        if (out_ || !key.data || key.size != 16 || !author.data || author.size != 64) return false;
        std::memcpy(key_.data(), key.data, 16);
        std::memcpy(author_.data(), author.data, 64);
        generation_ = generation;
        cache_ = cache;
        root_ = root;
        frame_ = frame;
        capacity_ = capacity;
        draft_.reset(new (std::nothrow) SdIndexGet(volume_));
        if (!draft_)
        {
            unavailable_ = true;
            return false;
        }
        if (!draft_->begin(root, 4, {key_.data(), key_.size()}, frame, capacity)) return false;
        out_ = &out;
        return true;
    }
    bool unavailable() const { return unavailable_; }
    bool matches(::geocaching::ByteView key, uint64_t generation, const ::geocaching::GeocacheId& cache,
                 ::geocaching::ByteView author, const ::geocaching::storage::PublicationHistory& out) const
    {
        return out_ == &out && generation_ == generation && cache_.bytes == cache.bytes && key.data && key.size == key_.size() &&
               author.data && author.size == author_.size() && !std::memcmp(key.data, key_.data(), key.size) &&
               !std::memcmp(author.data, author_.data(), author.size);
    }
    IndexGetStep step()
    {
        using namespace ::geocaching;
        using namespace ::geocaching::storage;
        if (!out_) return IndexGetStep::Invalid;
        if (draft_)
        {
            const auto status = draft_->step();
            if (status != IndexGetStep::Ready) return status;
            DraftView draft;
            if (!decodeDraft({key_.data(), key_.size()}, draft_->value(), draft)) return IndexGetStep::Invalid;
            // A stale edit is a business conflict, not damaged storage.
            if (draft.generation != generation_ || draft.author.size != author_.size() ||
                std::memcmp(draft.author.data, author_.data(), author_.size())) return IndexGetStep::NotFound;
            entry_.reset(new (std::nothrow) DraftCatalogEntry);
            if (!entry_ || !describeDraft({key_.data(), key_.size()}, draft, crypto_, *entry_))
            {
                unavailable_ = true;
                return IndexGetStep::Invalid;
            }
            draft_.reset();
            catalog_.reset(new (std::nothrow) SdIndexedDraftCatalog(volume_, crypto_));
            if (!catalog_)
            {
                unavailable_ = true;
                return IndexGetStep::Invalid;
            }
            if (!catalog_->beginPublication(root_, *entry_, frame_, capacity_))
            {
                unavailable_ = catalog_->unavailable();
                return IndexGetStep::Invalid;
            }
            return IndexGetStep::Working;
        }
        if (catalog_)
        {
            const auto status = catalog_->step();
            unavailable_ = catalog_->unavailable();
            if (status != IndexGetStep::Ready) return status;
            confirmed_ = entry_->publication.confirmed_revision;
            catalog_.reset();
            entry_.reset();
            scan_.reset(new (std::nothrow) SdIndexScan(volume_));
            if (!scan_)
            {
                unavailable_ = true;
                return IndexGetStep::Invalid;
            }
            if (!scan_->begin(root_, 3, frame_, capacity_)) return IndexGetStep::Invalid;
            return IndexGetStep::Working;
        }
        if (!scan_) return IndexGetStep::Invalid;
        const auto status = scan_->step();
        if (status == IndexScanStep::Working) return IndexGetStep::Working;
        if (status == IndexScanStep::End) return history_.finish(confirmed_, *out_) ? IndexGetStep::Ready : IndexGetStep::Invalid;
        if (status != IndexScanStep::Item)
            return status == IndexScanStep::IoError ? IndexGetStep::IoError : status == IndexScanStep::VolumeChanged   ? IndexGetStep::VolumeChanged
                                                                          : status == IndexScanStep::WorkspaceTooSmall ? IndexGetStep::WorkspaceTooSmall
                                                                                                                       : IndexGetStep::Invalid;
        MutationView row;
        if (!scan_->item(row) || !history_.consume(row.key, row.value, cache_, {author_.data(), author_.size()}) || !scan_->advance()) return IndexGetStep::Invalid;
        return IndexGetStep::Working;
    }

  private:
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::protocol::RecordCrypto& crypto_;
    ::geocaching::storage::IndexRootView root_;
    ::geocaching::storage::PublicationHistory* out_ = nullptr;
    ::geocaching::storage::PublicationHistoryScan history_;
    std::unique_ptr<SdIndexGet> draft_;
    std::unique_ptr<::geocaching::storage::DraftCatalogEntry> entry_;
    std::unique_ptr<SdIndexedDraftCatalog> catalog_;
    std::unique_ptr<SdIndexScan> scan_;
    std::array<uint8_t, 16> key_{};
    std::array<uint8_t, 64> author_{};
    ::geocaching::GeocacheId cache_;
    uint8_t* frame_ = nullptr;
    size_t capacity_ = 0;
    uint64_t generation_ = 0;
    uint32_t confirmed_ = 0;
    bool unavailable_ = false;
};
static_assert(sizeof(SdIndexedPublicationHistory) <= (sizeof(void*) == 4 ? 384 : 448), "History read retains metadata, never record payloads");
} // namespace platform::esp::arduino_common::geocaching
