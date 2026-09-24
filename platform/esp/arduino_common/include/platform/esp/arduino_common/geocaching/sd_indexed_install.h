#pragma once
#include "geocaching/protocol/get_request.h"
#include "geocaching/protocol/get_response.h"
#include "geocaching/storage/install_record.h"
#include "geocaching/storage/installable_record.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_commit.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_download_context.h"

namespace platform::esp::arduino_common::geocaching
{
// The storage owner pins the root and disjoint frame/verification leases until
// completion. Only small metadata is retained; SignedCache stays in the read
// frame and is authenticated before that frame is reused. GPX file operations
// remain the responsibility of SdDownloadPort/GpxInstall.
class SdIndexedInstall
{
  public:
    SdIndexedInstall(const ::geocaching::storage::VolumeInstance& volume, ::geocaching::protocol::RecordCrypto& crypto)
        : volume_(volume), crypto_(crypto) {}

    bool beginPrepare(const ::geocaching::storage::IndexRootView& root, unsigned copy, ::geocaching::ByteView request,
                      uint64_t generation, const std::array<uint8_t, 32>& file_hash, ::geocaching::ByteView old_hash,
                      uint8_t* frame, size_t capacity, uint8_t* verification, size_t verification_capacity,
                      ::geocaching::storage::IndexRootBytes& candidate)
    {
        if ((old_hash.size && (!old_hash.data || old_hash.size != 32)) || !verification || !verification_capacity) return false;
        if (!begin(root, copy, request, generation, file_hash, frame, capacity, verification, verification_capacity, candidate)) return false;
        old_present_ = old_hash.size != 0;
        if (old_present_) std::memcpy(old_hash_.data(), old_hash.data, old_hash_.size());
        return true;
    }

    // observed_hash must come from reopening and hashing the installed GPX.
    bool beginFinish(const ::geocaching::storage::IndexRootView& root, unsigned copy, ::geocaching::ByteView request,
                     uint64_t generation, const std::array<uint8_t, 32>& observed_hash,
                     uint8_t* frame, size_t capacity, ::geocaching::storage::IndexRootBytes& candidate)
    {
        if (!begin(root, copy, request, generation, observed_hash, frame, capacity, nullptr, 0, candidate)) return false;
        finishing_ = true;
        return true;
    }

    bool committed(::geocaching::storage::IndexRootView& out) const
    {
        out = {};
        if (result_ != IndexedCommitStep::Verified) return false;
        if (duplicate_) out = root_;
        else return std::get<SdIndexedCommit>(io_).committed(out);
        return true;
    }

