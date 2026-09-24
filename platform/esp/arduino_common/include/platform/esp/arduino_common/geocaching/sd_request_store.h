#pragma once
#include "geocaching/domain/version_policy.h"
#include "geocaching/protocol/capabilities.h"
#include "geocaching/protocol/directory_reply.h"
#include "geocaching/protocol/get_request.h"
#include "geocaching/protocol/get_response.h"
#include "geocaching/protocol/publish_request.h"
#include "geocaching/protocol/publish_response.h"
#include "geocaching/protocol/query_request.h"
#include "geocaching/protocol/query_response.h"
#include "geocaching/protocol/verify_record.h"
#include "geocaching/storage/attempt_references.h"
#include "geocaching/storage/attempt_timeout.h"
#include "geocaching/storage/author_history.h"
#include "geocaching/storage/draft_publication.h"
#include "geocaching/storage/draft_record.h"
#include "geocaching/storage/install_record.h"
#include "geocaching/storage/installable_record.h"
#include "geocaching/storage/object_ref.h"
#include "geocaching/storage/pending_request.h"
#include "geocaching/storage/queued_request.h"
#include "geocaching/storage/task_references.h"
#include "platform/esp/arduino_common/geocaching/request_dispatch_store.h"
#include "platform/esp/arduino_common/geocaching/sd_journal.h"

namespace platform::esp::arduino_common::geocaching
{
// Construct only after volume/checkpoint/journal recovery has established the
// committed sequence. One serialized storage worker owns this object; its
// buffers must be allocated off ESP task stacks. No network calls occur here.
class SdRequestStore : public RequestDispatchStore
{
  public:
    SdRequestStore(const ::geocaching::storage::VolumeInstance& volume, uint64_t recovered_sequence,
                   ::geocaching::storage::LogicalState& state)
        : journal_(volume), state_(state), volume_(volume), sequence_(recovered_sequence) {}
    uint64_t committedSequence() const { return sequence_; }
    bool needsRecovery() const { return needs_recovery_; }

    bool commitPending() const { return pending_; }

    DispatchReadResult readPending(const ::geocaching::Destination& local, ::geocaching::ByteView after,
                                   ::geocaching::storage::PendingRequestView& out) override
    {
        using namespace ::geocaching::storage;
        const auto result = nextPendingRequest(state_.view(), local, after, out);
        return result == PendingRequestResult::Ready ? DispatchReadResult::Ready : result == PendingRequestResult::None ? DispatchReadResult::None
                                                                                                                        : DispatchReadResult::Corrupt;
    }
    DispatchReadResult readForSend(::geocaching::ByteView key, DispatchSendView& out) override
    {
        using namespace ::geocaching::storage;
        out = {};
        ::geocaching::ByteView stored, task_bytes;
        OutgoingView outgoing;
        TaskView task;
        const auto view = state_.view();
        if (!view.find(5, key, stored) || !decodeOutgoing(key, stored, outgoing) || outgoing.state != 1 ||
            !view.find(10, outgoing.task_id, task_bytes) || !decodeTask(outgoing.task_id, task_bytes, task) ||
            !requestBelongsToTask(outgoing.task_id, task, key, outgoing)) return DispatchReadResult::Corrupt;
        out.request = outgoing.request;
        out.stopped = !outgoing.continue_intent || !task.continue_intent || task.state == 5;
        return DispatchReadResult::Ready;
    }

    JournalWriteResult saveDraft(::geocaching::ByteView key, ::geocaching::ByteView encoded, uint64_t expected_generation)
    {
        if (needs_recovery_) return JournalWriteResult::Unavailable;
        if (pending_) return JournalWriteResult::Busy;
        using namespace ::geocaching::storage;
        DraftView next, previous;
        ::geocaching::ByteView existing;
        if (!decodeDraft(key, encoded, next)) return JournalWriteResult::Invalid;
        const bool found = state_.view().find(4, key, existing);
        if (found && !decodeDraft(key, existing, previous)) return JournalWriteResult::StateRejected;
        const auto check = checkDraftUpdate(found ? &previous : nullptr, next, expected_generation);
        if (check == DraftUpdateCheck::InvalidGeneration) return JournalWriteResult::Invalid;
        if (check == DraftUpdateCheck::Conflict ||
            (check == DraftUpdateCheck::NeedsRetainedPublication && !draftPublication(state_.view(), key, previous).base_retained))
            return JournalWriteResult::StateRejected;
        const MutationView mutation{4, key, encoded, false};
        return commitMutations(&mutation, 1);
    }

    // Confirmation binds the selected public identity before signing starts.
    // The caller supplies a transient encoding lease; no payload is retained here.
    JournalWriteResult bindDraftAuthor(::geocaching::ByteView key, uint64_t expected_generation,
                                       ::geocaching::ByteView author, uint8_t* workspace, size_t capacity)
    {
        if (needs_recovery_) return JournalWriteResult::Unavailable;
        if (pending_) return JournalWriteResult::Busy;
        using namespace ::geocaching::storage;
        ::geocaching::ByteView value;
        DraftView draft;
        if (!author.data || author.size != 64 || !state_.view().find(4, key, value) ||
            !decodeDraft(key, value, draft) || draft.generation != expected_generation) return JournalWriteResult::StateRejected;
        if (draft.author.size)
            return std::memcmp(draft.author.data, author.data, 64) ? JournalWriteResult::StateRejected : JournalWriteResult::Verified;
        if (draft.generation == UINT64_MAX) return JournalWriteResult::StateRejected;
        draft.author = author;
        ++draft.generation;
        size_t size = 0;
        if (!encodeDraft(key, draft, workspace, capacity, size)) return JournalWriteResult::Invalid;
        return saveDraft(key, {workspace, size}, expected_generation);
    }

    JournalWriteResult stepCommit()
    {
        if (!pending_) return JournalWriteResult::Invalid;
        const auto result = journal_.step();
        if (result == JournalWriteResult::InProgress) return result;
        pending_ = false;
        if (result == JournalWriteResult::Verified)
        {
            if (!state_.commitPrepared())
            {
                needs_recovery_ = true;
                return JournalWriteResult::StateRejected;
            }
            ++sequence_;
        }
        else
        {
            state_.discardPrepared();
            // Even an existing next-sequence file must be reconciled before
            // another command is admitted. Never overwrite a partial commit.
            needs_recovery_ = true;
        }
        return result;
    }

    JournalWriteResult cancelCommit()
    {
        if (!pending_) return JournalWriteResult::Invalid;
        const auto result = journal_.cancel();
        pending_ = false;
        state_.discardPrepared();
        needs_recovery_ = journal_.mayHaveWritten();
        return result;
    }

