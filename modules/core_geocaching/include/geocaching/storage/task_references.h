#pragma once
#include "geocaching/storage/logical_state.h"
#include "geocaching/storage/task_record.h"

namespace geocaching::storage
{
// Owns only reference keys. The source row may be released before the owner
// asynchronously loads each referenced row into the same read workspace.
class TaskReferenceCheck
{
  public:
    bool begin(const MutationView& entry)
    {
        valid_ = false;
        position_ = count_ = 0;
        table_ = entry.table;
        if (entry.erase) return false;
        if (table_ == 5)
        {
            OutgoingView outgoing;
            if (!decodeOutgoing(entry.key, entry.value, outgoing)) return false;
            std::memcpy(task_.data(), outgoing.task_id.data, task_.size());
            std::memcpy(requests_[0].data(), entry.key.data, requests_[0].size());
            count_ = 1;
        }
        else if (table_ == 10)
        {
            TaskView task;
            if (!decodeTask(entry.key, entry.value, task)) return false;
            std::memcpy(task_.data(), entry.key.data, task_.size());
            count_ = task.request_count;
            for (size_t i = 0; i < count_; ++i)
                std::memcpy(requests_[i].data(), task.requests[i].data, requests_[i].size());
        }
        valid_ = true;
        return true;
    }

    bool next(uint8_t& table, ByteView& key) const
    {
        if (!valid_ || position_ == count_) return false;
        table = table_ == 5 ? 10 : 5;
        key = table_ == 5 ? ByteView{task_.data(), task_.size()}
                          : ByteView{requests_[position_].data(), requests_[position_].size()};
        return true;
    }

    // A missing row or I/O error must abort validation; neither is acceptance.
    bool accept(ByteView value)
    {
        if (!valid_ || position_ == count_) return false;
        bool matches = false;
        if (table_ == 5)
        {
            TaskView task;
            if (decodeTask({task_.data(), task_.size()}, value, task))
                for (size_t i = 0; i < task.request_count; ++i)
                    matches |= std::memcmp(task.requests[i].data, requests_[0].data(), 48) == 0;
        }
        else
        {
            OutgoingView outgoing;
            matches = decodeOutgoing({requests_[position_].data(), 48}, value, outgoing) &&
                      std::memcmp(outgoing.task_id.data, task_.data(), 16) == 0;
        }
        if (!matches) return valid_ = false;
        ++position_;
        return true;
    }

    bool complete() const { return valid_ && position_ == count_; }

  private:
    std::array<uint8_t, 16> task_{};
    std::array<std::array<uint8_t, 48>, 3> requests_{};
    size_t position_ = 0, count_ = 0;
    uint8_t table_ = 0;
    bool valid_ = false;
};
static_assert(sizeof(TaskReferenceCheck) <= 192, "Reference validation must not retain row payloads");

// Checks the resulting logical state, not mutations individually: creating or
// deleting a task and its requests together is valid. Other tables require
// their own schema/reference checks before the owner commits the candidate.
inline bool validateTaskReferences(const LogicalState::View& state)
{
    size_t cursor = 0;
    MutationView entry;
    while (state.next(cursor, entry))
    {
        TaskReferenceCheck check;
        if (!check.begin(entry)) return false;
        uint8_t table = 0;
        ByteView key;
        while (check.next(table, key))
        {
            ByteView value;
            if (!state.find(table, key, value) || !check.accept(value)) return false;
        }
        if (!check.complete()) return false;
    }
    return true;
}
} // namespace geocaching::storage
