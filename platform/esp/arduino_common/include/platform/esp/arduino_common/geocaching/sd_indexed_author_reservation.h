#pragma once
#include "geocaching/protocol/verify_record.h"
#include "geocaching/storage/author_issued.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_draft_save.h"

namespace platform::esp::arduino_common::geocaching
{
// AuthorIssue pins canonical record bytes through reservation and signing.
// Draft text is encoded into its supplied signing workspace before the frame
// is reused for history reads. Only one small issuance value is owned here.
class SdIndexedAuthorReservation
{
  public:
    SdIndexedAuthorReservation(const ::geocaching::storage::VolumeInstance& volume, ::geocaching::protocol::RecordCrypto& crypto)
        : volume_(volume), crypto_(crypto) {}
    bool beginBind(const ::geocaching::storage::IndexRootView& root, unsigned copy, ::geocaching::ByteView draft,
                   uint64_t generation, ::geocaching::ByteView author, uint8_t* workspace, size_t workspace_capacity,
                   uint8_t* frame, size_t capacity, ::geocaching::storage::IndexRootBytes& candidate)
    {
        if (!author.data || author.size != author_.size() || !draft.data || draft.size != draft_.size() || !generation ||
            !initialize(root, copy, {}, workspace, workspace_capacity, frame, capacity, candidate)) return false;
        std::memcpy(author_.data(), author.data, author_.size());
        std::memcpy(draft_.data(), draft.data, draft_.size());
        generation_ = generation;
        if (!io_.emplace<SdIndexGet>(volume_).begin(root_, 4, {draft_.data(), draft_.size()}, frame_, capacity_)) return false;
        phase_ = Phase::Bind;
        result_ = IndexedCommitStep::Working;
        return true;
    }
    bool beginReserve(const ::geocaching::storage::IndexRootView& root, unsigned copy, ::geocaching::ByteView encoded,
                      const ::geocaching::storage::StoredTime& issued, uint8_t* workspace, size_t workspace_capacity,
                      uint8_t* frame, size_t capacity, ::geocaching::storage::IndexRootBytes& candidate,
                      ::geocaching::ByteView draft = {}, uint64_t generation = 0)
    {
        using namespace ::geocaching;
        RecordView record;
        GeocacheId id;
        if ((draft.size && (!draft.data || draft.size != draft_.size() || !generation)) ||
            !initialize(root, copy, encoded, workspace, workspace_capacity, frame, capacity, candidate) ||
            !protocol::decodeGeocacheRecord(encoded, record) ||
            protocol::deriveGeocacheHashes(encoded, crypto_, workspace, workspace_capacity, id, hash_) != protocol::VerificationResult::Valid) return false;
        encoded_ = encoded;
        issued_ = issued;
        generation_ = generation;
        std::memcpy(key_.data(), id.bytes.data(), 32);
        for (unsigned i = 0; i < 4; ++i) key_[32 + i] = static_cast<uint8_t>(record.revision >> ((3 - i) * 8));
        std::memcpy(author_.data(), record.author_public_key.data, author_.size());
        has_draft_ = draft.size != 0;
        if (has_draft_)
        {
            if (!issued.has_utc || issued.utc_seconds != record.updated_at ||
                (record.revision == 1 && record.created_at != record.updated_at)) return false;
            std::memcpy(draft_.data(), draft.data, draft_.size());
            if (!io_.emplace<SdIndexGet>(volume_).begin(root_, 4, {draft_.data(), draft_.size()}, frame_, capacity_)) return false;
            phase_ = Phase::Draft;
        }
        else
        {
            if (!io_.emplace<SdIndexGet>(volume_).begin(root_, 3, {key_.data(), key_.size()}, frame_, capacity_)) return false;
            phase_ = Phase::Issued;
        }
        result_ = IndexedCommitStep::Working;
        return true;
    }
    bool inputConsumed() const
    {
        return result_ != IndexedCommitStep::Working ||
               (phase_ == Phase::Commit && std::get<SdIndexedCommit>(io_).inputConsumed()) ||
               (phase_ == Phase::BindCommit && std::get<SdIndexedDraftSave>(io_).inputConsumed());
    }
    bool committed(::geocaching::storage::IndexRootView& out) const
    {
        out = {};
        if (result_ != IndexedCommitStep::Verified) return false;
        if (duplicate_)
        {
            out = root_;
            return true;
        }
        return phase_ == Phase::BindCommit ? std::get<SdIndexedDraftSave>(io_).committed(out) : std::get<SdIndexedCommit>(io_).committed(out);
    }
    IndexedCommitStep step()
    {
        using namespace ::geocaching;
        using namespace ::geocaching::storage;
        if (result_ != IndexedCommitStep::Working) return result_;
        if (phase_ == Phase::Commit) return result_ = std::get<SdIndexedCommit>(io_).step();
        if (phase_ == Phase::BindCommit) return result_ = std::get<SdIndexedDraftSave>(io_).step();
        if (phase_ == Phase::History) return history();
        auto& get = std::get<SdIndexGet>(io_);
        const auto status = get.step();
        if (status == IndexGetStep::Working) return result_;
        if (status != IndexGetStep::Ready && !(phase_ == Phase::Issued && status == IndexGetStep::NotFound)) return error(status);
        if (phase_ == Phase::Issued)
        {
            if (status == IndexGetStep::Ready)
            {
                AuthorIssuedView prior;
                if (!decodeAuthorIssued({key_.data(), key_.size()}, get.value(), prior) ||
                    std::memcmp(prior.revision_hash.data, hash_.bytes.data(), 32) || std::memcmp(prior.author_public_key.data, author_.data(), 64)) return fail(IndexedCommitStep::Invalid);
                return unchanged();
            }
            return commit(false);
        }
        DraftView draft;
        const ByteView draft_key{draft_.data(), draft_.size()};
        if (!decodeDraft(draft_key, get.value(), draft) || draft.generation != generation_) return fail(IndexedCommitStep::Invalid);
        if (phase_ == Phase::Bind)
        {
            if (draft.author.size)
                return std::memcmp(draft.author.data, author_.data(), 64) ? fail(IndexedCommitStep::Invalid) : unchanged();
            if (generation_ == UINT64_MAX) return fail(IndexedCommitStep::Invalid);
            draft.author = {author_.data(), author_.size()};
            ++draft.generation;
            if (!encodeDraft(draft_key, draft, workspace_, workspace_capacity_, draft_size_) ||
                !io_.emplace<SdIndexedDraftSave>(volume_).begin(root_, copy_, draft_key, {workspace_, draft_size_}, generation_, frame_, capacity_, *candidate_)) return fail(IndexedCommitStep::Invalid);
            phase_ = Phase::BindCommit;
            return result_;
        }
        RecordView record;
        if (!protocol::decodeGeocacheRecord(encoded_, record) || draft.author.size != 64 || !draft.has_coordinates ||
            std::memcmp(draft.author.data, author_.data(), 64) || std::memcmp(record.creation_nonce.data, draft_.data(), 16) ||
            record.state != static_cast<CacheState>(draft.state) || record.latitude_e7 != draft.latitude_e7 || record.longitude_e7 != draft.longitude_e7 ||
            record.name != draft.name || record.description != draft.description || record.hint != draft.hint ||
            record.difficulty_x2 != draft.difficulty_x2 || record.terrain_x2 != draft.terrain_x2 || record.container_size != static_cast<ContainerSize>(draft.container_size)) return fail(IndexedCommitStep::Invalid);
        base_matches_ = draft.base_hash.size == 32 && !std::memcmp(draft.base_hash.data, hash_.bytes.data(), 32);
        if (generation_ != UINT64_MAX)
        {
            draft.base_hash = {hash_.bytes.data(), hash_.bytes.size()};
            ++draft.generation;
            if (!encodeDraft(draft_key, draft, workspace_, workspace_capacity_, draft_size_)) return fail(IndexedCommitStep::Invalid);
        }
        if (!io_.emplace<SdIndexScan>(volume_).begin(root_, 3, frame_, capacity_)) return fail(IndexedCommitStep::Invalid);
        phase_ = Phase::History;
        return result_;
    }

