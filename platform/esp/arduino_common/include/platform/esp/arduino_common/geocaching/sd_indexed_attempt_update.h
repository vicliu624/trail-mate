#pragma once
#include "geocaching/storage/tx_attempt.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_commit.h"

namespace platform::esp::arduino_common::geocaching
{
class SdIndexedAttemptUpdate
{
  public:
    explicit SdIndexedAttemptUpdate(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    // Nonempty hash selects transport-hash recording; empty hash selects a
    // terminal event. Only terminal events need caller-owned outgoing encoding.
    bool begin(const ::geocaching::storage::IndexRootView& root, unsigned copy, ::geocaching::ByteView key,
               ::geocaching::ByteView hash, ::geocaching::storage::TxAttemptState terminal,
               const ::geocaching::storage::StoredTime& finished, uint8_t* outgoing, size_t outgoing_capacity,
               uint8_t* frame, size_t capacity, ::geocaching::storage::IndexRootBytes& candidate)
    {
        using namespace ::geocaching::storage;
        if (result_ != IndexedCommitStep::Idle || !key.data || key.size != key_.size() || copy > 1 ||
            (hash.size && (!hash.data || hash.size != 32)) ||
            (!hash.size && (!outgoing || !outgoing_capacity ||
                            (terminal != TxAttemptState::Delivered && terminal != TxAttemptState::Failed && terminal != TxAttemptState::CancelledBeforeSend)))) return false;
        const auto overlaps = [](::geocaching::ByteView a, ::geocaching::ByteView b)
        {
            const auto x = reinterpret_cast<uintptr_t>(a.data), y = reinterpret_cast<uintptr_t>(b.data);
            return a.size && b.size && (x <= y ? y - x < a.size : x - y < b.size);
        };
        const ::geocaching::ByteView output{candidate.data(), candidate.size()}, scratch{frame, capacity}, encoding{outgoing, outgoing_capacity};
        if (overlaps(output, scratch) || overlaps(output, root.shards) || overlaps(encoding, output) ||
            overlaps(encoding, scratch) || overlaps(encoding, root.shards)) return false;
        std::memcpy(key_.data(), key.data, key_.size());
        hash_update_ = hash.size != 0;
        if (hash_update_) std::memcpy(hash_.data(), hash.data, hash_.size());
        terminal_ = terminal;
        finished_ = finished;
        root_ = root;
        copy_ = copy;
        outgoing_ = outgoing;
        outgoing_capacity_ = outgoing_capacity;
        frame_ = frame;
        capacity_ = capacity;
        candidate_ = &candidate;
        if (!io_.emplace<SdIndexGet>(volume_).begin(root_, 13, {key_.data(), key_.size()}, frame_, capacity_)) return false;
        result_ = IndexedCommitStep::Working;
        return true;
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
        return std::get<SdIndexedCommit>(io_).committed(out);
    }
    IndexedCommitStep step()
    {
        using namespace ::geocaching::storage;
        if (result_ != IndexedCommitStep::Working) return result_;
        if (phase_ == Phase::Commit) return result_ = std::get<SdIndexedCommit>(io_).step();
        auto& read = std::get<SdIndexGet>(io_);
        const auto status = read.step();
        if (status == IndexGetStep::Working) return result_;
        if (status != IndexGetStep::Ready)
            return fail(status == IndexGetStep::VolumeChanged ? IndexedCommitStep::VolumeChanged : status == IndexGetStep::IoError ? IndexedCommitStep::IoError
                                                                                                                                   : IndexedCommitStep::Invalid);
        if (phase_ == Phase::Attempt)
        {
            TxAttemptView attempt;
            const ::geocaching::ByteView key{key_.data(), key_.size()};
            if (!decodeTxAttempt(key, read.value(), attempt)) return fail(IndexedCommitStep::Invalid);
            const auto transition = hash_update_ ? recordAttemptTransportHash(attempt, {hash_.data(), hash_.size()}) : finishAttemptTransport(attempt, terminal_, finished_);
            if (transition == AttemptTransition::Rejected) return fail(IndexedCommitStep::Invalid);
            duplicate_ = transition == AttemptTransition::Unchanged;
            if (duplicate_ && hash_update_) return fail(IndexedCommitStep::Verified);
            size_t size = 0;
            if (!encodeTxAttempt(key, attempt, encoded_.data(), encoded_.size(), size)) return fail(IndexedCommitStep::Invalid);
            mutations_[0] = {13, key, {encoded_.data(), size}, false};
            if (hash_update_) return commit(1);
            if (!io_.emplace<SdIndexGet>(volume_).begin(root_, 5, {key_.data(), 48}, frame_, capacity_)) return fail(IndexedCommitStep::Invalid);
            phase_ = Phase::Outgoing;
            return result_;
        }
        OutgoingView outgoing;
        const ::geocaching::ByteView key{key_.data(), 48};
        if (!decodeOutgoing(key, read.value(), outgoing)) return fail(IndexedCommitStep::Invalid);
        if (duplicate_) return fail(IndexedCommitStep::Verified);
        if (outgoing.state < 4) outgoing.state = terminal_ == TxAttemptState::Delivered ? 2 : 3;
        size_t size = 0;
        if (!encodeOutgoing(key, outgoing, outgoing_, outgoing_capacity_, size)) return fail(IndexedCommitStep::Invalid);
        mutations_[1] = {5, key, {outgoing_, size}, false};
        return commit(2);
    }

  private:
    enum class Phase : uint8_t
    {
        Attempt,
        Outgoing,
        Commit
    };
    IndexedCommitStep commit(size_t count)
    {
        if (!io_.emplace<SdIndexedCommit>(volume_).begin(root_, copy_, mutations_.data(), count, frame_, capacity_, *candidate_))
            return fail(IndexedCommitStep::Invalid);
        phase_ = Phase::Commit;
        return result_;
    }
    IndexedCommitStep fail(IndexedCommitStep status)
    {
        io_.emplace<std::monostate>();
        return result_ = status;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootView root_;
    ::geocaching::storage::StoredTime finished_;
    ::geocaching::storage::TxAttemptState terminal_ = ::geocaching::storage::TxAttemptState::Failed;
    std::array<::geocaching::storage::MutationView, 2> mutations_{};
    std::array<uint8_t, 64> key_{};
    std::array<uint8_t, 32> hash_{};
    std::array<uint8_t, 192> encoded_{};
    ::geocaching::storage::IndexRootBytes* candidate_ = nullptr;
    uint8_t *frame_ = nullptr, *outgoing_ = nullptr;
    size_t capacity_ = 0, outgoing_capacity_ = 0;
    unsigned copy_ = 0;
    bool hash_update_ = false, duplicate_ = false;
    std::variant<std::monostate, SdIndexGet, SdIndexedCommit> io_;
    Phase phase_ = Phase::Attempt;
    IndexedCommitStep result_ = IndexedCommitStep::Idle;
};
static_assert(sizeof(SdIndexedAttemptUpdate) <= 1664, "Attempt updates own no request or response payload");
} // namespace platform::esp::arduino_common::geocaching