    IndexedCommitStep step()
    {
        using namespace ::geocaching;
        using namespace ::geocaching::storage;
        if (result_ != IndexedCommitStep::Working) return result_;
        if (phase_ == Phase::Commit) return result_ = std::get<SdIndexedCommit>(io_).step();
        if (phase_ == Phase::Context)
        {
            auto& context = std::get<SdIndexedDownloadContext>(io_);
            const auto status = context.step();
            if (status == IndexGetStep::Working) return result_;
            if (status != IndexGetStep::Ready) return error(status);
            OutgoingView outgoing;
            TaskView task;
            CacheHeadView head;
            if (!context.view(outgoing, task, head) || outgoing.state != 4) return fail();
            active_ = context.intentActive();
            if (!finishing_ && !active_) return fail();
            std::memcpy(task_id_.data(), outgoing.task_id.data, task_id_.size());
            std::memcpy(cache_.bytes.data(), task.cache_id.data, 32);
            std::memcpy(hash_.bytes.data(), task.revision_hash.data, 32);
            if (!encodeTask({task_id_.data(), task_id_.size()}, task, task_.data(), task_.size(), task_size_) ||
                !encodeCacheHead(task.cache_id, head, head_.data(), head_.size(), head_size_)) return fail();
            if (finishing_) return read(12, {task_id_.data(), task_id_.size()}, Phase::Install);
            RequestId id;
            std::memcpy(id.bytes.data(), key_.data() + 32, 16);
            protocol::GetRequestView request;
            protocol::GetResponseView response;
            protocol::VerifiedRecordView verified;
            if (!protocol::decodeGetRequest(outgoing.request, id, request) || request.wanted_hash.size != 32 ||
                std::memcmp(request.cache_id.data, cache_.bytes.data(), 32) || std::memcmp(request.wanted_hash.data, hash_.bytes.data(), 32) ||
                !protocol::decodeGetResponse(outgoing.terminal_data, id, request.budget, response) || response.has_conflict ||
                protocol::verifyGeocache(response.signed_cache, crypto_, verification_, verification_capacity_, verified, &cache_, &hash_) != protocol::VerificationResult::Valid)
                return fail();
            ObjectRefView object;
            object.cache_id = {cache_.bytes.data(), 32};
            object.previous_hash = verified.record.previous_hash;
            object.revision = verified.record.revision;
            object.state = verified.record.state;
            object.created_at = verified.record.created_at;
            if (!encodeObjectRef({hash_.bytes.data(), 32}, object, object_.data(), object_.size(), object_size_)) return fail();
            if (head.current_hash.size)
            {
                // read() copies its key before destroying the context.
                return read(1, head.current_hash, Phase::CurrentObject);
            }
            if (old_present_ || !installableRecord(head, nullptr, verified)) return fail();
            return read(1, {hash_.bytes.data(), 32}, Phase::Object);
        }
        if (phase_ == Phase::Proof) return proofStep();
        auto& get = std::get<SdIndexGet>(io_);
        const auto status = get.step();
        if (status == IndexGetStep::Working) return result_;
        if (status != IndexGetStep::Ready && status != IndexGetStep::NotFound) return error(status);
        CacheHeadView head;
        TaskView task;
        if (!decodeCacheHead({cache_.bytes.data(), 32}, {head_.data(), head_size_}, head) ||
            !decodeTask({task_id_.data(), task_id_.size()}, {task_.data(), task_size_}, task)) return fail();
        if (phase_ == Phase::CurrentObject)
        {
            ObjectRefView current, incoming;
            protocol::VerifiedRecordView verified;
            if (status != IndexGetStep::Ready || !decodeObjectRef(head.current_hash, get.value(), current) ||
                !decodeObjectRef({hash_.bytes.data(), 32}, {object_.data(), object_size_}, incoming)) return fail();
            verified.id = cache_;
            verified.hash = hash_;
            verified.record.revision = incoming.revision;
            verified.record.state = incoming.state;
            verified.record.created_at = incoming.created_at;
            verified.record.previous_hash = incoming.previous_hash;
            if (!installableRecord(head, &current, verified)) return fail();
            return read(1, {hash_.bytes.data(), 32}, Phase::Object);
        }
        if (phase_ == Phase::Object)
        {
            ObjectRefView object;
            if (finishing_)
            {
                if (status != IndexGetStep::Ready || !decodeObjectRef({hash_.bytes.data(), 32}, get.value(), object) ||
                    std::memcmp(object.cache_id.data, cache_.bytes.data(), 32)) return fail();
                return finishInstall(object, head, task);
            }
            if (status == IndexGetStep::Ready)
            {
                object_present_ = true;
                ObjectRefView proposed;
                if (!decodeObjectRef({hash_.bytes.data(), 32}, get.value(), object) ||
                    !decodeObjectRef({hash_.bytes.data(), 32}, {object_.data(), object_size_}, proposed) ||
                    object.revision != proposed.revision || object.state != proposed.state || object.created_at != proposed.created_at ||
                    std::memcmp(object.cache_id.data, proposed.cache_id.data, 32) || object.previous_hash.size != proposed.previous_hash.size ||
                    (object.previous_hash.size && std::memcmp(object.previous_hash.data, proposed.previous_hash.data, object.previous_hash.size)) ||
                    !encodeObjectRef({hash_.bytes.data(), 32}, object, object_.data(), object_.size(), object_size_)) return fail();
            }
            return read(12, {task_id_.data(), task_id_.size()}, Phase::Install);
        }
        if (status == IndexGetStep::Ready)
        {
            InstallRecordView install;
            if (!decodeInstallRecord({task_id_.data(), task_id_.size()}, get.value(), install) ||
                install.generation != generation_ || std::memcmp(install.cache_id.data, cache_.bytes.data(), 32) ||
                std::memcmp(install.revision_hash.data, hash_.bytes.data(), 32) || std::memcmp(install.new_file_hash.data, file_hash_.data(), 32)) return fail();
            if (finishing_)
            {
                if (get.value().size > install_.size()) return fail();
                install_size_ = get.value().size;
                std::memcpy(install_.data(), get.value().data, install_size_);
                return read(1, {hash_.bytes.data(), 32}, Phase::Object);
            }
            if (!object_present_ || install.phase != InstallPhase::Prepared || install.old_file_hash.size != (old_present_ ? 32U : 0U) ||
                (old_present_ && std::memcmp(install.old_file_hash.data, old_hash_.data(), 32))) return fail();
            duplicate_ = true;
        }
        else if (finishing_) return fail();
        if (old_present_)
        {
            if (!io_.emplace<SdIndexScan>(volume_).begin(root_, 12, frame_, capacity_)) return fail();
            phase_ = Phase::Proof;
            return result_;
        }
        return prepareInstall();
    }