  private:
    enum class Phase : uint8_t
    {
        Bind,
        BindCommit,
        Draft,
        Issued,
        History,
        Commit
    };
    bool initialize(const ::geocaching::storage::IndexRootView& root, unsigned copy, ::geocaching::ByteView encoded,
                    uint8_t* workspace, size_t workspace_capacity, uint8_t* frame, size_t capacity,
                    ::geocaching::storage::IndexRootBytes& candidate)
    {
        if (result_ != IndexedCommitStep::Idle || copy > 1 || !::geocaching::storage::validIndexRoot(root) || !frame || capacity < 24 || !workspace || !workspace_capacity) return false;
        const ::geocaching::ByteView buffers[] = {root.shards, {candidate.data(), candidate.size()}, {frame, capacity}, {workspace, workspace_capacity}, encoded};
        for (size_t i = 0; i < 5; ++i)
            for (size_t j = 0; j < i; ++j)
            {
                const auto a = reinterpret_cast<uintptr_t>(buffers[i].data), b = reinterpret_cast<uintptr_t>(buffers[j].data);
                if (buffers[i].size && buffers[j].size && (a <= b ? b - a < buffers[i].size : a - b < buffers[j].size)) return false;
            }
        root_ = root;
        copy_ = copy;
        frame_ = frame;
        capacity_ = capacity;
        workspace_ = workspace;
        workspace_capacity_ = workspace_capacity;
        candidate_ = &candidate;
        return true;
    }
    IndexedCommitStep history()
    {
        using namespace ::geocaching;
        using namespace ::geocaching::storage;
        auto& scan = std::get<SdIndexScan>(io_);
        const auto status = scan.step();
        if (status == IndexScanStep::Working) return result_;
        RecordView record;
        if (!protocol::decodeGeocacheRecord(encoded_, record)) return fail(IndexedCommitStep::Invalid);
        if (status == IndexScanStep::End)
        {
            if (record.revision != highest_ && (highest_ == UINT32_MAX || record.revision != highest_ + 1)) return fail(IndexedCommitStep::Invalid);
            if (record.revision > 1 && (!parent_valid_ || !first_valid_)) return fail(IndexedCommitStep::Invalid);
            if (existed_ && base_matches_) return unchanged();
            if (!draft_size_) return fail(IndexedCommitStep::Invalid);
            return commit(existed_);
        }
        if (status != IndexScanStep::Item) return error(status);
        MutationView row;
        AuthorIssuedView issued;
        if (!scan.item(row) || !decodeAuthorIssued(row.key, row.value, issued)) return fail(IndexedCommitStep::Invalid);
        if (!std::memcmp(row.key.data, key_.data(), 32))
        {
            if (std::memcmp(issued.author_public_key.data, author_.data(), 64)) return fail(IndexedCommitStep::Invalid);
            if (issued.revision > highest_) highest_ = issued.revision;
            if (issued.revision == record.revision)
            {
                if (std::memcmp(issued.revision_hash.data, hash_.bytes.data(), 32)) return fail(IndexedCommitStep::Invalid);
                existed_ = true;
            }
            if (issued.revision == 1) first_valid_ = issued.issued_at.has_utc && issued.issued_at.utc_seconds == record.created_at;
            if (record.revision > 1 && issued.revision == record.revision - 1)
                parent_valid_ = issued.issued_at.has_utc && record.updated_at >= issued.issued_at.utc_seconds &&
                                !std::memcmp(issued.revision_hash.data, record.previous_hash.data, 32);
        }
        if (!scan.advance()) return fail(IndexedCommitStep::Invalid);
        return result_;
    }
    IndexedCommitStep commit(bool existed)
    {
        using namespace ::geocaching::storage;
        size_t count = 0, size = 0;
        if (has_draft_) mutations_[count++] = {4, {draft_.data(), draft_.size()}, {workspace_, draft_size_}, false};
        if (!existed)
        {
            if (!encodeAuthorIssued(hash_, {author_.data(), author_.size()}, issued_, issued_value_.data(), issued_value_.size(), size)) return fail(IndexedCommitStep::Invalid);
            mutations_[count++] = {3, {key_.data(), key_.size()}, {issued_value_.data(), size}, false};
        }
        if (!count || !io_.emplace<SdIndexedCommit>(volume_).begin(root_, copy_, mutations_.data(), count, frame_, capacity_, *candidate_)) return fail(IndexedCommitStep::Invalid);
        phase_ = Phase::Commit;
        return result_;
    }
    IndexedCommitStep unchanged()
    {
        duplicate_ = true;
        return fail(IndexedCommitStep::Verified);
    }
    template <class Status>
    IndexedCommitStep error(Status status)
    {
        return fail(status == Status::IoError ? IndexedCommitStep::IoError : status == Status::VolumeChanged ? IndexedCommitStep::VolumeChanged
                                                                                                             : IndexedCommitStep::Invalid);
    }
    IndexedCommitStep fail(IndexedCommitStep status)
    {
        io_.emplace<std::monostate>();
        return result_ = status;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::protocol::RecordCrypto& crypto_;
    ::geocaching::storage::IndexRootView root_;
    ::geocaching::storage::IndexRootBytes* candidate_ = nullptr;
    ::geocaching::ByteView encoded_;
    ::geocaching::storage::StoredTime issued_;
    ::geocaching::RevisionHash hash_;
    std::array<::geocaching::storage::MutationView, 2> mutations_{};
    std::array<uint8_t, 16> draft_{};
    std::array<uint8_t, 36> key_{};
    std::array<uint8_t, 64> author_{};
    std::array<uint8_t, 192> issued_value_{};
    uint8_t *frame_ = nullptr, *workspace_ = nullptr;
    size_t capacity_ = 0, workspace_capacity_ = 0, draft_size_ = 0;
    uint64_t generation_ = 0;
    uint32_t highest_ = 0;
    unsigned copy_ = 0;
    bool has_draft_ = false, existed_ = false, base_matches_ = false, parent_valid_ = false, first_valid_ = false, duplicate_ = false;
    Phase phase_ = Phase::Issued;
    IndexedCommitStep result_ = IndexedCommitStep::Idle;
    std::variant<std::monostate, SdIndexGet, SdIndexScan, SdIndexedCommit, SdIndexedDraftSave> io_;
};
static_assert(sizeof(SdIndexedAuthorReservation) <= 2304, "Author reservation owns history metadata, not record or draft payloads");
} // namespace platform::esp::arduino_common::geocaching