    // Canonical record bytes stay immutable across reservation and signing.
    // The caller checks the selected local author before starting this operation.
    JournalWriteResult reserveUnsignedRecord(::geocaching::ByteView encoded,
                                             ::geocaching::protocol::RecordCrypto& crypto,
                                             uint8_t* workspace, size_t capacity,
                                             const ::geocaching::storage::StoredTime& time)
    {
        if (needs_recovery_) return JournalWriteResult::Unavailable;
        if (pending_) return JournalWriteResult::Busy;
        ::geocaching::GeocacheId id;
        ::geocaching::RevisionHash hash;
        const auto result = ::geocaching::protocol::deriveGeocacheHashes(encoded, crypto, workspace, capacity, id, hash);
        if (result == ::geocaching::protocol::VerificationResult::CryptoUnavailable) return JournalWriteResult::Unavailable;
        if (result == ::geocaching::protocol::VerificationResult::WorkspaceTooSmall) return JournalWriteResult::StateRejected;
        if (result != ::geocaching::protocol::VerificationResult::Valid) return JournalWriteResult::Invalid;
        ::geocaching::RecordView record;
        if (!::geocaching::protocol::decodeGeocacheRecord(encoded, record)) return JournalWriteResult::Invalid;
        return reserveAuthorVersion(id, record.revision, hash, record.author_public_key, time);
    }

    JournalWriteResult reserveDraftUnsignedRecord(::geocaching::ByteView draft_key, uint64_t generation,
                                                  ::geocaching::ByteView encoded, ::geocaching::protocol::RecordCrypto& crypto,
                                                  uint8_t* workspace, size_t capacity, const ::geocaching::storage::StoredTime& time)
    {
        using namespace ::geocaching;
        using namespace ::geocaching::storage;
        if (needs_recovery_) return JournalWriteResult::Unavailable;
        if (pending_) return JournalWriteResult::Busy;
        VolumeInstance volume;
        if (inspectSdVolume(volume) != SdVolumeResult::Ready || volume != volume_)
        {
            needs_recovery_ = true;
            return JournalWriteResult::Unavailable;
        }
        ByteView value;
        DraftView draft;
        RecordView record;
        if (!state_.view().find(4, draft_key, value) || !decodeDraft(draft_key, value, draft) || draft.generation != generation ||
            !protocol::decodeGeocacheRecord(encoded, record) || draft.author.size != 64 || !draft.has_coordinates ||
            std::memcmp(record.author_public_key.data, draft.author.data, 64) || std::memcmp(record.creation_nonce.data, draft_key.data, 16) ||
            record.state != static_cast<CacheState>(draft.state) || record.latitude_e7 != draft.latitude_e7 || record.longitude_e7 != draft.longitude_e7 ||
            record.name != draft.name || record.description != draft.description || record.hint != draft.hint ||
            record.difficulty_x2 != draft.difficulty_x2 || record.terrain_x2 != draft.terrain_x2 || record.container_size != static_cast<ContainerSize>(draft.container_size))
            return JournalWriteResult::StateRejected;
        GeocacheId id;
        RevisionHash hash;
        if (protocol::deriveGeocacheHashes(encoded, crypto, workspace, capacity, id, hash) != protocol::VerificationResult::Valid)
            return JournalWriteResult::Invalid;
        if (!time.has_utc || time.utc_seconds != record.updated_at) return JournalWriteResult::Invalid;
        if (record.revision == 1 && record.created_at != record.updated_at) return JournalWriteResult::StateRejected;
        size_t cursor = 0;
        MutationView row;
        uint32_t highest = 0;
        while (state_.view().next(cursor, row))
        {
            if (row.table != 3 || row.key.size != 36 || std::memcmp(row.key.data, id.bytes.data(), 32)) continue;
            AuthorIssuedView issued;
            if (!decodeAuthorIssued(row.key, row.value, issued)) return JournalWriteResult::StateRejected;
            if (issued.revision > highest) highest = issued.revision;
        }
        if (record.revision != highest && (highest == UINT32_MAX || record.revision != highest + 1)) return JournalWriteResult::StateRejected;
        if (record.revision > 1)
        {
            std::array<uint8_t, 36> predecessor;
            std::memcpy(predecessor.data(), id.bytes.data(), 32);
            for (unsigned i = 0; i < 4; ++i) predecessor[32 + i] = static_cast<uint8_t>((record.revision - 1) >> ((3 - i) * 8));
            ByteView parent;
            AuthorIssuedView issued;
            if (!state_.view().find(3, {predecessor.data(), predecessor.size()}, parent) ||
                !decodeAuthorIssued({predecessor.data(), predecessor.size()}, parent, issued) ||
                std::memcmp(issued.revision_hash.data, record.previous_hash.data, 32) || !issued.issued_at.has_utc ||
                record.updated_at < issued.issued_at.utc_seconds) return JournalWriteResult::StateRejected;
            predecessor[32] = predecessor[33] = predecessor[34] = 0;
            predecessor[35] = 1;
            if (!state_.view().find(3, {predecessor.data(), predecessor.size()}, parent) ||
                !decodeAuthorIssued({predecessor.data(), predecessor.size()}, parent, issued) || !issued.issued_at.has_utc ||
                record.created_at != issued.issued_at.utc_seconds) return JournalWriteResult::StateRejected;
        }
        std::array<uint8_t, 36> key;
        std::memcpy(key.data(), id.bytes.data(), 32);
        for (unsigned i = 0; i < 4; ++i) key[32 + i] = static_cast<uint8_t>(record.revision >> ((3 - i) * 8));
        const bool existed = state_.view().find(3, {key.data(), key.size()}, value);
        if (existed)
        {
            AuthorIssuedView issued;
            if (!decodeAuthorIssued({key.data(), key.size()}, value, issued) || std::memcmp(issued.revision_hash.data, hash.bytes.data(), 32) ||
                std::memcmp(issued.author_public_key.data, draft.author.data, 64)) return JournalWriteResult::StateRejected;
            if (draft.base_hash.size == 32 && !std::memcmp(draft.base_hash.data, hash.bytes.data(), 32)) return JournalWriteResult::Verified;
        }
        if (draft.generation == UINT64_MAX) return JournalWriteResult::StateRejected;
        // Freeze exact draft fields and their issuance identity in one commit.
        draft.base_hash = {hash.bytes.data(), hash.bytes.size()};
        ++draft.generation;
        size_t draft_size = 0, issued_size = 0;
        if (!encodeDraft(draft_key, draft, workspace, capacity, draft_size)) return JournalWriteResult::Invalid;
        if (!existed && !encodeAuthorIssued(hash, record.author_public_key, time, workspace_.data(), workspace_.size(), issued_size)) return JournalWriteResult::Invalid;
        const MutationView mutations[] = {
            {4, draft_key, {workspace, draft_size}, false},
            {3, {key.data(), key.size()}, {workspace_.data(), issued_size}, false}};
        return commitMutations(mutations, existed ? 1 : 2);
    }

