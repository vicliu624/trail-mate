#pragma once
#include "ui_presentation/common/ui_action_result.h"
#include "waypoint/waypoint.h"

namespace ui::waypoint
{
enum class CommandState : uint8_t
{
    Idle,
    Pending,
    Succeeded,
    Failed
};
struct CommandResult
{
    uint32_t sequence = 0;
    uint32_t id = 0;
    ::waypoint::Result result = ::waypoint::Result::Ok;
    CommandState state = CommandState::Idle;
};
class ISource
{
  public:
    virtual ~ISource() = default;
    virtual ::waypoint::Result page(uint32_t after, uint8_t capacity, ::waypoint::Page& out) = 0;
    virtual ::waypoint::Result detail(uint32_t id, ::waypoint::Record& out) = 0;
    virtual CommandResult commandResult() const = 0;
};
class IActionSink
{
  public:
    virtual ~IActionSink() = default;
    // Success acknowledges enqueueing only. Wait for the command sequence to
    // finish before closing an editor or applying a newly created waypoint.
    virtual UiActionResult save(const ::waypoint::Record& record) = 0;
    virtual UiActionResult remove(uint32_t id) = 0;
};
// Root-owned, renderer-independent, one pending operation. No list cache.
class Model final : public ISource, public IActionSink
{
  public:
    explicit Model(::waypoint::IStore& store) : store_(store) {}
    void setReady(bool ready)
    {
        ready_ = ready;
        if (!ready)
        {
            // A pending write belongs to the old media session, not to the
            // next card that happens to become available before pump().
            if (result_.state == CommandState::Pending)
            {
                result_.state = CommandState::Failed;
                result_.result = ::waypoint::Result::IoError;
                result_.id = 0;
            }
            pending_ = {};
            deleting_ = false;
        }
    }
    void pump()
    {
        if (result_.state != CommandState::Pending) return;
        uint32_t id = pending_.id;
        result_.result = !ready_     ? ::waypoint::Result::IoError
                         : deleting_ ? store_.remove(id)
                         : id        ? store_.update(pending_)
                                     : store_.create(pending_, id);
        result_.state = result_.result == ::waypoint::Result::Ok ? CommandState::Succeeded : CommandState::Failed;
        result_.id = result_.state == CommandState::Succeeded ? id : 0;
        pending_ = {};
    }
    ::waypoint::Result page(uint32_t after, uint8_t capacity, ::waypoint::Page& out) override
    {
        out = {};
        return ready_ ? ::waypoint::query(store_, after, capacity, out) : ::waypoint::Result::IoError;
    }
    ::waypoint::Result detail(uint32_t id, ::waypoint::Record& out) override
    {
        out = {};
        return ready_ ? ::waypoint::find(store_, id, out) : ::waypoint::Result::IoError;
    }
    CommandResult commandResult() const override { return result_; }
    UiActionResult save(const ::waypoint::Record& record) override
    {
        auto validation = record;
        if (!validation.id) validation.id = 1;
        if (!::waypoint::valid(validation)) return UiActionResult::fail(UiActionFailure::InvalidInput);
        return enqueue(record, false);
    }
    UiActionResult remove(uint32_t id) override
    {
        if (!id) return UiActionResult::fail(UiActionFailure::InvalidInput);
        ::waypoint::Record record;
        record.id = id;
        return enqueue(record, true);
    }

  private:
    UiActionResult enqueue(const ::waypoint::Record& record, bool deleting)
    {
        if (!ready_) return UiActionResult::fail(UiActionFailure::NotReady);
        if (result_.state == CommandState::Pending) return UiActionResult::fail(UiActionFailure::Busy);
        pending_ = record;
        deleting_ = deleting;
        if (!++result_.sequence) ++result_.sequence;
        result_.id = 0;
        result_.result = ::waypoint::Result::Ok;
        result_.state = CommandState::Pending;
        return UiActionResult::success();
    }
    ::waypoint::IStore& store_;
    ::waypoint::Record pending_{};
    CommandResult result_{};
    bool ready_ = false;
    bool deleting_ = false;
};
static_assert(sizeof(Model) < 96, "Waypoint model must remain bounded");
} // namespace ui::waypoint