  private:
    enum class Phase : uint8_t
    {
        Context,
        CurrentObject,
        Object,
        Install,
        Proof,
        Commit
    };
    bool begin(const ::geocaching::storage::IndexRootView& root, unsigned copy, ::geocaching::ByteView request,
               uint64_t generation, const std::array<uint8_t, 32>& file_hash,
               uint8_t* frame, size_t capacity, uint8_t* verification, size_t verification_capacity,
               ::geocaching::storage::IndexRootBytes& candidate)
    {
        using ::geocaching::ByteView;
        if (result_ != IndexedCommitStep::Idle || copy > 1 || !request.data || request.size != 48 || !generation ||
            !frame || capacity < 24 || !::geocaching::storage::validIndexRoot(root)) return false;
        const ByteView leases[] = {root.shards, {candidate.data(), candidate.size()}, {frame, capacity}, {verification, verification_capacity}};
        for (size_t i = 0; i < 4; ++i)
            for (size_t j = 0; j < i; ++j)
            {
                const auto a = reinterpret_cast<uintptr_t>(leases[i].data), b = reinterpret_cast<uintptr_t>(leases[j].data);
                if (leases[i].size && leases[j].size && (a <= b ? b - a < leases[i].size : a - b < leases[j].size)) return false;
            }
        root_ = root;
        copy_ = copy;
        std::memcpy(key_.data(), request.data, key_.size());
        generation_ = generation;
        file_hash_ = file_hash;
        frame_ = frame;
        capacity_ = capacity;
        verification_ = verification;
        verification_capacity_ = verification_capacity;
        candidate_ = &candidate;
        if (!io_.emplace<SdIndexedDownloadContext>(volume_).begin(root_, {key_.data(), key_.size()}, generation, frame, capacity)) return false;
        result_ = IndexedCommitStep::Working;
        return true;
    }
    IndexedCommitStep read(uint8_t table, ::geocaching::ByteView key, Phase phase)
    {
        std::array<uint8_t, 32> copied{};
        if (key.size > copied.size()) return fail();
        const auto size = key.size;
        std::memcpy(copied.data(), key.data, size);
        if (!io_.emplace<SdIndexGet>(volume_).begin(root_, table, {copied.data(), size}, frame_, capacity_)) return fail();
        phase_ = phase;
        return result_;
    }
    IndexedCommitStep proofStep()
    {
        using namespace ::geocaching::storage;
        auto& scan = std::get<SdIndexScan>(io_);
        const auto status = scan.step();
        if (status == IndexScanStep::Working) return result_;
        if (status == IndexScanStep::End)
        {
            if (!proof_generation_ || proof_hash_ != old_hash_) return fail();
            return prepareInstall();
        }
        if (status != IndexScanStep::Item) return error(status);
        CacheHeadView head;
        MutationView row;
        InstallRecordView install;
        if (!decodeCacheHead({cache_.bytes.data(), 32}, {head_.data(), head_size_}, head) ||
            !scan.item(row) || !decodeInstallRecord(row.key, row.value, install)) return fail();
        if (install.phase == InstallPhase::Installed && install.generation <= generation_ &&
            install.generation > proof_generation_ && head.current_hash.size == 32 &&
            !std::memcmp(install.cache_id.data, cache_.bytes.data(), 32) && !std::memcmp(install.revision_hash.data, head.current_hash.data, 32))
        {
            proof_generation_ = install.generation;
            std::memcpy(proof_hash_.data(), install.new_file_hash.data, 32);
        }
        if (!scan.advance()) return fail();
        return result_;
    }
    IndexedCommitStep prepareInstall()
    {
        using namespace ::geocaching::storage;
        if (duplicate_) return result_ = IndexedCommitStep::Verified;
        InstallRecordView install{{cache_.bytes.data(), 32}, {hash_.bytes.data(), 32}, {file_hash_.data(), 32}, {old_present_ ? old_hash_.data() : nullptr, old_present_ ? 32U : 0U}, generation_, InstallPhase::Prepared};
        if (!encodeInstallRecord({task_id_.data(), task_id_.size()}, install, install_.data(), install_.size(), install_size_)) return fail();
        mutations_[0] = {1, {hash_.bytes.data(), 32}, {object_.data(), object_size_}, false};
        mutations_[1] = {12, {task_id_.data(), task_id_.size()}, {install_.data(), install_size_}, false};
        return commit(2);
    }
    IndexedCommitStep finishInstall(const ::geocaching::storage::ObjectRefView& object,
                                    ::geocaching::storage::CacheHeadView head, ::geocaching::storage::TaskView task)
    {
        using namespace ::geocaching::storage;
        InstallRecordView install;
        if (!decodeInstallRecord({task_id_.data(), task_id_.size()}, {install_.data(), install_size_}, install)) return fail();
        if (install.phase == InstallPhase::Installed)
        {
            if (task.state != 3 || head.current_hash.size != 32 || std::memcmp(head.current_hash.data, hash_.bytes.data(), 32)) return fail();
            duplicate_ = true;
            return result_ = IndexedCommitStep::Verified;
        }
        if (install.phase != InstallPhase::Prepared || !active_ || object.revision < head.highest_seen_revision || head.conflict_state == 2) return fail();
        head.current_hash = {hash_.bytes.data(), 32};
        head.highest_seen_revision = object.revision;
        install.phase = InstallPhase::Installed;
        task.state = 3;
        // Encoders may read the old views while writing, so encode into frame
        // temporarily, then copy small metadata before beginning the commit.
        size_t size = 0;
        if (!encodeCacheHead({cache_.bytes.data(), 32}, head, frame_, capacity_, size) || size > head_.size()) return fail();
        head_size_ = size;
        std::memcpy(head_.data(), frame_, size);
        if (!encodeInstallRecord({task_id_.data(), task_id_.size()}, install, frame_, capacity_, size) || size > install_.size()) return fail();
        install_size_ = size;
        std::memcpy(install_.data(), frame_, size);
        if (!encodeTask({task_id_.data(), task_id_.size()}, task, frame_, capacity_, size) || size > task_.size()) return fail();
        task_size_ = size;
        std::memcpy(task_.data(), frame_, size);
        mutations_[0] = {2, {cache_.bytes.data(), 32}, {head_.data(), head_size_}, false};
        mutations_[1] = {12, {task_id_.data(), task_id_.size()}, {install_.data(), install_size_}, false};
        mutations_[2] = {10, {task_id_.data(), task_id_.size()}, {task_.data(), task_size_}, false};
        return commit(3);
    }
    IndexedCommitStep commit(size_t count)
    {
        if (!io_.emplace<SdIndexedCommit>(volume_).begin(root_, copy_, mutations_.data(), count, frame_, capacity_, *candidate_)) return fail();
        phase_ = Phase::Commit;
        return result_;
    }
    template <class Status>
    IndexedCommitStep error(Status status)
    {
        return fail(status == Status::IoError ? IndexedCommitStep::IoError : status == Status::VolumeChanged ? IndexedCommitStep::VolumeChanged
                                                                                                             : IndexedCommitStep::Invalid);
    }
    IndexedCommitStep fail(IndexedCommitStep status = IndexedCommitStep::Invalid)
    {
        io_.emplace<std::monostate>();
        return result_ = status;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::protocol::RecordCrypto& crypto_;
    ::geocaching::storage::IndexRootView root_;
    ::geocaching::storage::IndexRootBytes* candidate_ = nullptr;
    ::geocaching::GeocacheId cache_;
    ::geocaching::RevisionHash hash_;
    std::array<uint8_t, 48> key_{};
    std::array<uint8_t, 16> task_id_{};
    std::array<uint8_t, 32> file_hash_{}, old_hash_{}, proof_hash_{};
    std::array<uint8_t, 256> task_{};
    std::array<uint8_t, 64> head_{};
    std::array<uint8_t, 160> object_{}, install_{};
    std::array<::geocaching::storage::MutationView, 3> mutations_{};
    uint8_t *frame_ = nullptr, *verification_ = nullptr;
    size_t capacity_ = 0, verification_capacity_ = 0, task_size_ = 0, head_size_ = 0, object_size_ = 0, install_size_ = 0;
    uint64_t generation_ = 0, proof_generation_ = 0;
    unsigned copy_ = 0;
    bool finishing_ = false, active_ = false, old_present_ = false, duplicate_ = false, object_present_ = false;
    std::variant<std::monostate, SdIndexedDownloadContext, SdIndexGet, SdIndexScan, SdIndexedCommit> io_;
    Phase phase_ = Phase::Context;
    IndexedCommitStep result_ = IndexedCommitStep::Idle;
};
static_assert(sizeof(SdIndexedInstall) <= 2560, "Installation owns small metadata, not a logical ledger or SignedCache copy");
} // namespace platform::esp::arduino_common::geocaching