    JournalWriteResult persistNewTask(const ::geocaching::Destination& local,
                                      const ::geocaching::Destination& remote,
                                      const ::geocaching::RequestId& request_id,
                                      const std::array<uint8_t, 16>& task_id, uint8_t task_kind,
                                      ::geocaching::ByteView request,
                                      const ::geocaching::storage::StoredTime& time,
                                      const ::geocaching::storage::RequestTaskTarget& target = {})
    {
        if (needs_recovery_) return JournalWriteResult::Unavailable;
        if (pending_) return JournalWriteResult::Busy;
        using namespace ::geocaching::storage;
        std::array<uint8_t, 48> key{};
        OutgoingView outgoing;
        TaskView task;
        if (sequence_ == UINT64_MAX ||
            !describeNewRequestTask(local, remote, request_id, task_id, task_kind, request, time, key, outgoing, task, target))
            return JournalWriteResult::Invalid;
        const auto before = state_.view();
        ::geocaching::ByteView existing;
        if (before.find(5, {key.data(), key.size()}, existing) || before.find(10, outgoing.task_id, existing))
            return JournalWriteResult::StateRejected;
        size_t task_size = 0;
        if (!encodeTask(outgoing.task_id, task, workspace_.data(), workspace_.size(), task_size)) return JournalWriteResult::Invalid;
        if (task_kind == 2)
        {
            ::geocaching::protocol::GetRequestView get;
            if (!::geocaching::protocol::decodeGetRequest(request, request_id, get) || get.wanted_hash.size != 32 ||
                std::memcmp(get.cache_id.data, target.cache_id.data, 32) || std::memcmp(get.wanted_hash.data, target.revision_hash.data, 32))
                return JournalWriteResult::Invalid;
            CacheHeadView head;
            if (before.find(2, target.cache_id, existing) && !decodeCacheHead(target.cache_id, existing, head)) return JournalWriteResult::StateRejected;
            if (head.install_generation == UINT64_MAX || target.install_generation != head.install_generation + 1) return JournalWriteResult::StateRejected;
            head.install_generation = target.install_generation;
            uint8_t head_value[64];
            size_t head_size = 0;
            if (!encodeCacheHead(target.cache_id, head, head_value, sizeof(head_value), head_size)) return JournalWriteResult::Invalid;
            const MutationView mutations[] = {
                {5, {key.data(), key.size()}, {}, false},
                {10, outgoing.task_id, {workspace_.data(), task_size}, false},
                {2, target.cache_id, {head_value, head_size}, false}};
            return commitMutations(mutations, 3, &outgoing, 0);
        }
        const MutationView mutations[] = {
            {5, {key.data(), key.size()}, {}, false},
            {10, outgoing.task_id, {workspace_.data(), task_size}, false}};
        return commitMutations(mutations, 2, &outgoing, 0);
    }

    uint64_t nextDownloadGeneration(const ::geocaching::GeocacheId& id) const
    {
        ::geocaching::ByteView bytes;
        ::geocaching::storage::CacheHeadView head;
        const ::geocaching::ByteView key{id.bytes.data(), id.bytes.size()};
        if (state_.view().find(2, key, bytes) && !::geocaching::storage::decodeCacheHead(key, bytes, head)) return 0;
        return head.install_generation == UINT64_MAX ? 0 : head.install_generation + 1;
    }

    bool downloadIntentActive(::geocaching::ByteView key, uint64_t generation) const
    {
        ::geocaching::storage::OutgoingView outgoing;
        ::geocaching::storage::TaskView task;
        ::geocaching::storage::CacheHeadView head;
        return !needs_recovery_ && loadDownload(key, generation, outgoing, task, head) &&
               outgoing.continue_intent && task.continue_intent && task.state < 3;
    }

    bool installedFileProof(::geocaching::ByteView request_key, uint64_t generation,
                            std::array<uint8_t, 32>& file_hash, ::geocaching::RevisionHash& revision) const
    {
        using namespace ::geocaching::storage;
        OutgoingView outgoing;
        TaskView task;
        CacheHeadView head;
        if (!loadDownload(request_key, generation, outgoing, task, head) || head.current_hash.size != 32) return false;
        size_t cursor = 0;
        uint64_t latest = 0;
        MutationView row;
        const auto view = state_.view();
        while (view.next(cursor, row))
        {
            if (row.table != 12) continue;
            InstallRecordView installed;
            if (!decodeInstallRecord(row.key, row.value, installed)) return false;
            if (installed.phase == InstallPhase::Installed && installed.generation <= generation && installed.generation > latest &&
                !std::memcmp(installed.cache_id.data, task.cache_id.data, 32) &&
                !std::memcmp(installed.revision_hash.data, head.current_hash.data, 32))
            {
                std::memcpy(file_hash.data(), installed.new_file_hash.data, 32);
                latest = installed.generation;
            }
        }
        if (!latest) return false;
        std::memcpy(revision.bytes.data(), head.current_hash.data, 32);
        return true;
    }

