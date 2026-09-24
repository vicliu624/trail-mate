#pragma once
#include "platform/esp/arduino_common/geocaching/index_workspace_owner.h"
#include "platform/esp/arduino_common/geocaching/publication_store.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_author_reservation.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_directory_reply.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_draft_catalog.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_new_task.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_publication_history.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_publication_recovery.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_stop_task.h"

namespace platform::esp::arduino_common::geocaching
{
// Uses the session's single root and shared workspace owner. An idle adapter
// retains no payload or operation; signing jobs supply their own admitted lease.
class IndexedPublicationStore final : public PublicationStore
{
  public:
    IndexedPublicationStore(const ::geocaching::storage::VolumeInstance& volume, ::geocaching::storage::IndexRootView& root, unsigned& copy,
                            ::geocaching::storage::IndexRootBytes& first, ::geocaching::storage::IndexRootBytes& second,
                            IndexWorkspaceOwner& owner, ::geocaching::storage::QueuedRequestWorkspace& workspace,
                            uint8_t* frame, size_t capacity, uint8_t* response, size_t response_capacity,
                            uint8_t* verification, size_t verification_capacity, ::geocaching::protocol::RecordCrypto& crypto)
        : volume_(volume), root_(root), copy_(copy), roots_{&first, &second}, owner_(owner), workspace_(workspace), frame_(frame), capacity_(capacity),
          response_(response), response_capacity_(response_capacity), verification_(verification), verification_capacity_(verification_capacity), crypto_(crypto)
    {
        valid_ = copy < 2 && ::geocaching::storage::validIndexRoot(root) && root.shards.data == roots_[copy]->data() + 48 &&
                 workspace.outgoing && workspace.outgoing_capacity && frame && capacity >= 24 && response && response_capacity && verification && verification_capacity;
        const ::geocaching::ByteView buffers[] = {{first.data(), first.size()}, {second.data(), second.size()}, {workspace.outgoing, workspace.outgoing_capacity}, {workspace.task.data(), workspace.task.size()}, {frame, capacity}, {response, response_capacity}, {verification, verification_capacity}};
        for (size_t i = 0; i < 7; ++i)
            for (size_t j = 0; j < i; ++j)
            {
                const auto a = reinterpret_cast<uintptr_t>(buffers[i].data), b = reinterpret_cast<uintptr_t>(buffers[j].data);
                if (buffers[i].size && buffers[j].size && (a <= b ? b - a < buffers[i].size : a - b < buffers[j].size)) valid_ = false;
            }
    }
    ~IndexedPublicationStore() override { release(); }
    bool needsRecovery() const override { return blocked_; }
    void bindWorkspace(uint8_t* frame, uint8_t* response, uint8_t* verification)
    {
        frame_ = frame;
        response_ = response;
        verification_ = verification;
    }
    bool commitPending() const override { return io_ != nullptr; }
    uint64_t committedSequence() const { return root_.sequence; }
    DraftReadResult readDraft(::geocaching::ByteView key, ::geocaching::ByteView& value) override
    {
        value = {};
        if (!valid_ || !key.data || key.size != read_key_.size()) return DraftReadResult::Invalid;
        if (blocked_) return DraftReadResult::Unavailable;
        if (io_ || catalog_ || history_ || recovery_ || (reader_ && std::memcmp(read_key_.data(), key.data, key.size))) return DraftReadResult::Busy;
        if (!reader_)
        {
            if (!owner_.acquire(this)) return DraftReadResult::Busy;
            reader_.reset(new (std::nothrow) SdIndexGet(volume_));
            if (!reader_)
            {
                owner_.release(this);
                return DraftReadResult::Unavailable;
            }
            std::memcpy(read_key_.data(), key.data, key.size);
            revision_ = root_.revision;
            if (!reader_->begin(root_, 4, {read_key_.data(), read_key_.size()}, frame_, capacity_))
            {
                releaseDraftRead();
                return DraftReadResult::Invalid;
            }
            return DraftReadResult::Pending;
        }
        if (!owner_.heldBy(this) || revision_ != root_.revision)
        {
            blocked_ = true;
            releaseDraftRead();
            return DraftReadResult::Invalid;
        }
        const auto status = reader_->step();
        if (status == IndexGetStep::Working) return DraftReadResult::Pending;
        if (status == IndexGetStep::Ready)
        {
            value = reader_->value();
            return DraftReadResult::Ready;
        }
        blocked_ |= status != IndexGetStep::NotFound && status != IndexGetStep::WorkspaceTooSmall;
        releaseDraftRead();
        if (status == IndexGetStep::WorkspaceTooSmall) return DraftReadResult::WorkspaceTooSmall;
        return status == IndexGetStep::NotFound ? DraftReadResult::NotFound : status == IndexGetStep::IoError     ? DraftReadResult::IoError
                                                                          : status == IndexGetStep::VolumeChanged ? DraftReadResult::VolumeChanged
                                                                                                                  : DraftReadResult::Invalid;
    }
    void releaseDraftRead() override
    {
        if (!reader_ && !catalog_ && !history_ && !recovery_) return;
        reader_.reset();
        catalog_.reset();
        history_.reset();
        recovery_.reset();
        catalog_page_ = nullptr;
        owner_.release(this);
    }
    DraftReadResult readDraftCatalog(size_t offset, ::geocaching::protocol::RecordCrypto& crypto, ::geocaching::storage::DraftCatalogPage& page) override
    {
        if (!valid_ || &crypto != &crypto_) return DraftReadResult::Invalid;
        if (blocked_) return DraftReadResult::Unavailable;
        if (io_ || reader_ || history_ || recovery_ || (catalog_ && (catalog_page_ != &page || page.offset != offset))) return DraftReadResult::Busy;
        if (!catalog_)
        {
            if (!owner_.acquire(this)) return DraftReadResult::Busy;
            catalog_.reset(new (std::nothrow) SdIndexedDraftCatalog(volume_, crypto_));
            if (!catalog_)
            {
                owner_.release(this);
                return DraftReadResult::Unavailable;
            }
            catalog_page_ = &page;
            revision_ = root_.revision;
            if (!catalog_->begin(root_, offset, page, frame_, capacity_))
            {
                const bool unavailable = catalog_->unavailable();
                releaseDraftRead();
                return unavailable ? DraftReadResult::Unavailable : DraftReadResult::Invalid;
            }
            return DraftReadResult::Pending;
        }
        if (!owner_.heldBy(this) || revision_ != root_.revision)
        {
            blocked_ = true;
            releaseDraftRead();
            return DraftReadResult::Invalid;
        }
        const auto status = catalog_->step();
        if (status == IndexGetStep::Working) return DraftReadResult::Pending;
        const bool unavailable = catalog_->unavailable();
        releaseDraftRead();
        if (unavailable) return DraftReadResult::Unavailable;
        if (status == IndexGetStep::Ready) return DraftReadResult::Ready;
        if (status == IndexGetStep::WorkspaceTooSmall) return DraftReadResult::WorkspaceTooSmall;
        blocked_ = true;
        return status == IndexGetStep::IoError ? DraftReadResult::IoError : status == IndexGetStep::VolumeChanged ? DraftReadResult::VolumeChanged
                                                                                                                  : DraftReadResult::Invalid;
    }
    DraftReadResult readPublicationHistory(::geocaching::ByteView key, uint64_t generation,
                                           const ::geocaching::GeocacheId& cache, ::geocaching::ByteView author,
                                           ::geocaching::storage::PublicationHistory& out) override
    {
        if (!valid_ || !key.data || key.size != 16 || !author.data || author.size != 64) return DraftReadResult::Invalid;
        if (blocked_) return DraftReadResult::Unavailable;
        if (io_ || reader_ || catalog_ || recovery_ || (history_ && !history_->matches(key, generation, cache, author, out))) return DraftReadResult::Busy;
        if (!history_)
        {
            if (!owner_.acquire(this)) return DraftReadResult::Busy;
            history_.reset(new (std::nothrow) SdIndexedPublicationHistory(volume_, crypto_));
            if (!history_)
            {
                owner_.release(this);
                return DraftReadResult::Unavailable;
            }
            revision_ = root_.revision;
            if (!history_->begin(root_, key, generation, cache, author, out, frame_, capacity_))
            {
                const bool unavailable = history_->unavailable();
                releaseDraftRead();
                return unavailable ? DraftReadResult::Unavailable : DraftReadResult::Invalid;
            }
            return DraftReadResult::Pending;
        }
        if (!owner_.heldBy(this) || revision_ != root_.revision)
        {
            blocked_ = true;
            releaseDraftRead();
            return DraftReadResult::Invalid;
        }
        const auto status = history_->step();
        if (status == IndexGetStep::Working) return DraftReadResult::Pending;
        const bool unavailable = history_->unavailable();
        releaseDraftRead();
        if (unavailable) return DraftReadResult::Unavailable;
        if (status == IndexGetStep::Ready) return DraftReadResult::Ready;
        if (status == IndexGetStep::NotFound) return DraftReadResult::NotFound;
        if (status == IndexGetStep::WorkspaceTooSmall) return DraftReadResult::WorkspaceTooSmall;
        blocked_ = true;
        return status == IndexGetStep::IoError ? DraftReadResult::IoError : status == IndexGetStep::VolumeChanged ? DraftReadResult::VolumeChanged
                                                                                                                  : DraftReadResult::Invalid;
    }
    DraftReadResult readPublicationRecovery(const ::geocaching::storage::PublicationRecoveryFilter& filter,
                                            ::geocaching::storage::PublicationRecoveryView& out) override
    {
        out = {};
        if (!valid_) return DraftReadResult::Invalid;
        if (blocked_) return DraftReadResult::Unavailable;
        if (io_ || reader_ || catalog_ || history_ || (recovery_ && !recovery_->matches(filter))) return DraftReadResult::Busy;
        if (!recovery_)
        {
            if (!owner_.acquire(this)) return DraftReadResult::Busy;
            recovery_.reset(new (std::nothrow) SdIndexedPublicationRecovery(volume_));
            if (!recovery_)
            {
                owner_.release(this);
                return DraftReadResult::Unavailable;
            }
            revision_ = root_.revision;
            if (!recovery_->begin(root_, filter, frame_, capacity_))
            {
                const bool unavailable = recovery_->unavailable();
                releaseDraftRead();
                return unavailable ? DraftReadResult::Unavailable : DraftReadResult::Invalid;
            }
            return DraftReadResult::Pending;
        }
        if (!owner_.heldBy(this) || revision_ != root_.revision)
        {
            blocked_ = true;
            releaseDraftRead();
            return DraftReadResult::Invalid;
        }
        const auto status = recovery_->step();
        if (status == IndexGetStep::Working) return DraftReadResult::Pending;
        if (status == IndexGetStep::Ready && recovery_->value(out)) return DraftReadResult::Ready;
        const bool unavailable = recovery_->unavailable();
        releaseDraftRead();
        if (unavailable) return DraftReadResult::Unavailable;
        if (status == IndexGetStep::NotFound) return DraftReadResult::NotFound;
        if (status == IndexGetStep::WorkspaceTooSmall) return DraftReadResult::WorkspaceTooSmall;
        blocked_ = true;
        return status == IndexGetStep::IoError ? DraftReadResult::IoError : status == IndexGetStep::VolumeChanged ? DraftReadResult::VolumeChanged
                                                                                                                  : DraftReadResult::Invalid;
    }
    JournalWriteResult editDraft(::geocaching::ByteView key, uint8_t* bytes, size_t size, size_t capacity, uint64_t expected) override
    {
        const auto acquired = acquire();
        if (acquired != JournalWriteResult::Verified) return acquired;
        return started(io_->emplace<SdIndexedDraftSave>(volume_).beginEdit(root_, copy_, key, bytes, size, capacity, expected,
                                                                           frame_, capacity_, *roots_[1 - copy_]));
    }
    bool inputConsumed() const
    {
        if (!io_) return true;
        return std::visit([](const auto& operation)
                          {
                              using T = std::decay_t<decltype(operation)>;
                              if constexpr (std::is_same_v<T, std::monostate> || std::is_same_v<T, SdIndexedStopTask>) return true;
                              else return operation.inputConsumed(); },
                          *io_);
    }
    JournalWriteResult saveDraft(::geocaching::ByteView key, ::geocaching::ByteView encoded, uint64_t generation)
    {
        const auto acquired = acquire();
        if (acquired != JournalWriteResult::Verified) return acquired;
        return started(io_->emplace<SdIndexedDraftSave>(volume_).begin(root_, copy_, key, encoded, generation, frame_, capacity_, *roots_[1 - copy_]));
    }
    JournalWriteResult bindDraftAuthor(::geocaching::ByteView key, uint64_t generation, ::geocaching::ByteView author,
                                       uint8_t* workspace, size_t capacity)
    {
        const auto acquired = acquire();
        if (acquired != JournalWriteResult::Verified) return acquired;
        return started(io_->emplace<SdIndexedAuthorReservation>(volume_, crypto_).beginBind(root_, copy_, key, generation, author, workspace, capacity, frame_, capacity_, *roots_[1 - copy_]));
    }
    JournalWriteResult reserveUnsignedRecord(::geocaching::ByteView record, ::geocaching::protocol::RecordCrypto& crypto,
                                             uint8_t* workspace, size_t capacity, const ::geocaching::storage::StoredTime& time) override
    {
        return reserveDraftUnsignedRecord({}, 0, record, crypto, workspace, capacity, time);
    }
    JournalWriteResult reserveDraftUnsignedRecord(::geocaching::ByteView key, uint64_t generation, ::geocaching::ByteView record,
                                                  ::geocaching::protocol::RecordCrypto& crypto, uint8_t* workspace, size_t capacity,
                                                  const ::geocaching::storage::StoredTime& time) override
    {
        if (&crypto != &crypto_) return JournalWriteResult::Invalid;
        const auto acquired = acquire();
        if (acquired != JournalWriteResult::Verified) return acquired;
        return started(io_->emplace<SdIndexedAuthorReservation>(volume_, crypto_).beginReserve(root_, copy_, record, time, workspace, capacity, frame_, capacity_, *roots_[1 - copy_], key, generation));
    }
    JournalWriteResult persistNewTask(const ::geocaching::Destination& local, const ::geocaching::Destination& remote,
                                      const ::geocaching::RequestId& request, const std::array<uint8_t, 16>& task, uint8_t kind,
                                      ::geocaching::ByteView bytes, const ::geocaching::storage::StoredTime& time,
                                      const ::geocaching::storage::RequestTaskTarget& target) override
    {
        if (kind != 1) return JournalWriteResult::Invalid;
        const auto acquired = acquire();
        if (acquired != JournalWriteResult::Verified) return acquired;
        // NewTask encodes both rows into the leased workspace synchronously.
        return started(io_->emplace<SdIndexedNewTask>(volume_).begin(root_, copy_, local, remote, request, task, kind, bytes, time, target,
                                                                     workspace_, frame_, capacity_, *roots_[1 - copy_]));
    }
    JournalWriteResult commitPublishResult(const ::geocaching::Destination& local, const ::geocaching::Destination& remote,
                                           const ::geocaching::RequestId& request, ::geocaching::ByteView response,
                                           ::geocaching::protocol::RecordCrypto& crypto) override
    {
        if (&crypto != &crypto_ || !response.data || response.size > 512 || response.size > response_capacity_) return JournalWriteResult::Invalid;
        const auto acquired = acquire();
        if (acquired != JournalWriteResult::Verified) return acquired;
        std::memcpy(key_.data(), local.bytes.data(), 16);
        std::memcpy(key_.data() + 16, remote.bytes.data(), 16);
        std::memcpy(key_.data() + 32, request.bytes.data(), 16);
        std::memmove(response_, response.data, response.size);
        return started(io_->emplace<SdIndexedDirectoryReply>(volume_).begin(root_, copy_, {key_.data(), key_.size()}, 1, {response_, response.size},
                                                                            workspace_, frame_, capacity_, *roots_[1 - copy_], &crypto_, verification_, verification_capacity_));
    }
    JournalWriteResult stopTask(const std::array<uint8_t, 16>& task) override
    {
        const auto acquired = acquire();
        if (acquired != JournalWriteResult::Verified) return acquired;
        return started(io_->emplace<SdIndexedStopTask>(volume_).begin(root_, copy_, {task.data(), task.size()}, false, frame_, capacity_, *roots_[1 - copy_]));
    }
    JournalWriteResult stepCommit() override
    {
        if (!io_ || !owner_.heldBy(this) || root_.revision != revision_)
        {
            blocked_ = true;
            release();
            return JournalWriteResult::StateRejected;
        }
        ::geocaching::storage::IndexRootView committed;
        const auto result = std::visit([&](auto& operation) -> IndexedCommitStep
                                       {
                                           using T = std::decay_t<decltype(operation)>;
                                           if constexpr (std::is_same_v<T, std::monostate>) return IndexedCommitStep::Invalid;
                                           else
                                           {
                                               const auto status = operation.step();
                                               if (status == IndexedCommitStep::Verified && !operation.committed(committed)) return IndexedCommitStep::RecoveryRequired;
                                               return status;
                                           } },
                                       *io_);
        if (result == IndexedCommitStep::Working) return JournalWriteResult::InProgress;
        if (result == IndexedCommitStep::Verified)
        {
            if (committed.revision != root_.revision) copy_ = 1 - copy_;
            root_ = committed;
        }
        // Business rejection happens before the journal is written. I/O,
        // changed media or a possibly durable partial commit requires recovery.
        blocked_ |= result == IndexedCommitStep::IoError || result == IndexedCommitStep::VolumeChanged || result == IndexedCommitStep::RecoveryRequired;
        release();
        return result == IndexedCommitStep::Verified ? JournalWriteResult::Verified : result == IndexedCommitStep::IoError        ? JournalWriteResult::IoError
                                                                                  : result == IndexedCommitStep::VolumeChanged    ? JournalWriteResult::VolumeChanged
                                                                                  : result == IndexedCommitStep::RecoveryRequired ? JournalWriteResult::Unavailable
                                                                                                                                  : JournalWriteResult::StateRejected;
    }
    JournalWriteResult cancelCommit() override
    {
        if (!io_) return JournalWriteResult::Invalid;
        // Cancellation never rolls back an issued identity. Reconcile any
        // durable journal before allowing another reservation on this volume.
        blocked_ = true;
        release();
        return JournalWriteResult::Cancelled;
    }

