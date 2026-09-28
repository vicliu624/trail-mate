#pragma once
#include "geocaching/protocol/verify_record.h"
#include "geocaching/storage/pending_request.h"
#include "platform/esp/arduino_common/geocaching/index_workspace_owner.h"
#include "platform/esp/arduino_common/geocaching/sd_index_get.h"
#include <memory>

namespace platform::esp::arduino_common::geocaching
{
// Only saved downloads/publications need durable acknowledgement after reboot.
// Network callbacks queue a bounded fingerprint; the storage worker checks it.
class StoredReplyReceipt
{
  public:
    StoredReplyReceipt(const ::geocaching::storage::VolumeInstance& volume, ::geocaching::storage::IndexRootView& root,
                       const ::geocaching::Destination& local, IndexWorkspaceOwner& owner, ::geocaching::protocol::RecordCrypto& crypto)
        : volume_(volume), root_(root), local_(local), owner_(owner), crypto_(crypto) {}
    ~StoredReplyReceipt() { owner_.release(this); }
    void bindWorkspace(uint8_t* frame) { frame_ = frame; }
    bool pending() const { return phase_ == Phase::Queued || phase_ == Phase::Outgoing || phase_ == Phase::Task; }
    bool accepted(const ::geocaching::Destination& source, const ::geocaching::RequestId& id, ::geocaching::ByteView response)
    {
        if (!response.data || !response.size || response.size > 8192) return false;
        std::array<uint8_t, 48> key;
        std::memcpy(key.data(), local_.bytes.data(), 16);
        std::memcpy(key.data() + 16, source.bytes.data(), 16);
        std::memcpy(key.data() + 32, id.bytes.data(), 16);
        std::array<uint8_t, 32> hash;
        if (!crypto_.sha256(response, hash.data())) return false;
        if (key == key_ && hash == hash_ && size_ == response.size && revision_ == root_.revision &&
            phase_ == Phase::Accepted) return true;
        if (pending()) return false;
        key_ = key;
        hash_ = hash;
        size_ = response.size;
        phase_ = Phase::Queued;
        return false;
    }
    void step()
    {
        using namespace ::geocaching::storage;
        if (!pending()) return;
        if (phase_ == Phase::Queued)
        {
            if (!owner_.acquire(this)) return;
            revision_ = root_.revision;
            read_.reset(new (std::nothrow) SdIndexGet(volume_));
            if (!read_ || !read_->begin(root_, 5, {key_.data(), key_.size()}, frame_, 8192)) return finish(false);
            phase_ = Phase::Outgoing;
            return;
        }
        if (revision_ != root_.revision) return finish(false);
        const auto status = read_->step();
        if (status == IndexGetStep::Working) return;
        if (status != IndexGetStep::Ready) return finish(false);
        if (phase_ == Phase::Task)
        {
            TaskView task;
            return finish(decodeTask({task_.data(), task_.size()}, read_->value(), task) && task.state == 5 && !task.continue_intent);
        }
        OutgoingView outgoing;
        if (!decodeOutgoing({key_.data(), key_.size()}, read_->value(), outgoing)) return finish(false);
        if (outgoing.state == 4)
        {
            std::array<uint8_t, 32> hash;
            return finish(outgoing.terminal_data.size == size_ && crypto_.sha256(outgoing.terminal_data, hash.data()) && hash == hash_);
        }
        std::memcpy(task_.data(), outgoing.task_id.data, task_.size());
        read_.reset(new (std::nothrow) SdIndexGet(volume_));
        if (!read_ || !read_->begin(root_, 10, {task_.data(), task_.size()}, frame_, 8192)) return finish(false);
        phase_ = Phase::Task;
    }

  private:
    void finish(bool accepted)
    {
        read_.reset();
        owner_.release(this);
        phase_ = accepted ? Phase::Accepted : Phase::Rejected;
    }
    enum class Phase : uint8_t
    {
        None,
        Queued,
        Outgoing,
        Task,
        Accepted,
        Rejected
    };
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootView& root_;
    ::geocaching::Destination local_;
    IndexWorkspaceOwner& owner_;
    ::geocaching::protocol::RecordCrypto& crypto_;
    std::unique_ptr<SdIndexGet> read_;
    uint8_t* frame_ = nullptr;
    std::array<uint8_t, 48> key_{};
    std::array<uint8_t, 32> hash_{};
    std::array<uint8_t, 16> task_{};
    size_t size_ = 0;
    uint64_t revision_ = 0;
    Phase phase_ = Phase::None;
};
} // namespace platform::esp::arduino_common::geocaching
