#include "platform/esp/common/storage/storage_maintenance_state_machine.h"
#include <cassert>
#include <iostream>

using namespace platform::esp::common::storage;
using Adapter = ISemanticStorageAdapter;
using Operation = StorageOperation;
using OperationGeneration = StorageOperationGeneration;
using Result = StorageOperationResult;
using ResultKind = StorageOperationResultKind;

namespace geocaching::browse_runtime
{
bool pending = true;
unsigned steps = 0;
bool workPending() { return pending; }
void step() { ++steps; }
} // namespace geocaching::browse_runtime

// Model a backend which retains its logical cursor after a retryable step.
// Advancing the owner generation without finishing this cursor strands it.
struct Backend
{
    bool dirty = false;
    bool busy_once = false;
    bool in_flight = false;
    OperationGeneration held_generation = 0;
    unsigned finishes = 0;
    bool persistencePending() const { return dirty; }
    Result beginMaintenance(Operation operation, OperationGeneration generation)
    {
        if (operation == Operation::Hydrate || !dirty)
            return Result::completedResult(operation, generation);
        if (in_flight && held_generation != generation)
            return Result::failure(ResultKind::StateBusy, operation, generation);
        in_flight = true;
        held_generation = generation;
        return Result::inProgressResult(operation, generation);
    }
    Result stepMaintenance(Operation operation, OperationGeneration generation, const StorageOperationBudget&)
    {
        assert(in_flight && held_generation == generation);
        if (busy_once)
        {
            busy_once = false;
            return Result::failure(ResultKind::StateBusy, operation, generation);
        }
        in_flight = dirty = false;
        ++finishes;
        return Result::completedResult(operation, generation);
    }
    void cancelMaintenance(Operation, OperationGeneration) { in_flight = false; }
};

struct WorkerContext
{
    Backend* chat_store;
    Backend* peer_directory;
};
Result makeResult(Operation operation, OperationGeneration generation, bool ok)
{
    return ok ? Result::completedResult(operation, generation)
              : Result::failure(ResultKind::IoError, operation, generation);
}

// Generated verbatim from storage_runtime.cpp by run_storage_adapter_retry.py.
// Only ESP-specific wiring is excluded; the adapter under test is production code.
#include "storage_adapter_under_test.inc"

void exerciseRetry(bool peer_busy)
{
    Backend chat, peers;
    Backend& blocked = peer_busy ? peers : chat;
    blocked.dirty = blocked.busy_once = true;
    WorkerContext context{&chat, &peers};
    SdMaintenanceAdapter adapter(context);
    StorageMaintenanceStateMachine owner;
    unsigned retries = 0;
    uint32_t clock = 1;
    auto command = owner.arm(clock, StorageStartupGate::Immediate, true);
    geocaching::browse_runtime::steps = 0;
    geocaching::browse_runtime::pending = true;
    for (unsigned turn = 0; turn < 300 && (!blocked.finishes || geocaching::browse_runtime::steps < 2); ++turn)
    {
        if (command.kind != StorageMaintenanceCommandKind::None)
        {
            const auto result = command.kind == StorageMaintenanceCommandKind::Begin
                                    ? adapter.begin(command.operation, command.generation)
                                    : adapter.step(command.operation, command.generation, {});
            if (result.kind == ResultKind::StateBusy) ++retries;
            owner.complete(result, clock);
            // Completing this owner generation must never abandon a backend.
            if (result.completed()) assert(!blocked.in_flight);
        }
        clock += 100;
        command = owner.tick(clock, false, false, true, {true, false});
    }
    assert(retries == 1);
    assert(blocked.finishes == 1 && !blocked.dirty);
    assert(geocaching::browse_runtime::steps >= 2);
}

void exerciseLateDemand()
{
    Backend chat, peers;
    WorkerContext context{&chat, &peers};
    SdMaintenanceAdapter adapter(context);
    geocaching::browse_runtime::pending = true;
    geocaching::browse_runtime::steps = 0;
    assert(adapter.begin(Operation::Persist, 1).inProgress());
    chat.dirty = true;
    assert(adapter.step(Operation::Persist, 1, {}).completed());
    assert(geocaching::browse_runtime::steps == 1);
    assert(adapter.begin(Operation::Persist, 2).inProgress());
    assert(adapter.step(Operation::Persist, 2, {}).completed());
    assert(chat.finishes == 1);
    assert(adapter.begin(Operation::Persist, 3).inProgress());
    const unsigned before = geocaching::browse_runtime::steps;
    StorageOperationBudget zero{};
    zero.max_work_items = 0;
    assert(adapter.step(Operation::Persist, 3, zero).inProgress());
    assert(before == geocaching::browse_runtime::steps);
    for (unsigned i = 0; i < 7; ++i) assert(adapter.step(Operation::Persist, 3, {}).inProgress());
    assert(adapter.step(Operation::Persist, 3, {}).completed());
    assert(geocaching::browse_runtime::steps == before + 8);
}

int main()
{
    exerciseRetry(false);
    exerciseRetry(true);
    exerciseLateDemand();
    std::cout << "Storage adapter retry ownership and fairness passed\n";
}