    // Durable receipt is distinct from local installation: the task remains
    // Running until GPX replacement and the final install transaction succeed.
    JournalWriteResult recordDownloadResponse(const ::geocaching::Destination& local, const ::geocaching::Destination& remote,
                                              const ::geocaching::RequestId& id, uint64_t generation, ::geocaching::ByteView response,
                                              ::geocaching::protocol::RecordCrypto& crypto)
    {
        using namespace ::geocaching::storage;
        using namespace ::geocaching::protocol;
        if (needs_recovery_) return JournalWriteResult::Unavailable;
        if (pending_) return JournalWriteResult::Busy;
        uint8_t key[48];
        std::memcpy(key, local.bytes.data(), 16);
        std::memcpy(key + 16, remote.bytes.data(), 16);
        std::memcpy(key + 32, id.bytes.data(), 16);
        ::geocaching::ByteView bytes;
        OutgoingView outgoing;
        TaskView task;
        CacheHeadView head;
        const auto before = state_.view();
        if (!before.find(5, {key, 48}, bytes) || !decodeOutgoing({key, 48}, bytes, outgoing) ||
            !before.find(10, outgoing.task_id, bytes) || !decodeTask(outgoing.task_id, bytes, task) ||
            task.kind != 2 || task.cache_id.size != 32 || task.revision_hash.size != 32 ||
            !requestBelongsToTask(outgoing.task_id, task, {key, 48}, outgoing) ||
            !before.find(2, task.cache_id, bytes) || !decodeCacheHead(task.cache_id, bytes, head) ||
            !generation || outgoing.install_generation != generation || head.install_generation != generation)
            return JournalWriteResult::StateRejected;
        GetRequestView requested;
        GetResponseView reply;
        if (!decodeGetRequest(outgoing.request, id, requested) ||
            !decodeGetResponse(response, id, requested.budget, reply) || reply.has_conflict) return JournalWriteResult::Invalid;
        ::geocaching::GeocacheId cache_id;
        ::geocaching::RevisionHash hash;
        std::memcpy(cache_id.bytes.data(), task.cache_id.data, 32);
        std::memcpy(hash.bytes.data(), task.revision_hash.data, 32);
        VerifiedRecordView verified;
        auto result = VerificationResult::WorkspaceTooSmall;
        if (!state_.withScratch([&](uint8_t* scratch, size_t capacity)
                                { result = verifyGeocache(reply.signed_cache, crypto, scratch, capacity, verified, &cache_id, &hash); })) return JournalWriteResult::StateRejected;
        if (result == VerificationResult::CryptoUnavailable) return JournalWriteResult::Unavailable;
        if (result != VerificationResult::Valid) return JournalWriteResult::Invalid;
        if (!installableVersion(head, verified)) return JournalWriteResult::StateRejected;
        if (outgoing.state == 4)
            return outgoing.terminal_data.size == response.size && !std::memcmp(outgoing.terminal_data.data, response.data, response.size)
                       ? JournalWriteResult::Verified
                       : JournalWriteResult::StateRejected;
        if (!outgoing.continue_intent || !task.continue_intent || task.state >= 3) return JournalWriteResult::StateRejected;
        outgoing.state = 4;
        outgoing.terminal_data = response;
        task.state = 1;
        size_t task_size = 0;
        if (!encodeTask(outgoing.task_id, task, workspace_.data(), workspace_.size(), task_size)) return JournalWriteResult::Invalid;
        const MutationView mutations[] = {{5, {key, 48}, {}, false}, {10, outgoing.task_id, {workspace_.data(), task_size}, false}};
        return commitMutations(mutations, 2, &outgoing, 0);
    }

    // Call only after the staging job has flushed and verified the exact GPX.
    JournalWriteResult prepareDownloadInstall(::geocaching::ByteView request_key,
                                              const std::array<uint8_t, 16>& transaction, uint64_t generation,
                                              const std::array<uint8_t, 32>& new_file_hash, ::geocaching::ByteView old_file_hash,
                                              ::geocaching::protocol::RecordCrypto& crypto)
    {
        using namespace ::geocaching::storage;
        using namespace ::geocaching::protocol;
        if (needs_recovery_) return JournalWriteResult::Unavailable;
        if (pending_) return JournalWriteResult::Busy;
        if (old_file_hash.size && (!old_file_hash.data || old_file_hash.size != 32)) return JournalWriteResult::Invalid;
        OutgoingView outgoing;
        TaskView task;
        CacheHeadView head;
        if (!loadDownload(request_key, generation, outgoing, task, head) || outgoing.state != 4 ||
            !outgoing.continue_intent || !task.continue_intent || task.state >= 3) return JournalWriteResult::StateRejected;
        if (!head.current_hash.size && old_file_hash.size) return JournalWriteResult::StateRejected;
        ::geocaching::RequestId id;
        std::memcpy(id.bytes.data(), request_key.data + 32, 16);
        GetResponseView response;
        if (!decodeGetResponse(outgoing.terminal_data, id, 8192, response) || response.has_conflict) return JournalWriteResult::Invalid;
        ::geocaching::GeocacheId cache;
        ::geocaching::RevisionHash hash;
        std::memcpy(cache.bytes.data(), task.cache_id.data, 32);
        std::memcpy(hash.bytes.data(), task.revision_hash.data, 32);
        VerifiedRecordView verified;
        auto checked = VerificationResult::WorkspaceTooSmall;
        if (!state_.withScratch([&](uint8_t* bytes, size_t capacity)
                                { checked = verifyGeocache(response.signed_cache, crypto, bytes, capacity, verified, &cache, &hash); })) return JournalWriteResult::StateRejected;
        if (checked == VerificationResult::CryptoUnavailable) return JournalWriteResult::Unavailable;
        if (checked != VerificationResult::Valid) return JournalWriteResult::Invalid;
        const auto& record = verified.record;
        if (!installableVersion(head, verified)) return JournalWriteResult::StateRejected;
        ::geocaching::ByteView prior;
        const ::geocaching::ByteView transaction_key{transaction.data(), transaction.size()};
        if (state_.view().find(12, transaction_key, prior))
        {
            InstallRecordView existing;
            if (!decodeInstallRecord(transaction_key, prior, existing)) return JournalWriteResult::StateRejected;
            const bool same = existing.phase == InstallPhase::Prepared && existing.generation == generation &&
                              !std::memcmp(existing.cache_id.data, cache.bytes.data(), 32) &&
                              !std::memcmp(existing.revision_hash.data, hash.bytes.data(), 32) &&
                              !std::memcmp(existing.new_file_hash.data, new_file_hash.data(), 32) &&
                              existing.old_file_hash.size == old_file_hash.size &&
                              (!old_file_hash.size || !std::memcmp(existing.old_file_hash.data, old_file_hash.data, old_file_hash.size));
            return same ? JournalWriteResult::Verified : JournalWriteResult::StateRejected;
        }
        ObjectRefView object;
        object.cache_id = task.cache_id;
        object.previous_hash = record.previous_hash;
        object.revision = record.revision;
        object.state = record.state;
        object.created_at = record.created_at;
        if (state_.view().find(1, task.revision_hash, prior))
        {
            ObjectRefView existing;
            if (!decodeObjectRef(task.revision_hash, prior, existing) || existing.revision != object.revision || existing.state != object.state ||
                existing.created_at != object.created_at || std::memcmp(existing.cache_id.data, object.cache_id.data, 32) ||
                existing.previous_hash.size != object.previous_hash.size || (object.previous_hash.size && std::memcmp(existing.previous_hash.data, object.previous_hash.data, object.previous_hash.size))) return JournalWriteResult::StateRejected;
            object = existing;
        }
        InstallRecordView install{task.cache_id, task.revision_hash, {new_file_hash.data(), new_file_hash.size()}, old_file_hash, generation, InstallPhase::Prepared};
        uint8_t object_bytes[160], install_bytes[160];
        size_t object_size = 0, install_size = 0;
        if (!encodeObjectRef(task.revision_hash, object, object_bytes, sizeof(object_bytes), object_size) ||
            !encodeInstallRecord(transaction_key, install, install_bytes, sizeof(install_bytes), install_size)) return JournalWriteResult::Invalid;
        const MutationView mutations[] = {{1, task.revision_hash, {object_bytes, object_size}, false},
                                          {12, transaction_key, {install_bytes, install_size}, false}};
        return commitMutations(mutations, 2);
    }

