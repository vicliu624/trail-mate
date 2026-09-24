#pragma once
#include "geocaching/protocol/verify_record.h"
#include "platform/esp/arduino_common/geocaching/index_workspace_owner.h"
#include "platform/esp/arduino_common/geocaching/query_browse_port.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_directory_reply.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_new_task.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_stop_task.h"

namespace platform::esp::arduino_common::geocaching
{
// Session-owned buffers: one current response, transient encoding/read leases.
// Operations are allocated only while active. The owner serializes this port
// with dispatcher/storage work sharing the same roots and scratch buffers.
class IndexedQueryStorePort final : public QueryBrowsePort
{
  public:
    using RandomId = bool (*)(void*, uint8_t[16]);
    using Now = ::geocaching::storage::StoredTime (*)(void*);
    IndexedQueryStorePort(const ::geocaching::storage::VolumeInstance& volume, ::geocaching::storage::IndexRootView& root,
                          unsigned& copy, ::geocaching::storage::IndexRootBytes& first, ::geocaching::storage::IndexRootBytes& second,
                          const ::geocaching::Destination& local, IndexWorkspaceOwner& owner, ::geocaching::storage::QueuedRequestWorkspace& workspace,
                          uint8_t* frame, size_t capacity, uint8_t* response, size_t response_capacity,
                          ::geocaching::protocol::RecordCrypto& crypto, RandomId random, Now now, void* context)
        : volume_(volume), root_(root), copy_(copy), roots_{&first, &second}, local_(local), owner_(owner), workspace_(workspace), frame_(frame),
          capacity_(capacity), response_(response), response_capacity_(response_capacity), crypto_(crypto), random_(random), now_(now), context_(context)
    {
        valid_ = copy < 2 && ::geocaching::storage::validIndexRoot(root) && root.shards.data == roots_[copy]->data() + 48 &&
                 frame && capacity >= 24 && response && response_capacity >= 512 && workspace.outgoing;
        const ::geocaching::ByteView leases[] = {{first.data(), first.size()}, {second.data(), second.size()}, {frame, capacity}, {response, response_capacity}, {workspace.outgoing, workspace.outgoing_capacity}, {workspace.task.data(), workspace.task.size()}};
        for (size_t i = 0; i < 6; ++i)
            for (size_t j = 0; j < i; ++j)
            {
                const auto a = reinterpret_cast<uintptr_t>(leases[i].data), b = reinterpret_cast<uintptr_t>(leases[j].data);
                if (leases[i].size && leases[j].size && (a <= b ? b - a < leases[i].size : a - b < leases[j].size)) valid_ = false;
            }
    }
    ~IndexedQueryStorePort() override
    {
        io_.reset();
        owner_.release(this);
    }
    bool newRequestId(::geocaching::RequestId& out) override { return random_ && random_(context_, out.bytes.data()); }
    ::geocaching::QueryPersistence submit(const ::geocaching::DirectoryEntry& directory, const ::geocaching::RequestId& id,
                                          ::geocaching::ByteView request) override
    {
        if (!available() || !random_ || !now_ || !allocate()) return Result::Rejected;
        std::array<uint8_t, 16> task;
        if (!random_(context_, task.data())) return reject();
        auto& queued = io_->emplace<QueuedRequest>();
        queued.destination = directory.delivery;
        queued.id = id;
        queued.task = task;
        queued.request = request;
        queued.time = now_(context_);
        pending_ = Pending::Request;
        return Result::Pending;
    }
    ::geocaching::QueryPersistence commitCapabilities(const ::geocaching::Destination& source, const ::geocaching::RequestId& id,
                                                      ::geocaching::ByteView response) override
    {
        ::geocaching::protocol::DirectoryCapabilities capabilities;
        if (!::geocaching::protocol::decodeDirectoryCapabilities(response, id, capabilities)) return Result::Rejected;
        return beginReply(source, id, response, Pending::Capabilities);
    }
    ::geocaching::QueryPersistence commitPage(const ::geocaching::Destination& source, const ::geocaching::RequestId& id,
                                              ::geocaching::ByteView response, const ::geocaching::protocol::QueryPageView&) override
    {
        ::geocaching::protocol::QueryPageView checked;
        if (!::geocaching::protocol::decodeQueryPage(response, id, 2048, 20, checked)) return Result::Rejected;
        return beginReply(source, id, response, Pending::Page);
    }
    ::geocaching::QueryPersistence cancel(const ::geocaching::Destination& source, const ::geocaching::RequestId& id) override
    {
        if (!available() || !allocate()) return Result::Rejected;
        makeKey(source, id, pending_key_);
        pending_ = Pending::Cancel;
        return Result::Pending;
    }
    ::geocaching::QueryPersistence pollPersistence() override
    {
        if (pending_ == Pending::None) return Result::Rejected;
        if (!io_) return reject();
        if (std::holds_alternative<std::monostate>(*io_) || std::holds_alternative<QueuedRequest>(*io_))
        {
            if (!owner_.acquire(this)) return Result::Pending;
            revision_ = root_.revision;
            bool begun = false;
            if (pending_ == Pending::Request)
            {
                const auto queued = std::get<QueuedRequest>(*io_);
                begun = io_->emplace<SdIndexedNewTask>(volume_).begin(root_, copy_, local_, queued.destination, queued.id, queued.task,
                                                                      3, queued.request, queued.time, {}, workspace_, frame_, capacity_, *roots_[1 - copy_]);
            }
            else if (pending_ == Pending::Cancel)
                begun = io_->emplace<SdIndexedStopTask>(volume_).begin(root_, copy_, {pending_key_.data(), pending_key_.size()}, true,
                                                                       frame_, capacity_, *roots_[1 - copy_]);
            else
                begun = io_->emplace<SdIndexedDirectoryReply>(volume_).begin(root_, copy_, {response_key_.data(), response_key_.size()},
                                                                             pending_ == Pending::Page ? 2 : 0, {response_, response_size_}, workspace_, frame_, capacity_, *roots_[1 - copy_]);
            return begun ? Result::Pending : reject();
        }
        if (!owner_.heldBy(this) || root_.revision != revision_) return reject();
        IndexedCommitStep status;
        ::geocaching::storage::IndexRootView committed;
        bool selected = false;
        if (pending_ == Pending::Request)
        {
            auto& operation = std::get<SdIndexedNewTask>(*io_);
            status = operation.step();
            if (status == IndexedCommitStep::Verified) selected = operation.committed(committed);
        }
        else if (pending_ == Pending::Cancel)
        {
            auto& operation = std::get<SdIndexedStopTask>(*io_);
            status = operation.step();
            if (status == IndexedCommitStep::Verified) selected = operation.committed(committed);
        }
        else
        {
            auto& operation = std::get<SdIndexedDirectoryReply>(*io_);
            status = operation.step();
            if (status == IndexedCommitStep::Verified) selected = operation.committed(committed);
        }
        if (status == IndexedCommitStep::Working) return Result::Pending;
        if (status != IndexedCommitStep::Verified || !selected) return reject();
        if (committed.revision != root_.revision) copy_ = 1 - copy_;
        root_ = committed;
        if (pending_ == Pending::Capabilities || pending_ == Pending::Page)
        {
            response_committed_ = true;
            page_visible_ = pending_ == Pending::Page;
            if (page_visible_) ++generation_;
        }
        if (pending_ == Pending::Cancel)
        {
            stopped_key_ = pending_key_;
            has_stop_ = true;
            stopped_revision_ = root_.revision;
        }
        pending_ = Pending::None;
        io_.reset();
        owner_.release(this);
        return Result::Committed;
    }
    uint64_t generation() const override { return generation_; }
    bool pageSource(::geocaching::Destination& out) const override
    {
        if (!page_visible_) return false;
        std::memcpy(out.bytes.data(), response_key_.data() + 16, 16);
        return true;
    }
    bool page(::geocaching::protocol::QueryPageView& out) const override
    {
        out = {};
        if (!page_visible_ || !response_committed_) return false;
        ::geocaching::RequestId id;
        std::memcpy(id.bytes.data(), response_key_.data() + 32, 16);
        return ::geocaching::protocol::decodeQueryPage({response_, response_size_}, id, 2048, 20, out);
    }
    bool summary(size_t index, ::geocaching::protocol::SummaryView& out) const override
    {
        out = {};
        ::geocaching::protocol::QueryPageView current;
        if (!page(current) || index >= current.count) return false;
        ::geocaching::protocol::CmpReader rows(current.encoded_items);
        for (size_t i = 0; i <= index; ++i)
            if (!::geocaching::protocol::decodeSummary(rows, out)) return false;
        return true;
    }
    bool accepted(const ::geocaching::Destination& source, const ::geocaching::RequestId& id, ::geocaching::ByteView response) const override
    {
        if (!valid_ || !response.data || response.size > ::geocaching::kMaxApplicationBytes) return false;
        std::array<uint8_t, 48> key;
        makeKey(source, id, key);
        if (has_stop_ && stopped_revision_ == root_.revision && key == stopped_key_) return true;
        if (response_committed_ && key == response_key_ && response.size == response_size_ && !std::memcmp(response.data, response_, response.size)) return true;
        std::array<uint8_t, 32> hash;
        if (!crypto_.sha256(response, hash.data())) return false;
        if (proof_ == Proof::Accepted && proof_revision_ == root_.revision && proof_key_ == key && proof_hash_ == hash && proof_size_ == response.size) return true;
        if (proof_ == Proof::Queued || proof_ == Proof::Outgoing || proof_ == Proof::Task) return false;
        proof_key_ = key;
        proof_hash_ = hash;
        proof_size_ = response.size;
        proof_ = Proof::Queued;
        return false;
    }
    bool maintenancePending() const override { return proof_ == Proof::Queued || proof_ == Proof::Outgoing || proof_ == Proof::Task; }
    void maintenanceStep() override
    {
        using namespace ::geocaching::storage;
        if (pending_ != Pending::None || !maintenancePending()) return;
        if (proof_ == Proof::Queued)
        {
            if (!owner_.acquire(this)) return;
            if (!allocate() || !io_->emplace<SdIndexGet>(volume_).begin(root_, 5, {proof_key_.data(), proof_key_.size()}, frame_, capacity_))
            {
                finishProof(false);
                return;
            }
            revision_ = root_.revision;
            proof_ = Proof::Outgoing;
            return;
        }
        if (!io_ || !owner_.heldBy(this) || revision_ != root_.revision)
        {
            io_.reset();
            owner_.release(this);
            proof_ = Proof::Queued;
            return;
        }
        auto& read = std::get<SdIndexGet>(*io_);
        const auto status = read.step();
        if (status == IndexGetStep::Working) return;
        if (status != IndexGetStep::Ready)
        {
            finishProof(false);
            return;
        }
        if (proof_ == Proof::Task)
        {
            TaskView task;
            finishProof(decodeTask({proof_task_.data(), proof_task_.size()}, read.value(), task) && task.state == 5 && !task.continue_intent);
            return;
        }
        OutgoingView outgoing;
        if (!decodeOutgoing({proof_key_.data(), proof_key_.size()}, read.value(), outgoing))
        {
            finishProof(false);
            return;
        }
        if (outgoing.state == 4)
        {
            std::array<uint8_t, 32> hash;
            finishProof(outgoing.terminal_data.size == proof_size_ && crypto_.sha256(outgoing.terminal_data, hash.data()) && hash == proof_hash_);
            return;
        }
        std::memcpy(proof_task_.data(), outgoing.task_id.data, proof_task_.size());
        if (!io_->emplace<SdIndexGet>(volume_).begin(root_, 10, {proof_task_.data(), proof_task_.size()}, frame_, capacity_))
        {
            finishProof(false);
            return;
        }
        proof_ = Proof::Task;
    }

