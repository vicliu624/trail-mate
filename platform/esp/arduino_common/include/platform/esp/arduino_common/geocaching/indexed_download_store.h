#pragma once
#include "platform/esp/arduino_common/geocaching/download_store.h"
#include "platform/esp/arduino_common/geocaching/index_workspace_owner.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_download_recovery.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_download_reply.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_install.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_new_task.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_saved_cache.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_saved_page.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_stop_task.h"
#include "platform/memory/psram_ptr.h"

namespace platform::esp::arduino_common::geocaching
{
// Session-owned, disjoint buffer leases. Operations are allocated on demand.
// The read lease spans staging and installation; only this holder may mutate
// the root in that interval. No payload views survive releaseRead().
class IndexedDownloadStore final : public DownloadStore
{
  public:
    IndexedDownloadStore(const ::geocaching::storage::VolumeInstance& volume, ::geocaching::storage::IndexRootView& root, unsigned& copy,
                         ::geocaching::storage::IndexRootBytes& first, ::geocaching::storage::IndexRootBytes& second,
                         IndexWorkspaceOwner& owner, ::geocaching::storage::QueuedRequestWorkspace& workspace,
                         uint8_t* frame, size_t capacity, uint8_t* response, size_t response_capacity,
                         uint8_t* verification, size_t verification_capacity, ::geocaching::protocol::RecordCrypto& crypto)
        : volume_(volume), root_(root), copy_(copy), roots_{&first, &second}, owner_(owner), workspace_(workspace),
          frame_(frame), capacity_(capacity), response_(response), response_capacity_(response_capacity),
          verification_(verification), verification_capacity_(verification_capacity), crypto_(crypto)
    {
        valid_ = copy < 2 && ::geocaching::storage::validIndexRoot(root) && root.shards.data == roots_[copy]->data() + 48 &&
                 frame && capacity >= 24 && response_capacity && verification_capacity && workspace.outgoing;
        // Payload buffers may be absent until the owner's prepare callback.
        // Every write acquisition checks them before exposing the workspace.
        const ::geocaching::ByteView leases[] = {{first.data(), first.size()}, {second.data(), second.size()}, {frame, capacity}, {response, response ? response_capacity : 0}, {verification, verification ? verification_capacity : 0}, {workspace.outgoing, workspace.outgoing_capacity}, {workspace.task.data(), workspace.task.size()}};
        for (size_t i = 0; i < 7; ++i)
            for (size_t j = 0; j < i; ++j)
            {
                const auto a = reinterpret_cast<uintptr_t>(leases[i].data), b = reinterpret_cast<uintptr_t>(leases[j].data);
                if (leases[i].size && leases[j].size && (a <= b ? b - a < leases[i].size : a - b < leases[j].size)) valid_ = false;
            }
    }
    ~IndexedDownloadStore() override
    {
        io_.reset();
        recovery_.reset();
        saved_.reset();
        page_.reset();
        owner_.release(this);
    }
    bool needsRecovery() const override { return blocked_; }
    bool hasSavedPages() const override { return true; }
    bool metadataRead() const { return metadata_read_; }
    DownloadRecoveryRead readSavedPage(size_t offset, size_t limit, std::array<::geocaching::storage::SavedCacheEntry, 4>& rows, size_t& total) override
    {
        if (!valid_ || !limit || limit > rows.size()) return DownloadRecoveryRead::Invalid;
        if (blocked_) return DownloadRecoveryRead::Unavailable;
        if (!page_)
        {
            if (phase_ != Phase::None || saved_ || recovery_ || cached_ || owner_.heldBy(this)) return DownloadRecoveryRead::Busy;
            metadata_read_ = true;
            const bool acquired = owner_.acquire(this);
            metadata_read_ = false;
            if (!acquired) return DownloadRecoveryRead::Busy;
            page_.reset(::platform::memory::createPsram<SdIndexedSavedPage>(volume_, crypto_));
            if (!page_)
            {
                owner_.release(this);
                return DownloadRecoveryRead::Unavailable;
            }
            revision_ = root_.revision;
            if (!page_->begin(root_, offset, limit, rows, total, frame_, capacity_, verification_, verification_capacity_))
            {
                releaseRead();
                return DownloadRecoveryRead::Invalid;
            }
            return DownloadRecoveryRead::Pending;
        }
        if (!page_->matches(offset, limit, rows, total)) return DownloadRecoveryRead::Busy;
        if (!owner_.heldBy(this) || revision_ != root_.revision)
        {
            fail();
            return DownloadRecoveryRead::Invalid;
        }
        const auto status = page_->step();
        if (status == IndexScanStep::Working) return DownloadRecoveryRead::Pending;
        projection_upgrade_required_ = page_->missingProjection(cache_);
        const auto result = projection_upgrade_required_ ? DownloadRecoveryRead::NeedsUpgrade : page_->unavailable()                     ? DownloadRecoveryRead::Unavailable
                                                                                            : status == IndexScanStep::End               ? DownloadRecoveryRead::Ready
                                                                                            : status == IndexScanStep::WorkspaceTooSmall ? DownloadRecoveryRead::WorkspaceTooSmall
                                                                                            : status == IndexScanStep::IoError           ? DownloadRecoveryRead::IoError
                                                                                            : status == IndexScanStep::VolumeChanged     ? DownloadRecoveryRead::VolumeChanged
                                                                                                                                         : DownloadRecoveryRead::Invalid;
        releaseRead();
        // Retry only the failed page; transient I/O must not invalidate the
        // already opened index or start a full storage audit.
        if (result == DownloadRecoveryRead::IoError && page_read_retries_ < 2)
        {
            ++page_read_retries_;
            return DownloadRecoveryRead::Busy;
        }
        page_read_retries_ = 0;
        blocked_ = result == DownloadRecoveryRead::Invalid || result == DownloadRecoveryRead::IoError || result == DownloadRecoveryRead::VolumeChanged;
        return result;
    }
    bool missingSavedProjection(std::array<uint8_t, 32>& id) const
    {
        if (!projection_upgrade_required_) return false;
        id = cache_;
        return true;
    }
    // Caller must admit writes through storage recovery before upgrading a
    // legacy row. History is consulted here once, never inside a list page.
    JournalWriteResult upgradeSavedProjection(const std::array<uint8_t, 32>& id)
    {
        using namespace ::geocaching;
        using namespace ::geocaching::storage;
        if (blocked_) return JournalWriteResult::Unavailable;
        if (phase_ == Phase::None)
        {
            if (!acquire()) return JournalWriteResult::Busy;
            cache_ = id;
            saved_.reset(::platform::memory::createPsram<SdIndexedSavedCache>(volume_));
            if (!saved_)
            {
                releaseRead();
                return JournalWriteResult::Unavailable;
            }
            if (!io_->emplace<SdIndexGet>(volume_, &saved_->readSession()).begin(root_, 2, {cache_.data(), 32}, frame_, capacity_)) return fail();
            phase_ = Phase::UpgradeHead;
            return JournalWriteResult::InProgress;
        }
        if (cache_ != id || !owner_.heldBy(this) || root_.revision != revision_) return fail();
        if (phase_ == Phase::Finish)
        {
            const auto result = stepCommit();
            if (result == JournalWriteResult::Verified) projection_upgrade_required_ = false;
            return result;
        }
        if (phase_ == Phase::UpgradeRead)
        {
            const auto status = saved_->step();
            if (status == IndexScanStep::Working) return JournalWriteResult::InProgress;
            if (status != IndexScanStep::Item) return error(status);
            ByteView request, task, signed_cache;
            const auto* record = saved_->installation(request, task, signed_cache);
            if (!record || record->id != cache_ || record->hash != proof_hash_) return fail();
            ::platform::memory::PsramPtr<protocol::VerifiedRecordView> verified(::platform::memory::createPsram<protocol::VerifiedRecordView>());
            if (!verified)
            {
                releaseRead();
                return JournalWriteResult::Unavailable;
            }
            GeocacheId expected_id{cache_};
            RevisionHash expected_hash{proof_hash_};
            if (protocol::verifyGeocache(signed_cache, crypto_, verification_, verification_capacity_, *verified, &expected_id, &expected_hash) != protocol::VerificationResult::Valid) return fail();
            ObjectRefView object;
            if (!decodeObjectRef({proof_hash_.data(), 32}, outgoing_, object) ||
                object.revision != verified->record.revision || object.state != verified->record.state || object.created_at != verified->record.created_at ||
                std::memcmp(object.cache_id.data, cache_.data(), 32) || object.previous_hash.size != verified->record.previous_hash.size ||
                (object.previous_hash.size && std::memcmp(object.previous_hash.data, verified->record.previous_hash.data, object.previous_hash.size))) return fail();
            std::memcpy(key_.data(), request.data, 48);
            std::memcpy(task_.data(), task.data, 16);
            object.cache_id = {cache_.data(), 32};
            object.previous_hash = verified->record.previous_hash;
            object.name = verified->record.name;
            object.latitude_e7 = verified->record.latitude_e7;
            object.longitude_e7 = verified->record.longitude_e7;
            object.saved_request = {key_.data(), 48};
            object.saved_task = {task_.data(), 16};
            size_t size = 0;
            if (!encodeObjectRef({proof_hash_.data(), 32}, object, response_, response_capacity_, size)) return fail();
            saved_.reset();
            upgrade_mutations_[0] = {1, {proof_hash_.data(), 32}, {response_, size}, false};
            upgrade_mutations_[1] = {2, {cache_.data(), 32}, {install_.data(), install_size_}, false};
            if (!io_->emplace<SdIndexedCommit>(volume_).begin(root_, copy_, upgrade_mutations_.data(), 2, frame_, capacity_, *roots_[1 - copy_])) return fail();
            phase_ = Phase::Finish;
            return JournalWriteResult::InProgress;
        }
        if (phase_ != Phase::UpgradeHead && phase_ != Phase::UpgradeObject) return fail();
        auto& get = std::get<SdIndexGet>(*io_);
        const auto status = get.step();
        if (status == IndexGetStep::Working) return JournalWriteResult::InProgress;
        if (status != IndexGetStep::Ready) return error(status);
        if (phase_ == Phase::UpgradeHead)
        {
            CacheHeadView head;
            if (!decodeCacheHead({cache_.data(), 32}, get.value(), head) || get.value().size > install_.size()) return fail();
            install_size_ = get.value().size;
            std::memcpy(install_.data(), get.value().data, install_size_);
            if (!head.current_hash.size) return beginHeadUpgrade();
            std::memcpy(proof_hash_.data(), head.current_hash.data, 32);
            if (!io_->emplace<SdIndexGet>(volume_, &saved_->readSession()).begin(root_, 1, {proof_hash_.data(), 32}, frame_, capacity_)) return fail();
            phase_ = Phase::UpgradeObject;
            return JournalWriteResult::InProgress;
        }
        ObjectRefView object;
        if (!decodeObjectRef({proof_hash_.data(), 32}, get.value(), object) || std::memcmp(object.cache_id.data, cache_.data(), 32)) return fail();
        if (object.saved_request.size) return beginHeadUpgrade(get.value());
        if (get.value().size > response_capacity_) return fail();
        std::memcpy(response_, get.value().data, get.value().size);
        outgoing_ = {response_, get.value().size}; // Phase-local owned legacy object.
        io_->emplace<std::monostate>();
        if (!saved_->beginFromHead(root_, {cache_.data(), 32}, {install_.data(), install_size_}, frame_, capacity_, false)) return fail();
        phase_ = Phase::UpgradeRead;
        return JournalWriteResult::InProgress;
    }
    // Called by the workspace owner's prepare callback, before acquiring a
    // fresh lease. No borrowed view or operation may outlive its prior lease.
    void bindWorkspace(uint8_t* frame, uint8_t* response, uint8_t* verification)
    {
        frame_ = frame;
        response_ = response;
        verification_ = verification;
    }
    DownloadRecoveryRead readSavedCache(::geocaching::ByteView key, bool exact, ::geocaching::protocol::RecordCrypto& crypto,
                                        ::geocaching::storage::SavedCacheRecord& out) override
    {
        out = {};
        if (!valid_ || &crypto != &crypto_ || (key.size && (!key.data || key.size != 32)) || (exact && key.size != 32)) return DownloadRecoveryRead::Invalid;
        if (blocked_) return DownloadRecoveryRead::Unavailable;
        if (saved_)
        {
            if (!owner_.heldBy(this) || revision_ != root_.revision)
            {
                fail();
                return DownloadRecoveryRead::Invalid;
            }
            if (generation_ != (exact ? 2u : unsigned(key.size != 0)) || (key.size && std::memcmp(key.data, key_.data(), 32))) return DownloadRecoveryRead::Busy;
        }
        else
        {
            if (phase_ != Phase::None || recovery_ || cached_ || owner_.heldBy(this) || !owner_.acquire(this)) return DownloadRecoveryRead::Busy;
            saved_.reset(::platform::memory::createPsram<SdIndexedSavedCache>(volume_));
            if (!saved_)
            {
                owner_.release(this);
                return DownloadRecoveryRead::Unavailable;
            }
            revision_ = root_.revision;
            generation_ = exact ? 2u : unsigned(key.size != 0);
            if (key.size) std::memcpy(key_.data(), key.data, 32);
            if (!saved_->begin(root_, key, exact, frame_, capacity_))
            {
                releaseRead();
                return DownloadRecoveryRead::Invalid;
            }
            return DownloadRecoveryRead::Pending;
        }
        const auto status = saved_->step();
        if (status == IndexScanStep::Working) return DownloadRecoveryRead::Pending;
        auto result = DownloadRecoveryRead::Invalid;
        ::geocaching::ByteView signed_cache;
        if (status == IndexScanStep::Item && saved_->result(out, signed_cache))
        {
            const auto verified = ::geocaching::storage::verifySavedCache(signed_cache, crypto_, verification_, verification_capacity_, out);
            result = verified == ::geocaching::protocol::VerificationResult::Valid               ? DownloadRecoveryRead::Ready
                     : verified == ::geocaching::protocol::VerificationResult::WorkspaceTooSmall ? DownloadRecoveryRead::WorkspaceTooSmall
                     : verified == ::geocaching::protocol::VerificationResult::CryptoUnavailable ? DownloadRecoveryRead::Unavailable
                                                                                                 : DownloadRecoveryRead::Invalid;
        }
        else if (status == IndexScanStep::End) result = DownloadRecoveryRead::End;
        else if (status == IndexScanStep::WorkspaceTooSmall) result = DownloadRecoveryRead::WorkspaceTooSmall;
        else if (status == IndexScanStep::IoError) result = DownloadRecoveryRead::IoError;
        else if (status == IndexScanStep::VolumeChanged) result = DownloadRecoveryRead::VolumeChanged;
        blocked_ = result == DownloadRecoveryRead::Invalid || result == DownloadRecoveryRead::IoError || result == DownloadRecoveryRead::VolumeChanged;
        if (result != DownloadRecoveryRead::Ready) out = {};
        releaseRead();
        return result;
    }
    DownloadRecoveryRead readNextGeneration(const ::geocaching::GeocacheId& id, uint64_t& generation) override
    {
        generation = 0;
        if (!valid_) return DownloadRecoveryRead::Invalid;
        if (blocked_) return DownloadRecoveryRead::Unavailable;
        if ((owner_.heldBy(this) && revision_ != root_.revision) ||
            ((phase_ == Phase::Generation || phase_ == Phase::GenerationReady) && !owner_.heldBy(this)))
        {
            fail();
            return DownloadRecoveryRead::Invalid;
        }
        if (saved_ || page_ || recovery_ || cached_ || (phase_ != Phase::None && phase_ != Phase::Generation && phase_ != Phase::GenerationReady)) return DownloadRecoveryRead::Busy;
        if (phase_ == Phase::None)
        {
            if (owner_.holder()) return DownloadRecoveryRead::Busy;
            if (!acquire()) return DownloadRecoveryRead::Unavailable;
            cache_ = id.bytes;
            if (!io_->emplace<SdIndexGet>(volume_).begin(root_, 2, {cache_.data(), cache_.size()}, frame_, capacity_))
            {
                releaseRead();
                return DownloadRecoveryRead::Invalid;
            }
            phase_ = Phase::Generation;
            return DownloadRecoveryRead::Pending;
        }
        if (cache_ != id.bytes) return DownloadRecoveryRead::Busy;
        if (phase_ == Phase::GenerationReady)
        {
            generation = generation_;
            return DownloadRecoveryRead::Ready;
        }
        auto& get = std::get<SdIndexGet>(*io_);
        const auto status = get.step();
        if (status == IndexGetStep::Working) return DownloadRecoveryRead::Pending;
        if (status != IndexGetStep::Ready && status != IndexGetStep::NotFound)
        {
            const auto result = status == IndexGetStep::WorkspaceTooSmall ? DownloadRecoveryRead::WorkspaceTooSmall
                                : status == IndexGetStep::IoError         ? DownloadRecoveryRead::IoError
                                : status == IndexGetStep::VolumeChanged   ? DownloadRecoveryRead::VolumeChanged
                                                                          : DownloadRecoveryRead::Invalid;
            blocked_ = result != DownloadRecoveryRead::WorkspaceTooSmall;
            releaseRead();
            return result;
        }
        ::geocaching::storage::CacheHeadView head;
        if (status == IndexGetStep::Ready && !::geocaching::storage::decodeCacheHead({cache_.data(), cache_.size()}, get.value(), head))
        {
            fail();
            return DownloadRecoveryRead::Invalid;
        }
        generation = generation_ = head.install_generation == UINT64_MAX ? 0 : head.install_generation + 1;
        phase_ = Phase::GenerationReady;
        return DownloadRecoveryRead::Ready;
    }
    bool commitPending() const override { return phase_ >= Phase::Create; }
    void releaseRead() override
    {
        if (commitPending()) return;
        io_.reset();
        recovery_.reset();
        saved_.reset();
        page_.reset();
        phase_ = Phase::None;
        cached_ = active_ = completed_ = false;
        outgoing_ = {};
        owner_.release(this);
    }
    DownloadRecoveryRead readRecovery(::geocaching::ByteView after, ::geocaching::storage::DownloadRecoveryRequest& out) override
    {
        return readRecoverySelection(after, nullptr, out, nullptr);
    }
    DownloadRecoveryRead readWaitingDownload(const ::geocaching::Destination& local, ::geocaching::storage::DownloadRecoveryRequest& out,
                                             ::geocaching::protocol::SummaryView& preview) override
    {
        preview = {};
        return readRecoverySelection({}, &local, out, &preview);
    }