    // The install owner supplies the hash observed by reopening the target GPX.
    // Current version, install marker and task completion become visible together.
    JournalWriteResult finishDownloadInstall(::geocaching::ByteView request_key,
                                             const std::array<uint8_t, 16>& transaction, uint64_t generation,
                                             const std::array<uint8_t, 32>& observed_file_hash)
    {
        using namespace ::geocaching::storage;
        if (needs_recovery_) return JournalWriteResult::Unavailable;
        if (pending_) return JournalWriteResult::Busy;
        OutgoingView outgoing;
        TaskView task;
        CacheHeadView head;
        if (!loadDownload(request_key, generation, outgoing, task, head) || outgoing.state != 4) return JournalWriteResult::StateRejected;
        ::geocaching::ByteView value;
        const ::geocaching::ByteView key{transaction.data(), transaction.size()};
        InstallRecordView install;
        ObjectRefView object;
        if (!state_.view().find(12, key, value) || !decodeInstallRecord(key, value, install) || install.generation != generation ||
            std::memcmp(install.cache_id.data, task.cache_id.data, 32) || std::memcmp(install.revision_hash.data, task.revision_hash.data, 32) ||
            std::memcmp(install.new_file_hash.data, observed_file_hash.data(), 32) ||
            !state_.view().find(1, task.revision_hash, value) || !decodeObjectRef(task.revision_hash, value, object) ||
            std::memcmp(object.cache_id.data, task.cache_id.data, 32)) return JournalWriteResult::StateRejected;
        if (install.phase == InstallPhase::Installed)
            return task.state == 3 && head.current_hash.size == 32 && !std::memcmp(head.current_hash.data, task.revision_hash.data, 32)
                       ? JournalWriteResult::Verified
                       : JournalWriteResult::StateRejected;
        if (install.phase != InstallPhase::Prepared || !outgoing.continue_intent || !task.continue_intent || task.state >= 3 ||
            object.revision < head.highest_seen_revision || head.conflict_state == 2) return JournalWriteResult::StateRejected;
        head.current_hash = task.revision_hash;
        head.highest_seen_revision = object.revision;
        install.phase = InstallPhase::Installed;
        task.state = 3;
        uint8_t head_bytes[64], install_bytes[160];
        size_t head_size = 0, install_size = 0, task_size = 0;
        if (!encodeCacheHead(task.cache_id, head, head_bytes, sizeof(head_bytes), head_size) ||
            !encodeInstallRecord(key, install, install_bytes, sizeof(install_bytes), install_size) ||
            !encodeTask(outgoing.task_id, task, workspace_.data(), workspace_.size(), task_size)) return JournalWriteResult::Invalid;
        const MutationView mutations[] = {{2, task.cache_id, {head_bytes, head_size}, false},
                                          {12, key, {install_bytes, install_size}, false},
                                          {10, outgoing.task_id, {workspace_.data(), task_size}, false}};
        return commitMutations(mutations, 3);
    }

    JournalWriteResult reserveAuthorVersion(const ::geocaching::GeocacheId& id, uint32_t revision,
                                            const ::geocaching::RevisionHash& hash, ::geocaching::ByteView public_key,
                                            const ::geocaching::storage::StoredTime& time)
    {
        if (needs_recovery_) return JournalWriteResult::Unavailable;
        if (pending_) return JournalWriteResult::Busy;
        if (!revision || !public_key.data || public_key.size != 64) return JournalWriteResult::Invalid;
        ::geocaching::storage::VolumeInstance current;
        if (inspectSdVolume(current) != SdVolumeResult::Ready)
        {
            needs_recovery_ = true;
            return JournalWriteResult::Unavailable;
        }
        if (current != volume_)
        {
            needs_recovery_ = true;
            return JournalWriteResult::VolumeChanged;
        }
        uint8_t key[36]{};
        std::memcpy(key, id.bytes.data(), 32);
        for (unsigned i = 0; i < 4; ++i) key[32 + i] = static_cast<uint8_t>(revision >> ((3 - i) * 8));
        ::geocaching::ByteView existing;
        if (state_.view().find(3, {key, sizeof(key)}, existing))
        {
            ::geocaching::storage::AuthorIssuedView issued;
            if (!::geocaching::storage::decodeAuthorIssued({key, sizeof(key)}, existing, issued)) return JournalWriteResult::StateRejected;
            return !std::memcmp(issued.revision_hash.data, hash.bytes.data(), 32) &&
                           !std::memcmp(issued.author_public_key.data, public_key.data, 64)
                       ? JournalWriteResult::Verified
                       : JournalWriteResult::StateRejected;
        }
        uint8_t value[192]{};
        size_t value_size = 0;
        if (sequence_ == UINT64_MAX || !::geocaching::storage::encodeAuthorIssued(hash, public_key, time, value, sizeof(value), value_size))
            return JournalWriteResult::Invalid;
        const ::geocaching::storage::MutationView mutation{3, {key, sizeof(key)}, {value, value_size}, false};
        return commitMutations(&mutation, 1);
    }

    // Authenticated source/local destination are supplied by the delivery owner.
    // Revalidates the stored signed publication and commits result + task together.
    JournalWriteResult commitPublishResult(const ::geocaching::Destination& local, const ::geocaching::Destination& remote,
                                           const ::geocaching::RequestId& id, ::geocaching::ByteView response,
                                           ::geocaching::protocol::RecordCrypto& crypto)
    {
        using namespace ::geocaching::storage;
        using namespace ::geocaching::protocol;
        if (needs_recovery_) return JournalWriteResult::Unavailable;
        if (pending_) return JournalWriteResult::Busy;
        if (!response.data || response.size > 512) return JournalWriteResult::Invalid;
        VolumeInstance current;
        if (inspectSdVolume(current) != SdVolumeResult::Ready)
        {
            needs_recovery_ = true;
            return JournalWriteResult::Unavailable;
        }
        if (current != volume_)
        {
            needs_recovery_ = true;
            return JournalWriteResult::VolumeChanged;
        }
        uint8_t key[48];
        std::memcpy(key, local.bytes.data(), 16);
        std::memcpy(key + 16, remote.bytes.data(), 16);
        std::memcpy(key + 32, id.bytes.data(), 16);
        const auto before = state_.view();
        ::geocaching::ByteView value, task_value;
        OutgoingView outgoing;
        TaskView task;
        if (!before.find(5, {key, 48}, value) || !decodeOutgoing({key, 48}, value, outgoing) ||
            !before.find(10, outgoing.task_id, task_value) || !decodeTask(outgoing.task_id, task_value, task) || task.kind != 1 ||
            !requestBelongsToTask(outgoing.task_id, task, {key, 48}, outgoing)) return JournalWriteResult::StateRejected;
        if (outgoing.state == 4)
            return outgoing.terminal_data.size == response.size && !std::memcmp(outgoing.terminal_data.data, response.data, response.size)
                       ? JournalWriteResult::Verified
                       : JournalWriteResult::StateRejected;
        if (!outgoing.continue_intent || !task.continue_intent || task.state == 5) return JournalWriteResult::StateRejected;
        PublishRequestView request;
        if (!decodePublishRequest(outgoing.request, id, request)) return JournalWriteResult::Invalid;
        VerifiedRecordView verified;
        // Borrow the original canonical SignedCache subspan. Verification uses
        // the existing workspace, which can be reused after verification ends.
        auto verified_result = VerificationResult::WorkspaceTooSmall;
        if (!state_.withScratch([&](uint8_t* scratch, size_t capacity)
                                { verified_result = verifyGeocache(
                                      request.signed_cache, crypto,
                                      scratch, capacity, verified); })) return JournalWriteResult::StateRejected;
        if (verified_result == VerificationResult::CryptoUnavailable) return JournalWriteResult::Unavailable;
        if (verified_result != VerificationResult::Valid || task.cache_id.size != 32 || task.revision_hash.size != 32 ||
            std::memcmp(task.cache_id.data, verified.id.bytes.data(), 32) || std::memcmp(task.revision_hash.data, verified.hash.bytes.data(), 32))
            return JournalWriteResult::StateRejected;
        PublishDisposition disposition;
        if (!decodePublishResponse(response, id, verified.id, verified.hash, verified.record.revision, verified.record.state, disposition))
            return JournalWriteResult::Invalid;
        return commitRequestResult(key, outgoing, task, response);
    }

