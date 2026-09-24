#pragma once
#include "geocaching/storage/draft_catalog.h"
#include "platform/esp/arduino_common/geocaching/sd_index_get.h"
#include "platform/esp/arduino_common/geocaching/sd_index_scan.h"
#include <memory>
#include <new>

namespace platform::esp::arduino_common::geocaching
{
// Holds a root/frame lease through both table scans and parent-task checks.
// Output belongs to the caller and is not visible until Ready. Stored requests
// were verified on admission/recovery; this is a read-only status projection.
class SdIndexedDraftCatalog
{
  public:
    SdIndexedDraftCatalog(const ::geocaching::storage::VolumeInstance& volume, ::geocaching::protocol::RecordCrypto& crypto)
        : volume_(volume), crypto_(crypto) {}
    bool begin(const ::geocaching::storage::IndexRootView& root, size_t offset, ::geocaching::storage::DraftCatalogPage& page,
               uint8_t* frame, size_t capacity)
    {
        if (scan_ || page_) return false;
        scan_.reset(new (std::nothrow) SdIndexScan(volume_));
        if (!scan_)
        {
            unavailable_ = true;
            return false;
        }
        if (!scan_->begin(root, 4, frame, capacity)) return false;
        page.count = page.total = 0;
        page.offset = offset;
        page_ = &page;
        entries_ = page.rows.data();
        root_ = root;
        frame_ = frame;
        capacity_ = capacity;
        return true;
    }
    // Publication preparation already read and validated its one draft. Reuse
    // the same request/task projection without scanning the draft catalog.
    bool beginPublication(const ::geocaching::storage::IndexRootView& root, ::geocaching::storage::DraftCatalogEntry& entry,
                          uint8_t* frame, size_t capacity)
    {
        if (scan_ || entries_ || !entry.has_author) return false;
        scan_.reset(new (std::nothrow) SdIndexScan(volume_));
        if (!scan_)
        {
            unavailable_ = true;
            return false;
        }
        if (!scan_->begin(root, 5, frame, capacity)) return false;
        entry.publication = {};
        entries_ = &entry;
        count_ = 1;
        requests_ = true;
        root_ = root;
        frame_ = frame;
        capacity_ = capacity;
        return true;
    }
    bool unavailable() const { return unavailable_; }
    IndexGetStep step()
    {
        using namespace ::geocaching;
        using namespace ::geocaching::storage;
        if (!entries_ || !scan_) return IndexGetStep::Invalid;
        if (task_)
        {
            const auto status = task_->step();
            if (status == IndexGetStep::Working) return status;
            if (status != IndexGetStep::Ready) return status == IndexGetStep::NotFound ? IndexGetStep::Invalid : status;
            TaskView parent;
            if (!decodeTask({task_key_.data(), task_key_.size()}, task_->value(), parent)) return IndexGetStep::Invalid;
            OutgoingView outgoing;
            outgoing.task_id = {task_key_.data(), task_key_.size()};
            if (parent.kind == 1 && parent.cache_id.size == 32 && parent.revision_hash.size == 32 &&
                requestBelongsToTask(outgoing.task_id, parent, {request_key_.data(), request_key_.size()}, outgoing))
            {
                auto& entry = entries_[matched_];
                auto& projection = entry.publication;
                if (entry.has_base && !std::memcmp(entry.base_hash.data(), parent.revision_hash.data, 32)) projection.base_retained = true;
                if (revision_ > projection.latest_revision)
                {
                    projection.latest_revision = revision_;
                    projection.local_changes = changed_;
                    projection.pending = projection.stopped = false;
                }
                if (state_ == 4)
                {
                    if (acknowledged_ && !std::memcmp(ack_id_.bytes.data(), parent.cache_id.data, 32) &&
                        !std::memcmp(ack_hash_.bytes.data(), parent.revision_hash.data, 32) && revision_ > projection.confirmed_revision)
                        projection.confirmed_revision = revision_;
                }
                else if (revision_ == projection.latest_revision)
                {
                    const bool active = intent_ && parent.continue_intent && parent.state != 5;
                    projection.pending |= active;
                    projection.stopped |= !active;
                }
            }
            task_.reset();
            return IndexGetStep::Working;
        }
        const auto status = scan_->step();
        if (status == IndexScanStep::Working) return IndexGetStep::Working;
        if (status == IndexScanStep::End)
        {
            if (requests_ || !page_->count) return IndexGetStep::Ready;
            scan_.reset(new (std::nothrow) SdIndexScan(volume_));
            if (!scan_)
            {
                unavailable_ = true;
                return IndexGetStep::Invalid;
            }
            if (!scan_->begin(root_, 5, frame_, capacity_)) return IndexGetStep::Invalid;
            requests_ = true;
            count_ = page_->count;
            return IndexGetStep::Working;
        }
        if (status != IndexScanStep::Item)
            return status == IndexScanStep::IoError ? IndexGetStep::IoError : status == IndexScanStep::VolumeChanged   ? IndexGetStep::VolumeChanged
                                                                          : status == IndexScanStep::WorkspaceTooSmall ? IndexGetStep::WorkspaceTooSmall
                                                                                                                       : IndexGetStep::Invalid;
        MutationView row;
        if (!scan_->item(row)) return IndexGetStep::Invalid;
        if (!requests_)
        {
            const auto ordinal = page_->total++;
            if (ordinal >= page_->offset && page_->count < page_->rows.size())
            {
                DraftView draft;
                if (!decodeDraft(row.key, row.value, draft)) return IndexGetStep::Invalid;
                if (!describeDraft(row.key, draft, crypto_, page_->rows[page_->count]))
                {
                    unavailable_ = true;
                    return IndexGetStep::Invalid;
                }
                ++page_->count;
            }
        }
        else
        {
            OutgoingView outgoing;
            if (!decodeOutgoing(row.key, row.value, outgoing)) return IndexGetStep::Invalid;
            RequestId request;
            std::memcpy(request.bytes.data(), row.key.data + 32, 16);
            protocol::PublishRequestView publish;
            if (protocol::decodePublishRequest(outgoing.request, request, publish))
            {
                protocol::CmpReader reader(publish.signed_cache);
                size_t fields = 0;
                ByteView encoded, signature;
                RecordView record;
                if (!reader.array(fields, 2) || fields != 2 || !reader.binary(encoded, kMaxRecordBytes) ||
                    !reader.binary(signature, 64) || !reader.finished() || !protocol::decodeGeocacheRecord(encoded, record)) return IndexGetStep::Invalid;
                for (size_t i = 0; i < count_; ++i)
                {
                    const auto& entry = entries_[i];
                    if (!entry.has_author || std::memcmp(record.author_public_key.data, entry.author.data(), 64) ||
                        std::memcmp(record.creation_nonce.data, entry.id.data(), 16)) continue;
                    bool equal = false;
                    if (!draftContentMatches(entry, record, crypto_, equal))
                    {
                        unavailable_ = true;
                        return IndexGetStep::Invalid;
                    }
                    matched_ = i;
                    revision_ = record.revision;
                    changed_ = !equal;
                    state_ = outgoing.state;
                    intent_ = outgoing.continue_intent;
                    acknowledged_ = false;
                    if (state_ == 4)
                    {
                        // Decode the response identity before reusing the frame
                        // for its task, then compare it with that task below.
                        protocol::CmpReader ack(outgoing.terminal_data);
                        uint64_t value = 0;
                        ByteView bytes;
                        if (ack.array(fields, 6) && ack.unsignedInteger(value) && ack.unsignedInteger(value) && ack.unsignedInteger(value) &&
                            ack.binary(bytes, 16) && ack.unsignedInteger(value) && ack.array(fields, 7) && ack.binary(bytes, 32) && bytes.size == 32)
                        {
                            std::memcpy(ack_id_.bytes.data(), bytes.data, 32);
                            if (ack.unsignedInteger(value) && ack.binary(bytes, 32) && bytes.size == 32)
                            {
                                std::memcpy(ack_hash_.bytes.data(), bytes.data, 32);
                                protocol::PublishDisposition disposition;
                                acknowledged_ = protocol::decodePublishResponse(outgoing.terminal_data, request, ack_id_, ack_hash_, record.revision, record.state, disposition);
                            }
                        }
                    }
                    std::memcpy(request_key_.data(), row.key.data, 48);
                    std::memcpy(task_key_.data(), outgoing.task_id.data, 16);
                    task_.reset(new (std::nothrow) SdIndexGet(volume_));
                    if (!task_)
                    {
                        unavailable_ = true;
                        return IndexGetStep::Invalid;
                    }
                    if (!task_->begin(root_, 10, {task_key_.data(), task_key_.size()}, frame_, capacity_)) return IndexGetStep::Invalid;
                    break;
                }
            }
        }
        return scan_->advance() ? IndexGetStep::Working : IndexGetStep::Invalid;
    }

  private:
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::protocol::RecordCrypto& crypto_;
    ::geocaching::storage::IndexRootView root_;
    ::geocaching::storage::DraftCatalogPage* page_ = nullptr;
    ::geocaching::storage::DraftCatalogEntry* entries_ = nullptr;
    std::unique_ptr<SdIndexScan> scan_;
    std::unique_ptr<SdIndexGet> task_;
    uint8_t* frame_ = nullptr;
    size_t capacity_ = 0, matched_ = 0, count_ = 0;
    std::array<uint8_t, 48> request_key_{};
    std::array<uint8_t, 16> task_key_{};
    ::geocaching::GeocacheId ack_id_;
    ::geocaching::RevisionHash ack_hash_;
    uint32_t revision_ = 0;
    uint8_t state_ = 0;
    bool requests_ = false, changed_ = false, intent_ = false, acknowledged_ = false;
    bool unavailable_ = false;
};
static_assert(sizeof(SdIndexedDraftCatalog) <= (sizeof(void*) == 4 ? 256 : 320), "Catalog scan owns metadata only");
} // namespace platform::esp::arduino_common::geocaching
