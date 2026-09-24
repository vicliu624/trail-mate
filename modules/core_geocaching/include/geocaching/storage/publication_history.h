#pragma once
#include "geocaching/storage/author_issued.h"
#include "geocaching/storage/draft_publication.h"

namespace geocaching::storage
{
// Owned metadata for reconstructing a retry or a successor. No borrowed SD
// bytes escape the history read, and the complete issuance ledger stays on disk.
struct PublicationHistory
{
    RevisionHash latest_hash, previous_hash;
    StoredTime latest_time;
    uint64_t created_at = 0;
    uint32_t latest_revision = 0, confirmed_revision = 0;
};

class PublicationHistoryScan
{
  public:
    bool consume(ByteView key, ByteView value, const GeocacheId& cache, ByteView author)
    {
        if (!key.data || key.size != 36 || !author.data || author.size != 64) return false;
        if (std::memcmp(key.data, cache.bytes.data(), 32)) return true;
        AuthorIssuedView issued;
        if (!decodeAuthorIssued(key, value, issued) || !issued.issued_at.has_utc ||
            std::memcmp(issued.author_public_key.data, author.data, 64) || count_ == UINT32_MAX) return false;
        ++count_;
        if (issued.revision == 1)
        {
            if (first_) return false;
            first_ = true;
            history_.created_at = issued.issued_at.utc_seconds;
        }
        if (issued.revision > history_.latest_revision)
        {
            previous_revision_ = history_.latest_revision;
            history_.previous_hash = history_.latest_hash;
            history_.latest_revision = issued.revision;
            history_.latest_time = issued.issued_at;
            std::memcpy(history_.latest_hash.bytes.data(), issued.revision_hash.data, 32);
        }
        else if (issued.revision == history_.latest_revision || issued.revision == previous_revision_) return false;
        else if (issued.revision > previous_revision_)
        {
            previous_revision_ = issued.revision;
            std::memcpy(history_.previous_hash.bytes.data(), issued.revision_hash.data, 32);
        }
        return true;
    }
    bool finish(uint32_t confirmed, PublicationHistory& out) const
    {
        // The scan visits unique index keys. Count plus the latest revision
        // proves there is no missing issuance; table order need not be sorted.
        if (confirmed > history_.latest_revision || count_ != history_.latest_revision ||
            (count_ && (!first_ || history_.created_at > history_.latest_time.utc_seconds)) ||
            (count_ > 1 && previous_revision_ != history_.latest_revision - 1)) return false;
        out = history_;
        out.confirmed_revision = confirmed;
        return true;
    }

  private:
    PublicationHistory history_;
    uint32_t count_ = 0, previous_revision_ = 0;
    bool first_ = false;
};

enum class PublicationHistoryResult : uint8_t
{
    Ready,
    NotFound,
    Invalid
};
template <class View>
PublicationHistoryResult readLogicalPublicationHistory(const View& view, ByteView key, uint64_t generation,
                                                       const GeocacheId& cache, ByteView author, PublicationHistory& out)
{
    ByteView value;
    DraftView draft;
    if (!key.data || key.size != 16 || !author.data || author.size != 64) return PublicationHistoryResult::Invalid;
    if (!view.find(4, key, value)) return PublicationHistoryResult::NotFound;
    if (!decodeDraft(key, value, draft)) return PublicationHistoryResult::Invalid;
    if (draft.generation != generation || draft.author.size != 64 || std::memcmp(draft.author.data, author.data, 64)) return PublicationHistoryResult::NotFound;
    const auto published = draftPublication(view, key, draft);
    PublicationHistoryScan scan;
    size_t cursor = 0;
    MutationView row;
    while (view.next(cursor, row))
        if (row.table == 3 && !scan.consume(row.key, row.value, cache, author)) return PublicationHistoryResult::Invalid;
    return scan.finish(published.confirmed_revision, out) ? PublicationHistoryResult::Ready : PublicationHistoryResult::Invalid;
}
} // namespace geocaching::storage