    JournalWriteResult commitDirectoryCapabilities(const ::geocaching::Destination& local, const ::geocaching::Destination& remote,
                                                   const ::geocaching::RequestId& id, ::geocaching::ByteView response)
    {
        using namespace ::geocaching::storage;
        using namespace ::geocaching::protocol;
        if (needs_recovery_) return JournalWriteResult::Unavailable;
        if (pending_) return JournalWriteResult::Busy;
        DirectoryCapabilities capabilities;
        if (!decodeDirectoryCapabilities(response, id, capabilities)) return JournalWriteResult::Invalid;
        VolumeInstance current;
        if (inspectSdVolume(current) != SdVolumeResult::Ready)
        {
            needs_recovery_ = true;
            return JournalWriteResult::Unavailable;
        }
        if (current != volume_)
        {
            needs_recovery_ = true;
            return JournalWriteResult::VolumeChanged;
        }
        uint8_t key[48];
        std::memcpy(key, local.bytes.data(), 16);
        std::memcpy(key + 16, remote.bytes.data(), 16);
        std::memcpy(key + 32, id.bytes.data(), 16);
        ::geocaching::ByteView value, task_value;
        OutgoingView outgoing;
        TaskView task;
        const auto before = state_.view();
        if (!before.find(5, {key, 48}, value) || !decodeOutgoing({key, 48}, value, outgoing) ||
            !before.find(10, outgoing.task_id, task_value) || !decodeTask(outgoing.task_id, task_value, task) || task.kind != 3 ||
            !requestBelongsToTask(outgoing.task_id, task, {key, 48}, outgoing)) return JournalWriteResult::StateRejected;
        if (!matchesCapabilitiesRequest(outgoing.request, id, response.size)) return JournalWriteResult::Invalid;
        if (outgoing.state == 4)
            return outgoing.terminal_data.size == response.size && !std::memcmp(outgoing.terminal_data.data, response.data, response.size)
                       ? JournalWriteResult::Verified
                       : JournalWriteResult::StateRejected;
        if (!outgoing.continue_intent || !task.continue_intent || task.state == 5) return JournalWriteResult::StateRejected;
        return commitRequestResult(key, outgoing, task, response);
    }

    // Store the complete authenticated page in Outgoing before exposing it.
    // SummaryCache is a rebuildable index; it is not the acceptance authority.
    JournalWriteResult commitQueryResult(const ::geocaching::Destination& local, const ::geocaching::Destination& remote,
                                         const ::geocaching::RequestId& id, ::geocaching::ByteView response)
    {
        using namespace ::geocaching::storage;
        using namespace ::geocaching::protocol;
        if (needs_recovery_) return JournalWriteResult::Unavailable;
        if (pending_) return JournalWriteResult::Busy;
        uint8_t key[48];
        std::memcpy(key, local.bytes.data(), 16);
        std::memcpy(key + 16, remote.bytes.data(), 16);
        std::memcpy(key + 32, id.bytes.data(), 16);
        const auto before = state_.view();
        ::geocaching::ByteView value, task_value;
        OutgoingView outgoing;
        TaskView task;
        if (!before.find(5, {key, 48}, value) || !decodeOutgoing({key, 48}, value, outgoing) ||
            !before.find(10, outgoing.task_id, task_value) || !decodeTask(outgoing.task_id, task_value, task) ||
            task.kind != 3 || !requestBelongsToTask(outgoing.task_id, task, {key, 48}, outgoing)) return JournalWriteResult::StateRejected;
        if (!matchesQueryReply(outgoing.request, response, id)) return JournalWriteResult::Invalid;
        VolumeInstance current;
        if (inspectSdVolume(current) != SdVolumeResult::Ready)
        {
            needs_recovery_ = true;
            return JournalWriteResult::Unavailable;
        }
        if (current != volume_)
        {
            needs_recovery_ = true;
            return JournalWriteResult::VolumeChanged;
        }
        if (outgoing.state == 4)
            return outgoing.terminal_data.size == response.size && !std::memcmp(outgoing.terminal_data.data, response.data, response.size)
                       ? JournalWriteResult::Verified
                       : JournalWriteResult::StateRejected;
        if (!outgoing.continue_intent || !task.continue_intent || task.state == 5) return JournalWriteResult::StateRejected;
        return commitRequestResult(key, outgoing, task, response);
    }

    // Task-level stop is authoritative for every child request. Dispatch must
    // require both task and Outgoing continuation intent, including after reboot.
    JournalWriteResult stopTask(const std::array<uint8_t, 16>& task_id)
    {
        using namespace ::geocaching::storage;
        if (needs_recovery_) return JournalWriteResult::Unavailable;
        if (pending_) return JournalWriteResult::Busy;
        VolumeInstance current;
        if (inspectSdVolume(current) != SdVolumeResult::Ready)
        {
            needs_recovery_ = true;
            return JournalWriteResult::Unavailable;
        }
        if (current != volume_)
        {
            needs_recovery_ = true;
            return JournalWriteResult::VolumeChanged;
        }
        const ::geocaching::ByteView key{task_id.data(), task_id.size()};
        ::geocaching::ByteView value;
        TaskView task;
        if (!state_.view().find(10, key, value) || !decodeTask(key, value, task)) return JournalWriteResult::StateRejected;
        if (task.state == 5 && !task.continue_intent) return JournalWriteResult::Verified;
        if (task.state == 3 || sequence_ == UINT64_MAX) return JournalWriteResult::StateRejected;
        task.state = 5;
        task.continue_intent = false;
        size_t value_size = 0;
        if (!encodeTask(key, task, workspace_.data(), workspace_.size(), value_size)) return JournalWriteResult::Invalid;
        const MutationView mutation{10, key, {workspace_.data(), value_size}, false};
        return commitMutations(&mutation, 1);
    }

