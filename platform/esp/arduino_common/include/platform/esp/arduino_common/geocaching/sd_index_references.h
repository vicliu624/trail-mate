#pragma once
#include "geocaching/storage/task_references.h"
#include "geocaching/storage/tx_attempt.h"
#include "platform/esp/arduino_common/geocaching/sd_index_get.h"
#include "platform/esp/arduino_common/geocaching/sd_index_scan.h"
#include <optional>

namespace platform::esp::arduino_common::geocaching
{
// Pins one immutable root and one frame lease. The owner must not publish a
// new root during validation. Allocate in owner storage, never on an ESP stack.
class SdIndexReferences
{
  public:
    explicit SdIndexReferences(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}

    // Pending mutations are borrowed and immutable until completion. Their
    // storage must be disjoint from the reusable read frame.
    bool begin(const ::geocaching::storage::IndexRootView& root, uint8_t* frame, size_t capacity,
               const ::geocaching::storage::MutationView* mutations = nullptr, size_t count = 0)
    {
        if (result_ != IndexScanStep::Idle || count > 64 || (count && !mutations)) return false;
        auto overlaps = [&](const void* data, size_t size)
        {
            if (!size) return false;
            const auto a = reinterpret_cast<uintptr_t>(data), b = reinterpret_cast<uintptr_t>(frame);
            return a <= b ? b - a < size : a - b < capacity;
        };
        if (overlaps(mutations, count * sizeof(*mutations))) return false;
        for (size_t i = 0; i < count; ++i)
        {
            const auto& row = mutations[i];
            if (!::geocaching::storage::validStoredRowShape(row) || overlaps(row.key.data, row.key.size) ||
                overlaps(row.value.data, row.value.size)) return false;
            for (size_t j = 0; j < i; ++j)
                if (mutations[j].table == row.table && mutations[j].key.size == row.key.size &&
                    !std::memcmp(mutations[j].key.data, row.key.data, row.key.size)) return false;
        }
        if (!scan_.emplace(volume_).begin(root, 5, frame, capacity)) return false;
        mutations_ = mutations;
        mutation_count_ = count;
        root_ = root;
        frame_ = frame;
        capacity_ = capacity;
        result_ = IndexScanStep::Working;
        return true;
    }

    // End means every live task/outgoing/attempt reference was checked.
    IndexScanStep step()
    {
        using namespace ::geocaching::storage;
        if (result_ != IndexScanStep::Working) return result_;
        if (get_ || overlay_ready_)
        {
            const auto status = overlay_ready_ ? IndexGetStep::Ready : get_->step();
            if (status == IndexGetStep::Working) return result_;
            if (status != IndexGetStep::Ready)
                return finish(status == IndexGetStep::IoError ? IndexScanStep::IoError : status == IndexGetStep::VolumeChanged   ? IndexScanStep::VolumeChanged
                                                                                     : status == IndexGetStep::WorkspaceTooSmall ? IndexScanStep::WorkspaceTooSmall
                                                                                                                                 : IndexScanStep::Invalid);
            const auto value = overlay_ready_ ? overlay_value_ : get_->value();
            overlay_ready_ = false;
            if (table_ == 13)
            {
                OutgoingView outgoing;
                if (!decodeOutgoing({attempt_key_.data(), attempt_key_.size()}, value, outgoing))
                    return finish(IndexScanStep::Invalid);
                get_.reset();
                return result_;
            }
            uint8_t target_table = 0;
            ::geocaching::ByteView target_key;
            if (!references_.next(target_table, target_key) || !references_.accept(value)) return finish(IndexScanStep::Invalid);
            remember(table_ == 5 ? target_key : ::geocaching::ByteView{attempt_key_.data(), 16},
                     table_ == 5 ? ::geocaching::ByteView{attempt_key_.data(), 48} : target_key);
            get_.reset();
            return startReference();
        }

        MutationView row;
        if (scanned_)
        {
            while (mutation_position_ < mutation_count_)
            {
                row = mutations_[mutation_position_++];
                if (!row.erase && (row.table == 5 || row.table == 10 || row.table == 13))
                {
                    table_ = row.table;
                    return checkRow(row);
                }
            }
            return finish(IndexScanStep::End);
        }
        const auto status = scan_->step();
        if (status == IndexScanStep::Working) return result_;
        if (status == IndexScanStep::End)
        {
            if (table_ == 13)
            {
                scanned_ = true;
                scan_.reset();
                return result_;
            }
            table_ = table_ == 5 ? 10 : 13;
            if (!scan_.emplace(volume_).begin(root_, table_, frame_, capacity_)) return finish(IndexScanStep::Invalid);
            return result_;
        }
        if (status != IndexScanStep::Item) return finish(status);
        if (!scan_->item(row)) return finish(IndexScanStep::Invalid);
        const bool replaced = replacement(row.table, row.key) != nullptr;
        if (!replaced && checkRow(row) != IndexScanStep::Working) return result_;
        if (!scan_->advance()) return finish(IndexScanStep::Invalid);
        return result_;
    }