  private:
    DownloadRecoveryRead readRecoverySelection(::geocaching::ByteView after, const ::geocaching::Destination* local,
                                               ::geocaching::storage::DownloadRecoveryRequest& out, ::geocaching::protocol::SummaryView* preview)
    {
        out = {};
        if (!valid_ || (after.size && (!after.data || after.size != key_.size()))) return DownloadRecoveryRead::Invalid;
        if (blocked_) return DownloadRecoveryRead::Unavailable;
        if (recovery_)
        {
            if (!owner_.heldBy(this) || revision_ != root_.revision)
            {
                fail();
                return DownloadRecoveryRead::Invalid;
            }
            if (generation_ != (local ? 2u : unsigned(after.size != 0)) ||
                (local && std::memcmp(local->bytes.data(), key_.data(), 16)) ||
                (after.size && std::memcmp(after.data, key_.data(), key_.size()))) return DownloadRecoveryRead::Busy;
        }
        else
        {
            if (saved_ || page_ || phase_ != Phase::None || cached_ || owner_.heldBy(this) || !owner_.acquire(this)) return DownloadRecoveryRead::Busy;
            recovery_.reset(::platform::memory::createPsram<SdIndexedDownloadRecovery>(volume_));
            if (!recovery_)
            {
                owner_.release(this);
                return DownloadRecoveryRead::Unavailable;
            }
            revision_ = root_.revision;
            generation_ = local ? 2u : unsigned(after.size != 0);
            if (local) std::memcpy(key_.data(), local->bytes.data(), 16);
            if (after.size) std::memcpy(key_.data(), after.data, key_.size());
            if (!(local ? recovery_->beginWaiting(root_, *local, frame_, capacity_) : recovery_->begin(root_, after, frame_, capacity_)))
            {
                releaseRead();
                return DownloadRecoveryRead::Invalid;
            }
            return DownloadRecoveryRead::Pending;
        }
        const auto status = recovery_->step();
        if (status == IndexScanStep::Working) return DownloadRecoveryRead::Pending;
        DownloadRecoveryRead result = DownloadRecoveryRead::Invalid;
        if (status == IndexScanStep::Item && recovery_->selected(out) && (!preview || recovery_->preview(*preview))) result = DownloadRecoveryRead::Ready;
        else if (status == IndexScanStep::End) result = DownloadRecoveryRead::End;
        else if (status == IndexScanStep::WorkspaceTooSmall) result = DownloadRecoveryRead::WorkspaceTooSmall;
        else if (status == IndexScanStep::IoError) result = DownloadRecoveryRead::IoError;
        else if (status == IndexScanStep::VolumeChanged) result = DownloadRecoveryRead::VolumeChanged;
        blocked_ = result == DownloadRecoveryRead::Invalid || result == DownloadRecoveryRead::IoError || result == DownloadRecoveryRead::VolumeChanged;
        if (preview && result == DownloadRecoveryRead::Ready) return result;
        releaseRead();
        return result;
    }