    // Caller reacquires the committed request view and invokes transport only
    // after Verified. An unresolved attempt requires explicit recovery/retry.
    JournalWriteResult beginAttempt(const ::geocaching::Destination& local, ::geocaching::ByteView request_key,
                                    const std::array<uint8_t, 16>& attempt_id, const ::geocaching::storage::StoredTime& time)
    {
        using namespace ::geocaching::storage;
        if (needs_recovery_) return JournalWriteResult::Unavailable;
        if (pending_) return JournalWriteResult::Busy;
        if (!request_key.data || request_key.size != 48 || sequence_ == UINT64_MAX) return JournalWriteResult::Invalid;
        const auto before = state_.view();
        ::geocaching::ByteView value, task_value, existing;
        PendingRequestView pending;
        OutgoingView outgoing;
        TaskView task;
        if (!before.find(5, request_key, value) || inspectPendingRequest(before, local, request_key, value, pending) != PendingRequestResult::Ready ||
            !decodeOutgoing(request_key, value, outgoing) || !before.find(10, outgoing.task_id, task_value) ||
            !decodeTask(outgoing.task_id, task_value, task)) return JournalWriteResult::StateRejected;
        uint8_t key[64];
        std::memcpy(key, request_key.data, 48);
        std::memcpy(key + 48, attempt_id.data(), 16);
        if (before.find(13, {key, 64}, existing)) return JournalWriteResult::StateRejected;
        TxAttemptView attempt;
        attempt.submitted = time;
        uint8_t attempt_value[192];
        size_t attempt_size = 0, task_size = 0;
        outgoing.state = 1;
        if (task.state == 0) task.state = 1;
        if (!encodeTxAttempt({key, 64}, attempt, attempt_value, sizeof(attempt_value), attempt_size) ||
            !encodeTask(outgoing.task_id, task, workspace_.data(), workspace_.size(), task_size)) return JournalWriteResult::Invalid;
        const MutationView mutations[] = {
            {5, request_key, {}, false},
            {10, outgoing.task_id, {workspace_.data(), task_size}, false},
            {13, {key, 64}, {attempt_value, attempt_size}, false}};
        return commitMutations(mutations, 3, &outgoing, 0);
    }

    JournalWriteResult recordAttemptHash(::geocaching::ByteView attempt_key, const std::array<uint8_t, 32>& hash)
    {
        using namespace ::geocaching::storage;
        if (needs_recovery_) return JournalWriteResult::Unavailable;
        if (pending_) return JournalWriteResult::Busy;
        if (!attempt_key.data || attempt_key.size != 64) return JournalWriteResult::Invalid;
        VolumeInstance current;
        if (inspectSdVolume(current) != SdVolumeResult::Ready)
        {
            needs_recovery_ = true;
            return JournalWriteResult::Unavailable;
        }
        if (current != volume_)
        {
            needs_recovery_ = true;
            return JournalWriteResult::VolumeChanged;
        }
        ::geocaching::ByteView stored;
        TxAttemptView attempt;
        if (!state_.view().find(13, attempt_key, stored) || !decodeTxAttempt(attempt_key, stored, attempt)) return JournalWriteResult::StateRejected;
        const auto transition = recordAttemptTransportHash(attempt, {hash.data(), hash.size()});
        if (transition == AttemptTransition::Unchanged) return JournalWriteResult::Verified;
        if (transition == AttemptTransition::Rejected || sequence_ == UINT64_MAX) return JournalWriteResult::StateRejected;
        uint8_t value[192]{};
        size_t value_size = 0;
        if (!encodeTxAttempt(attempt_key, attempt, value, sizeof(value), value_size)) return JournalWriteResult::Invalid;
        const MutationView mutation{13, attempt_key, {value, value_size}, false};
        return commitMutations(&mutation, 1);
    }

    // The delivery owner maps an authenticated event to this exact attempt.
    // Delivered means transport delivery, not acceptance of the public record.
    JournalWriteResult finishAttempt(::geocaching::ByteView attempt_key, ::geocaching::storage::TxAttemptState terminal,
                                     const ::geocaching::storage::StoredTime& finished)
    {
        using namespace ::geocaching::storage;
        if (needs_recovery_) return JournalWriteResult::Unavailable;
        if (pending_) return JournalWriteResult::Busy;
        if (!attempt_key.data || attempt_key.size != 64 ||
            (terminal != TxAttemptState::Delivered && terminal != TxAttemptState::Failed && terminal != TxAttemptState::CancelledBeforeSend))
            return JournalWriteResult::Invalid;
        VolumeInstance current;
        if (inspectSdVolume(current) != SdVolumeResult::Ready)
        {
            needs_recovery_ = true;
            return JournalWriteResult::Unavailable;
        }
        if (current != volume_)
        {
            needs_recovery_ = true;
            return JournalWriteResult::VolumeChanged;
        }
        const auto before = state_.view();
        ::geocaching::ByteView value, outgoing_value;
        TxAttemptView attempt;
        OutgoingView outgoing;
        if (!before.find(13, attempt_key, value) || !decodeTxAttempt(attempt_key, value, attempt) ||
            !before.find(5, attempt.request_key, outgoing_value) || !decodeOutgoing(attempt.request_key, outgoing_value, outgoing))
            return JournalWriteResult::StateRejected;
        const auto transition = finishAttemptTransport(attempt, terminal, finished);
        if (transition == AttemptTransition::Unchanged) return JournalWriteResult::Verified;
        if (transition == AttemptTransition::Rejected || sequence_ == UINT64_MAX) return JournalWriteResult::StateRejected;
        // Do not downgrade a business result that arrived before its transport
        // event. Task/Outgoing continuation flags remain unchanged by delivery.
        if (outgoing.state < 4) outgoing.state = terminal == TxAttemptState::Delivered ? 2 : 3;
        uint8_t attempt_value[192]{};
        size_t attempt_size = 0;
        if (!encodeTxAttempt(attempt_key, attempt, attempt_value, sizeof(attempt_value), attempt_size))
            return JournalWriteResult::Invalid;
        const MutationView mutations[] = {
            {13, attempt_key, {attempt_value, attempt_size}, false},
            {5, attempt.request_key, {}, false}};
        return commitMutations(mutations, 2, &outgoing, 1);
    }

