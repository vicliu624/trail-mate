#include "ui_presentation/waypoint/waypoint_model.h"
#include <cassert>
#include <cstring>

class Store final : public waypoint::IStore
{
  public:
    unsigned writes = 0;
    bool fail = false;
    waypoint::Record record;
    uint16_t slotCount() const override { return 1; }
    waypoint::Result read(uint16_t, waypoint::Record& out) override
    {
        out = record;
        return waypoint::Result::Ok;
    }
    waypoint::Result create(const waypoint::Record& value, uint32_t& id) override
    {
        ++writes;
        if (fail) return waypoint::Result::IoError;
        record = value;
        record.id = id = 7;
        return waypoint::Result::Ok;
    }
    waypoint::Result update(const waypoint::Record& value) override
    {
        uint32_t id;
        return create(value, id);
    }
    waypoint::Result remove(uint32_t) override
    {
        ++writes;
        record = {};
        return waypoint::Result::Ok;
    }
};
int main()
{
    Store store;
    ui::waypoint::Model model(store);
    waypoint::Record draft;
    std::strcpy(draft.name, "Camp");
    assert(model.save(draft).failure == ui::UiActionFailure::NotReady);
    model.setReady(true);
    assert(model.save(draft).ok && store.writes == 0);
    auto sequence = model.commandResult().sequence;
    assert(model.save(draft).failure == ui::UiActionFailure::Busy);
    assert(model.commandResult().sequence == sequence);
    model.pump();
    assert(store.writes == 1 && model.commandResult().id == 7);
    assert(model.commandResult().state == ui::waypoint::CommandState::Succeeded);
    model.pump();
    assert(store.writes == 1);
    waypoint::Page page;
    assert(model.page(0, 5, page) == waypoint::Result::Ok && page.count == 1);
    store.fail = true;
    assert(model.save(draft).ok);
    model.pump();
    assert(model.commandResult().state == ui::waypoint::CommandState::Failed && !model.commandResult().id);
    assert(model.commandResult().sequence != sequence);
    assert(model.remove(7).ok);
    model.pump();
    assert(model.page(0, 5, page) == waypoint::Result::Ok && !page.count);
    assert(model.save(draft).ok);
    model.setReady(false);
    const auto writes = store.writes;
    assert(model.commandResult().state == ui::waypoint::CommandState::Failed);
    assert(model.page(0, 5, page) == waypoint::Result::IoError && !page.count);
    model.setReady(true); // Reinsert before the next pump: never replay the save.
    model.pump();
    assert(store.writes == writes && model.commandResult().state == ui::waypoint::CommandState::Failed);
    assert(model.remove(7).ok);
    model.setReady(false);
    model.setReady(true);
    model.pump();
    assert(store.writes == writes && model.commandResult().state == ui::waypoint::CommandState::Failed);
}