  private:
    using Result = ::geocaching::QueryPersistence;
    enum class Pending : uint8_t
    {
        None,
        Request,
        Capabilities,
        Page,
        Cancel
    };
    enum class Proof : uint8_t
    {
        None,
        Queued,
        Outgoing,
        Task,
        Accepted,
        Rejected
    };
    struct QueuedRequest
    {
        ::geocaching::Destination destination;
        ::geocaching::RequestId id;
        std::array<uint8_t, 16> task{};
        ::geocaching::ByteView request;
        ::geocaching::storage::StoredTime time;
    };
    using Operation = std::variant<std::monostate, QueuedRequest, SdIndexedNewTask, SdIndexedDirectoryReply, SdIndexedStopTask, SdIndexGet>;
    bool available() const { return valid_ && pending_ == Pending::None && proof_ != Proof::Outgoing && proof_ != Proof::Task && copy_ < 2; }
    bool allocate()
    {
        io_.reset(new (std::nothrow) Operation);
        return io_ != nullptr;
    }
    Result reject()
    {
        pending_ = Pending::None;
        io_.reset();
        owner_.release(this);
        return Result::Rejected;
    }
    void finishProof(bool accepted)
    {
        io_.reset();
        owner_.release(this);
        proof_revision_ = root_.revision;
        proof_ = accepted ? Proof::Accepted : Proof::Rejected;
    }
    void makeKey(const ::geocaching::Destination& remote, const ::geocaching::RequestId& id, std::array<uint8_t, 48>& key) const
    {
        std::memcpy(key.data(), local_.bytes.data(), 16);
        std::memcpy(key.data() + 16, remote.bytes.data(), 16);
        std::memcpy(key.data() + 32, id.bytes.data(), 16);
    }
    Result beginReply(const ::geocaching::Destination& source, const ::geocaching::RequestId& id, ::geocaching::ByteView response,
                      Pending pending)
    {
        if (!available() || !response_ || response.size > response_capacity_ || !allocate()) return Result::Rejected;
        if (page_visible_) ++generation_;
        page_visible_ = response_committed_ = false;
        makeKey(source, id, response_key_);
        std::memmove(response_, response.data, response.size);
        response_size_ = response.size;
        pending_ = pending;
        return Result::Pending;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootView& root_;
    unsigned& copy_;
    ::geocaching::storage::IndexRootBytes* roots_[2];
    ::geocaching::Destination local_;
    IndexWorkspaceOwner& owner_;
    ::geocaching::storage::QueuedRequestWorkspace& workspace_;
    uint8_t *frame_, *response_;
    size_t capacity_, response_capacity_, response_size_ = 0;
    ::geocaching::protocol::RecordCrypto& crypto_;
    RandomId random_;
    Now now_;
    void* context_;
    std::unique_ptr<Operation> io_;
    std::array<uint8_t, 48> response_key_{}, pending_key_{}, stopped_key_{};
    mutable std::array<uint8_t, 48> proof_key_{};
    mutable std::array<uint8_t, 32> proof_hash_{};
    std::array<uint8_t, 16> proof_task_{};
    mutable size_t proof_size_ = 0;
    uint64_t generation_ = 0, revision_ = 0;
    uint64_t stopped_revision_ = 0, proof_revision_ = 0;
    bool valid_ = false;
    bool response_committed_ = false, page_visible_ = false, has_stop_ = false;
    Pending pending_ = Pending::None;
    mutable Proof proof_ = Proof::None;
};
static_assert(sizeof(IndexedQueryStorePort) <= 512, "Idle query adapter owns neither operation workspaces nor page payloads");
} // namespace platform::esp::arduino_common::geocaching