    JournalWriteResult expireOneAttempt(const ::geocaching::storage::StoredTime& now, uint64_t recovery_started_ms,
                                        uint64_t timeout_ms, bool& expired)
    {
        using namespace ::geocaching::storage;
        expired = false;
        if (needs_recovery_) return JournalWriteResult::Unavailable;
        if (pending_) return JournalWriteResult::Busy;
        size_t cursor = 0;
        MutationView entry;
        const auto before = state_.view();
        while (before.next(cursor, entry))
        {
            if (entry.table != 13) continue;
            TxAttemptView attempt;
            if (!decodeTxAttempt(entry.key, entry.value, attempt)) return JournalWriteResult::StateRejected;
            if (!attemptTimeoutReached(attempt, now, recovery_started_ms, timeout_ms)) continue;
            const auto result = finishAttempt(entry.key, TxAttemptState::Failed, now);
            expired = result == JournalWriteResult::Verified;
            return result;
        }
        return JournalWriteResult::Verified;
    }

  private:
    bool installableVersion(const ::geocaching::storage::CacheHeadView& head,
                            const ::geocaching::protocol::VerifiedRecordView& incoming) const
    {
        if (incoming.record.revision < head.highest_seen_revision || head.conflict_state == 2) return false;
        if (!head.current_hash.size) return ::geocaching::storage::installableRecord(head, nullptr, incoming);
        ::geocaching::ByteView bytes;
        ::geocaching::storage::ObjectRefView object;
        if (!state_.view().find(1, head.current_hash, bytes) || !::geocaching::storage::decodeObjectRef(head.current_hash, bytes, object) ||
            std::memcmp(object.cache_id.data, incoming.id.bytes.data(), 32)) return false;
        return ::geocaching::storage::installableRecord(head, &object, incoming);
    }

    bool loadDownload(::geocaching::ByteView key, uint64_t generation,
                      ::geocaching::storage::OutgoingView& outgoing, ::geocaching::storage::TaskView& task,
                      ::geocaching::storage::CacheHeadView& head) const
    {
        using namespace ::geocaching::storage;
        ::geocaching::ByteView value;
        const auto view = state_.view();
        return generation && view.find(5, key, value) && decodeOutgoing(key, value, outgoing) &&
               view.find(10, outgoing.task_id, value) && decodeTask(outgoing.task_id, value, task) && task.kind == 2 &&
               task.cache_id.size == 32 && task.revision_hash.size == 32 && requestBelongsToTask(outgoing.task_id, task, key, outgoing) &&
               view.find(2, task.cache_id, value) && decodeCacheHead(task.cache_id, value, head) &&
               outgoing.install_generation == generation && head.install_generation == generation;
    }

    JournalWriteResult commitRequestResult(const uint8_t key[48], ::geocaching::storage::OutgoingView outgoing,
                                           ::geocaching::storage::TaskView task, ::geocaching::ByteView response)
    {
        using namespace ::geocaching::storage;
        const auto before = state_.view();
        outgoing.state = 4;
        outgoing.terminal_data = response;
        bool all_confirmed = true;
        for (size_t i = 0; i < task.request_count; ++i)
        {
            if (!std::memcmp(task.requests[i].data, key, 48)) continue;
            ::geocaching::ByteView other_value;
            OutgoingView other;
            if (!before.find(5, task.requests[i], other_value) || !decodeOutgoing(task.requests[i], other_value, other)) return JournalWriteResult::StateRejected;
            all_confirmed = all_confirmed && other.state == 4;
        }
        task.state = all_confirmed ? 3 : 2;
        size_t task_size = 0;
        if (!encodeTask(outgoing.task_id, task, workspace_.data(), workspace_.size(), task_size)) return JournalWriteResult::Invalid;
        const MutationView mutations[] = {
            {5, {key, 48}, {}, false},
            {10, outgoing.task_id, {workspace_.data(), task_size}, false}};
        return commitMutations(mutations, 2, &outgoing, 0);
    }

    JournalWriteResult commitMutations(const ::geocaching::storage::MutationView* mutations, size_t count,
                                       const ::geocaching::storage::OutgoingView* outgoing = nullptr,
                                       size_t outgoing_index = 0)
    {
        using namespace ::geocaching::storage;
        if (pending_) return JournalWriteResult::Busy;
        TransactionEncoding encoding;
        if (sequence_ == UINT64_MAX || count > pending_mutations_.size() ||
            !encoding.open(sequence_, mutations, count)) return JournalWriteResult::Invalid;
        if (outgoing && (outgoing_index >= count || mutations[outgoing_index].table != 5 ||
                         mutations[outgoing_index].erase)) return JournalWriteResult::Invalid;
        const auto before = state_.view();
        if (!state_.prepareGenerated(
                mutations, count, outgoing ? outgoing_index : count,
                [&](uint8_t* output, size_t capacity, size_t& written)
                {
                    return outgoing && encodeOutgoing(mutations[outgoing_index].key, *outgoing, output, capacity, written);
                },
                [&](const auto& candidate)
                { return validateTaskReferences(candidate) && validateAuthorHistory(before, candidate) &&
                         validateAttemptReferences(candidate); })) return JournalWriteResult::StateRejected;
        const auto candidate = state_.preparedView();
        for (size_t i = 0; i < count; ++i)
        {
            const auto& source = mutations[i];
            std::memcpy(pending_keys_[i].data(), source.key.data, source.key.size);
            pending_mutations_[i] = {source.table, {pending_keys_[i].data(), source.key.size}, {}, source.erase};
            if (!source.erase && !candidate.find(source.table, pending_mutations_[i].key, pending_mutations_[i].value))
            {
                state_.discardPrepared();
                return JournalWriteResult::StateRejected;
            }
        }
        const auto result = journal_.begin(sequence_, pending_mutations_.data(), count);
        pending_ = result == JournalWriteResult::InProgress;
        if (!pending_) state_.discardPrepared();
        return result;
    }

  private:
    // Current commands atomically touch at most three records. Values borrow
    // the frozen candidate arena; only small keys/descriptors survive steps.
    std::array<::geocaching::storage::MutationView, 3> pending_mutations_{};
    std::array<std::array<uint8_t, 96>, 3> pending_keys_{};
    bool pending_ = false;
    SdGeocachingJournal journal_;
    ::geocaching::storage::LogicalState& state_;
    const ::geocaching::storage::VolumeInstance volume_;
    uint64_t sequence_ = 0;
    bool needs_recovery_ = false;
    std::array<uint8_t, 256> workspace_{};
};
} // namespace platform::esp::arduino_common::geocaching