  private:
    const ::geocaching::storage::MutationView* replacement(uint8_t table, ::geocaching::ByteView key) const
    {
        for (size_t i = 0; i < mutation_count_; ++i)
            if (mutations_[i].table == table && mutations_[i].key.size == key.size &&
                !std::memcmp(mutations_[i].key.data, key.data, key.size)) return &mutations_[i];
        return nullptr;
    }
    IndexScanStep loadReference(uint8_t table, ::geocaching::ByteView key)
    {
        if (const auto* row = replacement(table, key))
        {
            if (row->erase) return finish(IndexScanStep::Invalid);
            overlay_value_ = row->value;
            overlay_ready_ = true;
        }
        else if (!get_.emplace(volume_).begin(root_, table, key, frame_, capacity_)) return finish(IndexScanStep::Invalid);
        return result_;
    }
    IndexScanStep checkRow(const ::geocaching::storage::MutationView& row)
    {
        using namespace ::geocaching::storage;
        if (table_ == 13)
        {
            TxAttemptView attempt;
            if (!decodeTxAttempt(row.key, row.value, attempt)) return finish(IndexScanStep::Invalid);
            if (known({}, attempt.request_key)) return result_;
            std::memcpy(attempt_key_.data(), attempt.request_key.data, attempt_key_.size());
            return loadReference(5, {attempt_key_.data(), attempt_key_.size()});
        }
        // Every row still passes the normal scan/frame/shape checks. Only the
        // repeated relationship lookup is elided, within this pinned root and
        // immutable mutation overlay. Eviction merely causes another lookup.
        if (table_ == 5)
        {
            OutgoingView outgoing;
            if (!decodeOutgoing(row.key, row.value, outgoing)) return finish(IndexScanStep::Invalid);
            if (known(outgoing.task_id, row.key)) return result_;
        }
        else if (table_ == 10)
        {
            TaskView task;
            if (!decodeTask(row.key, row.value, task)) return finish(IndexScanStep::Invalid);
            bool complete = true;
            for (size_t i = 0; i < task.request_count; ++i) complete &= known(row.key, task.requests[i]);
            if (complete) return result_;
        }
        std::memcpy(attempt_key_.data(), row.key.data, row.key.size);
        if (!references_.begin(row)) return finish(IndexScanStep::Invalid);
        return startReference();
    }

    bool known(::geocaching::ByteView task, ::geocaching::ByteView request) const
    {
        for (size_t i = 0; i < proof_count_; ++i)
            if ((!task.size || (task.size == 16 && !std::memcmp(proofs_[i].data(), task.data, 16))) &&
                request.size == 48 && !std::memcmp(proofs_[i].data() + 16, request.data, 48)) return true;
        return false;
    }
    void remember(::geocaching::ByteView task, ::geocaching::ByteView request)
    {
        if (known(task, request)) return;
        auto& proof = proofs_[proof_next_];
        std::memcpy(proof.data(), task.data, 16);
        std::memcpy(proof.data() + 16, request.data, 48);
        proof_next_ = (proof_next_ + 1) % proofs_.size();
        if (proof_count_ < proofs_.size()) ++proof_count_;
    }

    IndexScanStep startReference()
    {
        uint8_t table = 0;
        ::geocaching::ByteView key;
        if (!references_.next(table, key))
            return references_.complete() ? result_ : finish(IndexScanStep::Invalid);
        return loadReference(table, key);
    }
    IndexScanStep finish(IndexScanStep status)
    {
        get_.reset();
        scan_.reset();
        return result_ = status;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootView root_;
    std::optional<SdIndexScan> scan_;
    std::optional<SdIndexGet> get_;
    ::geocaching::storage::TaskReferenceCheck references_;
    std::array<uint8_t, 48> attempt_key_{};
    std::array<std::array<uint8_t, 64>, 4> proofs_{};
    uint8_t proof_count_ = 0, proof_next_ = 0;
    uint8_t* frame_ = nullptr;
    size_t capacity_ = 0;
    const ::geocaching::storage::MutationView* mutations_ = nullptr;
    size_t mutation_count_ = 0, mutation_position_ = 0;
    ::geocaching::ByteView overlay_value_;
    bool overlay_ready_ = false, scanned_ = false;
    uint8_t table_ = 5;
    IndexScanStep result_ = IndexScanStep::Idle;
};
static_assert(sizeof(SdIndexReferences) <= 2048, "Reference recovery keeps only four proven relationships and an external frame");
} // namespace platform::esp::arduino_common::geocaching
