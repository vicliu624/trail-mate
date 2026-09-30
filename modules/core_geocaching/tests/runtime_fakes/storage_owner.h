#pragma once
#include "platform/esp/arduino_common/geocaching/browse_runtime.h"
#include "platform/esp/common/storage/storage_maintenance_state_machine.h"

namespace runtime_test::maintenance
{
using namespace ::platform::esp::common::storage;
using Adapter = ISemanticStorageAdapter;
using Operation = StorageOperation;
using OperationGeneration = StorageOperationGeneration;
using Result = StorageOperationResult;
using ResultKind = StorageOperationResultKind;
namespace geocaching
{
namespace browse_runtime = ::platform::esp::arduino_common::geocaching::browse_runtime;
}

// Chat and contacts are idle here. Their retry/fairness cases are covered by
// run_storage_adapter_retry.py; Geocaching below is the real device session.
struct IdleBackend
{
    bool persistencePending() const { return false; }
    Result beginMaintenance(Operation operation, OperationGeneration generation)
    {
        return Result::completedResult(operation, generation);
    }
    Result stepMaintenance(Operation operation, OperationGeneration generation, const StorageOperationBudget&)
    {
        return Result::completedResult(operation, generation);
    }
    void cancelMaintenance(Operation, OperationGeneration) {}
};
struct WorkerContext
{
    IdleBackend* chat_store = nullptr;
    IdleBackend* peer_directory = nullptr;
};
inline Result makeResult(Operation operation, OperationGeneration generation, bool ok)
{
    return ok ? Result::completedResult(operation, generation) : Result::failure(ResultKind::IoError, operation, generation);
}

// CMake extracts the complete production class, without rewriting its logic.
#include "runtime_storage_adapter.inc"

inline uint64_t begins = 0, slices = 0;
inline void tick(uint32_t now_ms)
{
    static WorkerContext context;
    static SdMaintenanceAdapter adapter(context);
    static StorageMaintenanceStateMachine owner;
    static bool armed = false;
    const auto command = !armed ? owner.arm(now_ms, StorageStartupGate::Immediate, true)
                                : owner.tick(now_ms, false, false, true, {geocaching::browse_runtime::workPending(), false});
    armed = true;
    if (command.kind == StorageMaintenanceCommandKind::None) return;
    if (command.kind == StorageMaintenanceCommandKind::Begin) ++begins;
    else ++slices;
    const auto result = command.kind == StorageMaintenanceCommandKind::Begin
                            ? adapter.begin(command.operation, command.generation)
                            : adapter.step(command.operation, command.generation, {});
    owner.complete(result, now_ms);
}
} // namespace runtime_test::maintenance