  public:
    JournalWriteResult readDownload(::geocaching::ByteView key, uint64_t generation) override
    {
        using namespace ::geocaching::storage;
        if (!valid_ || !key.data || key.size != key_.size() || !generation) return JournalWriteResult::Invalid;
        if (blocked_) return JournalWriteResult::Unavailable;
        if (owner_.heldBy(this) && revision_ != root_.revision) return fail();
        if (commitPending() || saved_ || page_ || recovery_ || phase_ == Phase::Generation || phase_ == Phase::GenerationReady) return JournalWriteResult::Busy;
        if (cached_ && matches(key, generation)) return JournalWriteResult::Verified;
        if (phase_ == Phase::None)
        {
            if (!acquire()) return JournalWriteResult::Busy;
            std::memcpy(key_.data(), key.data, key_.size());
            generation_ = generation;
            revision_ = root_.revision;
            proof_generation_ = install_size_ = 0;
            active_ = cached_ = completed_ = false;
            if (!io_->emplace<SdIndexedDownloadContext>(volume_).begin(root_, {key_.data(), key_.size()}, generation, frame_, capacity_)) return fail();
            phase_ = Phase::Context;
            return JournalWriteResult::InProgress;
        }
        if (!matches(key, generation) || !io_) return fail();
        if (phase_ == Phase::Context)
        {
            auto& context = std::get<SdIndexedDownloadContext>(*io_);
            const auto status = context.step();
            if (status == IndexGetStep::Working) return JournalWriteResult::InProgress;
            if (status != IndexGetStep::Ready) return error(status);
            OutgoingView outgoing;
            TaskView task;
            CacheHeadView head;
            if (!context.view(outgoing, task, head)) return fail();
            active_ = context.intentActive();
            completed_ = outgoing.state == 4 && task.state == 3 && head.current_hash.size == 32 &&
                         !std::memcmp(head.current_hash.data, task.revision_hash.data, 32);
            if (!active_ && !completed_) return fail();
            std::memcpy(task_.data(), outgoing.task_id.data, task_.size());
            std::memcpy(cache_.data(), task.cache_id.data, cache_.size());
            has_current_ = head.current_hash.size == 32;
            removed_current_ = !has_current_ && head.highest_seen_revision != 0;
            if (has_current_) std::memcpy(old_revision_.bytes.data(), head.current_hash.data, 32);
            if (!io_->emplace<SdIndexGet>(volume_).begin(root_, 12, {task_.data(), task_.size()}, frame_, capacity_)) return fail();
            phase_ = Phase::Install;
            return JournalWriteResult::InProgress;
        }
        if (phase_ == Phase::Proof)
        {
            auto& scan = std::get<SdIndexScan>(*io_);
            const auto status = scan.step();
            if (status == IndexScanStep::Working) return JournalWriteResult::InProgress;
            if (status == IndexScanStep::End) return reload();
            if (status != IndexScanStep::Item) return error(status);
            MutationView row;
            InstallRecordView install;
            if (!scan.item(row) || !decodeInstallRecord(row.key, row.value, install)) return fail();
            const bool matching = completed_         ? install.generation < generation_ && !std::memcmp(install.new_file_hash.data, recovery_old_hash_.data(), 32)
                                  : removed_current_ ? install.generation < generation_
                                                     : !std::memcmp(install.revision_hash.data, old_revision_.bytes.data(), 32);
            if (install.phase == InstallPhase::Installed && install.generation <= generation_ && install.generation > proof_generation_ &&
                !std::memcmp(install.cache_id.data, cache_.data(), 32) && matching)
            {
                proof_generation_ = install.generation;
                std::memcpy(proof_hash_.data(), install.new_file_hash.data, 32);
                if (completed_ || removed_current_) std::memcpy(old_revision_.bytes.data(), install.revision_hash.data, 32);
            }
            if (!scan.advance()) return fail();
            return JournalWriteResult::InProgress;
        }
        auto& get = std::get<SdIndexGet>(*io_);
        const auto status = get.step();
        if (status == IndexGetStep::Working) return JournalWriteResult::InProgress;
        if (phase_ == Phase::Install)
        {
            if (status != IndexGetStep::NotFound && status != IndexGetStep::Ready) return error(status);
            if (status == IndexGetStep::Ready)
            {
                InstallRecordView install;
                if (get.value().size > install_.size() || !decodeInstallRecord({task_.data(), task_.size()}, get.value(), install)) return fail();
                install_size_ = get.value().size;
                std::memcpy(install_.data(), get.value().data, install_size_);
                if (completed_)
                {
                    if (install.phase != InstallPhase::Installed || install.generation != generation_ ||
                        std::memcmp(install.cache_id.data, cache_.data(), 32) || std::memcmp(install.revision_hash.data, old_revision_.bytes.data(), 32)) return fail();
                    has_current_ = install.old_file_hash.size == 32;
                    if (has_current_) std::memcpy(recovery_old_hash_.data(), install.old_file_hash.data, 32);
                }
            }
            else if (completed_) return fail();
            // A local deletion retains the GPX. Locate its last committed
            // installation proof before allowing a later download to replace it.
            if (!has_current_ && (!removed_current_ || completed_)) return reload();
            if (!io_->emplace<SdIndexScan>(volume_).begin(root_, 12, frame_, capacity_)) return fail();
            phase_ = Phase::Proof;
            return JournalWriteResult::InProgress;
        }
        if (status != IndexGetStep::Ready) return error(status);
        outgoing_ = get.value();
        cached_ = true;
        phase_ = Phase::None;
        return JournalWriteResult::Verified;
    }
    bool readOutgoing(::geocaching::ByteView key, ::geocaching::storage::OutgoingView& out) const override
    {
        return cached_ && matches(key, generation_) && ::geocaching::storage::decodeOutgoing(key, outgoing_, out);
    }
    bool readInstall(const std::array<uint8_t, 16>& task, ::geocaching::ByteView& out) const override
    {
        out = {};
        if (!cached_ || !owner_.heldBy(this) || root_.revision != revision_ || task != task_ || !install_size_) return false;
        out = {install_.data(), install_size_};
        return true;
    }
    bool verifyRecord(::geocaching::ByteView bytes, ::geocaching::protocol::RecordCrypto& crypto,
                      const ::geocaching::GeocacheId& id, const ::geocaching::RevisionHash& hash,
                      ::geocaching::protocol::VerifiedRecordView& out) override
    {
        return cached_ && owner_.heldBy(this) && root_.revision == revision_ && &crypto == &crypto_ &&
               ::geocaching::protocol::verifyGeocache(bytes, crypto_, verification_, verification_capacity_, out, &id, &hash) == ::geocaching::protocol::VerificationResult::Valid;
    }
    bool downloadIntentActive(::geocaching::ByteView key, uint64_t generation) const override { return active_ && matches(key, generation); }
    bool downloadCompleted(::geocaching::ByteView key, uint64_t generation) const override { return completed_ && matches(key, generation); }
    bool installedFileProof(::geocaching::ByteView key, uint64_t generation, std::array<uint8_t, 32>& hash,
                            ::geocaching::RevisionHash& revision) const override
    {
        if (!matches(key, generation) || !proof_generation_) return false;
        hash = proof_hash_;
        revision = old_revision_;
        return true;
    }
    JournalWriteResult persistNewTask(const ::geocaching::Destination& local, const ::geocaching::Destination& remote,
                                      const ::geocaching::RequestId& request, const std::array<uint8_t, 16>& task, uint8_t kind,
                                      ::geocaching::ByteView bytes, const ::geocaching::storage::StoredTime& time,
                                      const ::geocaching::storage::RequestTaskTarget& target) override
    {
        if (!valid_) return JournalWriteResult::Invalid;
        if (blocked_) return JournalWriteResult::Unavailable;
        if (kind != 2 || !bytes.data || bytes.size > response_capacity_) return JournalWriteResult::Invalid;
        if (owner_.heldBy(this) && revision_ != root_.revision) return fail();
        if (phase_ == Phase::GenerationReady && (!generation_ || target.install_generation != generation_ || !target.cache_id.data ||
                                                 target.cache_id.size != cache_.size() || std::memcmp(target.cache_id.data, cache_.data(), cache_.size()))) return JournalWriteResult::StateRejected;
        if (commitPending() || !acquire(true)) return JournalWriteResult::Busy;
        cached_ = active_ = completed_ = false;
        makeKey(local, remote, request);
        task_ = task;
        generation_ = target.install_generation;
        std::memmove(response_, bytes.data, bytes.size);
        if (!io_->emplace<SdIndexedNewTask>(volume_).begin(root_, copy_, local, remote, request, task, kind, {response_, bytes.size},
                                                           time, target, workspace_, frame_, capacity_, *roots_[1 - copy_])) return fail();
        phase_ = Phase::Create;
        return JournalWriteResult::InProgress;
    }
    JournalWriteResult recordDownloadResponse(const ::geocaching::Destination& local, const ::geocaching::Destination& remote,
                                              const ::geocaching::RequestId& request, uint64_t generation, ::geocaching::ByteView response,
                                              ::geocaching::protocol::RecordCrypto& crypto) override
    {
        if (!valid_) return JournalWriteResult::Invalid;
        if (blocked_) return JournalWriteResult::Unavailable;
        if (&crypto != &crypto_ || !response.data || response.size > response_capacity_) return JournalWriteResult::Invalid;
        if (commitPending() || !acquire()) return JournalWriteResult::Busy;
        cached_ = active_ = completed_ = false;
        makeKey(local, remote, request);
        generation_ = generation;
        std::memmove(response_, response.data, response.size);
        if (!io_->emplace<SdIndexedDownloadReply>(volume_, crypto_).begin(root_, copy_, {key_.data(), key_.size()}, generation, {response_, response.size}, workspace_, frame_, capacity_, verification_, verification_capacity_, *roots_[1 - copy_])) return fail();
        phase_ = Phase::Reply;
        return JournalWriteResult::InProgress;
    }
    JournalWriteResult stopTask(const std::array<uint8_t, 16>& task) override
    {
        if (!valid_) return JournalWriteResult::Invalid;
        if (blocked_) return JournalWriteResult::Unavailable;
        if (commitPending() || !acquire()) return JournalWriteResult::Busy;
        cached_ = false;
        if (!io_->emplace<SdIndexedStopTask>(volume_).begin(root_, copy_, {task.data(), task.size()}, false, frame_, capacity_, *roots_[1 - copy_])) return fail();
        phase_ = Phase::Stop;
        return JournalWriteResult::InProgress;
    }
    JournalWriteResult prepareDownloadInstall(::geocaching::ByteView request, const std::array<uint8_t, 16>& task,
                                              uint64_t generation, const std::array<uint8_t, 32>& hash, ::geocaching::ByteView old_hash,
                                              ::geocaching::protocol::RecordCrypto& crypto) override
    {
        if (blocked_) return JournalWriteResult::Unavailable;
        if (&crypto != &crypto_ || !active_ || !matches(request, generation) || task != task_) return JournalWriteResult::StateRejected;
        if (commitPending()) return JournalWriteResult::Busy;
        cached_ = false;
        if (!io_->emplace<SdIndexedInstall>(volume_, crypto_).beginPrepare(root_, copy_, request, generation, hash, old_hash, frame_, capacity_, verification_, verification_capacity_, *roots_[1 - copy_])) return fail();
        phase_ = Phase::Prepare;
        return JournalWriteResult::InProgress;
    }
    JournalWriteResult finishDownloadInstall(::geocaching::ByteView request, const std::array<uint8_t, 16>& task,
                                             uint64_t generation, const std::array<uint8_t, 32>& hash) override
    {
        if (blocked_) return JournalWriteResult::Unavailable;
        if (!active_ || !matches(request, generation) || task != task_) return JournalWriteResult::StateRejected;
        if (commitPending()) return JournalWriteResult::Busy;
        cached_ = false;
        if (!io_->emplace<SdIndexedInstall>(volume_, crypto_).beginFinish(root_, copy_, request, generation, hash, frame_, capacity_, *roots_[1 - copy_])) return fail();
        phase_ = Phase::Finish;
        return JournalWriteResult::InProgress;
    }
    JournalWriteResult finalizeDownloadInstall(::geocaching::ByteView request, uint64_t generation,
                                               const std::array<uint8_t, 32>& hash) override
    {
        if (blocked_) return JournalWriteResult::Unavailable;
        if (commitPending() || !acquire()) return JournalWriteResult::Busy;
        cached_ = false;
        if (!io_->emplace<SdIndexedInstall>(volume_, crypto_).beginFinalize(root_, copy_, request, generation, hash, frame_, capacity_, verification_, verification_capacity_, *roots_[1 - copy_])) return fail();
        phase_ = Phase::Finish;
        return JournalWriteResult::InProgress;
    }
    JournalWriteResult stepCommit() override
    {
        if (!commitPending() || !io_ || !owner_.heldBy(this) || root_.revision != revision_) return fail();
        ::geocaching::storage::IndexRootView committed;
        const auto status = std::visit([&](auto& operation) -> IndexedCommitStep
                                       {
                                          using T = std::decay_t<decltype(operation)>;
                                          if constexpr (std::is_same_v<T, SdIndexedNewTask> || std::is_same_v<T, SdIndexedDownloadReply> ||
                                                        std::is_same_v<T, SdIndexedStopTask> || std::is_same_v<T, SdIndexedInstall> || std::is_same_v<T, SdIndexedCommit>)
                                          {
                                              const auto result = operation.step();
                                              if (result == IndexedCommitStep::Verified && !operation.committed(committed)) return IndexedCommitStep::Invalid;
                                              return result;
                                          }
                                          else return IndexedCommitStep::Invalid; },
                                       *io_);
        if (status == IndexedCommitStep::Working) return JournalWriteResult::InProgress;
        if (status != IndexedCommitStep::Verified) return error(status);
        if (committed.revision != root_.revision) copy_ = 1 - copy_;
        root_ = committed;
        revision_ = root_.revision;
        const bool keep = phase_ == Phase::Prepare;
        phase_ = Phase::None;
        io_->emplace<std::monostate>();
        if (!keep) releaseRead();
        return JournalWriteResult::Verified;
    }