  private:
    using Operation = std::variant<std::monostate, SdIndexedAuthorReservation, SdIndexedDraftSave, SdIndexedNewTask, SdIndexedDirectoryReply, SdIndexedStopTask>;
    JournalWriteResult acquire()
    {
        if (!valid_) return JournalWriteResult::Invalid;
        if (blocked_) return JournalWriteResult::Unavailable;
        if (io_ || reader_ || catalog_ || history_ || recovery_ || !owner_.acquire(this)) return JournalWriteResult::Busy;
        if (copy_ > 1 || !::geocaching::storage::validIndexRoot(root_) || root_.shards.data != roots_[copy_]->data() + 48)
        {
            blocked_ = true;
            release();
            return JournalWriteResult::StateRejected;
        }
        io_.reset(new (std::nothrow) Operation);
        if (!io_)
        {
            release();
            return JournalWriteResult::Unavailable;
        }
        revision_ = root_.revision;
        return JournalWriteResult::Verified;
    }
    JournalWriteResult started(bool ok)
    {
        if (ok) return JournalWriteResult::InProgress;
        release();
        return JournalWriteResult::Invalid;
    }
    void release()
    {
        io_.reset();
        reader_.reset();
        catalog_.reset();
        catalog_page_ = nullptr;
        history_.reset();
        recovery_.reset();
        owner_.release(this);
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootView& root_;
    unsigned& copy_;
    ::geocaching::storage::IndexRootBytes* roots_[2];
    IndexWorkspaceOwner& owner_;
    ::geocaching::storage::QueuedRequestWorkspace& workspace_;
    uint8_t* frame_;
    size_t capacity_;
    uint8_t* response_;
    size_t response_capacity_;
    uint8_t* verification_;
    size_t verification_capacity_;
    ::geocaching::protocol::RecordCrypto& crypto_;
    std::unique_ptr<Operation> io_;
    std::unique_ptr<SdIndexGet> reader_;
    std::unique_ptr<SdIndexedDraftCatalog> catalog_;
    std::unique_ptr<SdIndexedPublicationHistory> history_;
    std::unique_ptr<SdIndexedPublicationRecovery> recovery_;
    ::geocaching::storage::DraftCatalogPage* catalog_page_ = nullptr;
    std::array<uint8_t, 16> read_key_{};
    std::array<uint8_t, 48> key_{};
    uint64_t revision_ = 0;
    bool valid_ = false, blocked_ = false;
};
static_assert(sizeof(IndexedPublicationStore) <= 256, "Idle publication storage owns leases and small metadata only");
} // namespace platform::esp::arduino_common::geocaching