  private:
    enum class Phase : uint8_t
    {
        None,
        Context,
        Install,
        Proof,
        Reload,
        Generation,
        GenerationReady,
        UpgradeHead,
        UpgradeObject,
        UpgradeRead,
        Create,
        Reply,
        Stop,
        Prepare,
        Finish
    };
    using Operation = std::variant<std::monostate, SdIndexedDownloadContext, SdIndexGet, SdIndexScan,
                                   SdIndexedNewTask, SdIndexedDownloadReply, SdIndexedStopTask, SdIndexedInstall, SdIndexedCommit>;
    JournalWriteResult beginHeadUpgrade(::geocaching::ByteView object = {})
    {
        upgrade_mutations_[0] = {2, {cache_.data(), 32}, {install_.data(), install_size_}, false};
        if (object.size)
        {
            if (object.size > response_capacity_) return fail();
            std::memcpy(response_, object.data, object.size);
            upgrade_mutations_[1] = {1, {proof_hash_.data(), 32}, {response_, object.size}, false};
        }
        if (!io_->emplace<SdIndexedCommit>(volume_).begin(root_, copy_, upgrade_mutations_.data(), object.size ? 2 : 1, frame_, capacity_, *roots_[1 - copy_])) return fail();
        phase_ = Phase::Finish;
        return JournalWriteResult::InProgress;
    }
    bool matches(::geocaching::ByteView key, uint64_t generation) const
    {
        return !blocked_ && owner_.heldBy(this) && revision_ == root_.revision && generation == generation_ &&
               key.data && key.size == key_.size() && !std::memcmp(key.data, key_.data(), key_.size());
    }
    bool acquire(bool promote_generation = false)
    {
        if (!valid_ || blocked_ || saved_ || page_ || recovery_ || phase_ == Phase::Generation || (phase_ == Phase::GenerationReady && !promote_generation) ||
            copy_ > 1 || !owner_.acquire(this)) return false;
        if (!frame_ || !response_ || !verification_)
        {
            owner_.release(this);
            return false;
        }
        if (!io_) io_.reset(::platform::memory::createPsram<Operation>());
        if (!io_)
        {
            owner_.release(this);
            return false;
        }
        revision_ = root_.revision;
        return true;
    }
    void makeKey(const ::geocaching::Destination& local, const ::geocaching::Destination& remote, const ::geocaching::RequestId& request)
    {
        std::memcpy(key_.data(), local.bytes.data(), 16);
        std::memcpy(key_.data() + 16, remote.bytes.data(), 16);
        std::memcpy(key_.data() + 32, request.bytes.data(), 16);
    }
    JournalWriteResult reload()
    {
        if (!io_->emplace<SdIndexGet>(volume_).begin(root_, 5, {key_.data(), key_.size()}, frame_, capacity_)) return fail();
        phase_ = Phase::Reload;
        return JournalWriteResult::InProgress;
    }
    template <class Status>
    JournalWriteResult error(Status status)
    {
        return fail(status == Status::IoError ? JournalWriteResult::IoError : status == Status::VolumeChanged ? JournalWriteResult::VolumeChanged
                                                                                                              : JournalWriteResult::StateRejected);
    }
    JournalWriteResult fail(JournalWriteResult status = JournalWriteResult::StateRejected)
    {
        blocked_ = true;
        phase_ = Phase::None;
        releaseRead();
        return status;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootView& root_;
    unsigned& copy_;
    ::geocaching::storage::IndexRootBytes* roots_[2];
    IndexWorkspaceOwner& owner_;
    ::geocaching::storage::QueuedRequestWorkspace& workspace_;
    uint8_t *frame_, *response_, *verification_;
    size_t capacity_, response_capacity_, verification_capacity_;
    bool metadata_read_ = false;
    ::geocaching::protocol::RecordCrypto& crypto_;
    ::platform::memory::PsramPtr<Operation> io_;
    ::platform::memory::PsramPtr<SdIndexedDownloadRecovery> recovery_;
    ::platform::memory::PsramPtr<SdIndexedSavedCache> saved_;
    ::platform::memory::PsramPtr<SdIndexedSavedPage> page_;
    ::geocaching::ByteView outgoing_;
    ::geocaching::RevisionHash old_revision_;
    std::array<uint8_t, 48> key_{};
    std::array<uint8_t, 16> task_{};
    std::array<uint8_t, 32> cache_{}, proof_hash_{}, recovery_old_hash_{};
    std::array<uint8_t, 160> install_{};
    std::array<::geocaching::storage::MutationView, 2> upgrade_mutations_;
    uint64_t generation_ = 0, revision_ = 0, proof_generation_ = 0;
    size_t install_size_ = 0;
    Phase phase_ = Phase::None;
    uint8_t page_read_retries_ = 0;
    bool valid_ = false, blocked_ = false, cached_ = false, active_ = false, completed_ = false, has_current_ = false, removed_current_ = false;
    bool projection_upgrade_required_ = false;
};
static_assert(sizeof(IndexedDownloadStore) <= 640, "Idle download storage contains metadata and caller buffer leases only");
} // namespace platform::esp::arduino_common::geocaching
