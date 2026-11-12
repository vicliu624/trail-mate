#include "app/app_context.h"
#include "platform/esp/arduino_common/app_context_platform_bindings.h"
#include "platform/esp/arduino_common/app_event_runtime_support.h"
#include "platform/esp/arduino_common/app_runtime_bootstrap_support.h"
#include "platform/esp/common/storage/storage_maintenance_owner.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <stdexcept>
#include <vector>

class BoardBase
{
};
class LoraBoard
{
};
namespace storage = platform::esp::common::storage;
namespace runtime = platform::esp::arduino_common;
namespace
{
struct QueueBlocked
{
};
struct Queue
{
    size_t capacity, item_size;
    std::deque<std::vector<uint8_t>> items;
};
struct Adapter : storage::ISemanticStorageAdapter
{
    unsigned reads = 0;
    storage::StorageOperationResult begin(storage::StorageOperation operation, storage::StorageOperationGeneration generation) override
    {
        ++reads;
        return storage::StorageOperationResult::completedResult(operation, generation);
    }
    storage::StorageOperationResult step(storage::StorageOperation operation, storage::StorageOperationGeneration generation, const storage::StorageOperationBudget&) override
    {
        return begin(operation, generation);
    }
    void cancelAtStepBoundary(storage::StorageOperation, storage::StorageOperationGeneration) override {}
};
struct Fixture
{
    size_t free_bytes = 72U * 1024U;
    size_t ble_bytes = 24U * 1024U, radio_bytes = 16U * 1024U;
    uint32_t now = 1;
    bool init_ok = true;
    unsigned task_count = 0, prepare_count = 0, ble_count = 0;
    TaskFunction_t task_entry = nullptr;
    void* task_argument = nullptr;
    std::vector<std::unique_ptr<Queue>> queues;
    Adapter adapter;
    storage::StorageMaintenanceOwner owner;

    Fixture()
    {
        storage::StorageMaintenanceOwnerConfig config;
        config.adapter = &adapter;
        config.context = this;
        config.startup_gate = storage::StorageStartupGate::DisplayTransaction;
        config.now = [](void* context)
        { return static_cast<Fixture*>(context)->now; };
        config.admit = [](void* context)
        {
            // The real Arduino admission requires an 8 KiB worker plus its
            // 40 KiB internal floor. Host bookkeeping exercises this boundary.
            return static_cast<Fixture*>(context)->free_bytes >= 48U * 1024U;
        };
        owner.configure(config);
    }
    void drain()
    {
        assert(task_entry);
        try
        {
            task_entry(task_argument);
        }
        catch (const QueueBlocked&)
        {
        }
    }
};
Fixture* current = nullptr;

bool cold_boot_reserves_before_optional_services()
{
    Fixture fixture;
    current = &fixture;
    app::AppContext app;
    BoardBase board;
    LoraBoard radio;
    runtime::AppContextBootstrapResult result;
    assert(runtime::bootstrapAppContext(app, {&board, &radio}, false, &result));
    assert(result.app_context_bound && result.background_tasks == runtime::BackgroundTaskStartResult::Started);
    if (fixture.task_count != 1)
    {
        // Reproduce the original ordering: arm after BLE, radio and shell
        // allocation. The admission floor prevents any recovery task running.
        fixture.owner.arm(fixture.now, false);
        assert(fixture.task_count == 0 && fixture.owner.initialHydrationPending());
        std::fprintf(stderr, "FAIL: late storage admission leaves Contacts waiting; internal_free=%zu\n", fixture.free_bytes);
        return false;
    }
    assert(fixture.prepare_count == 1 && fixture.ble_count == 1);
    assert(!fixture.owner.isArmed() && fixture.adapter.reads == 0);
    assert(fixture.queues.size() == 1 && fixture.queues.front()->items.empty());
    assert(fixture.owner.arm(fixture.now, false));
    fixture.drain();
    assert(fixture.owner.initialHydrationPending() && fixture.adapter.reads == 0);
    fixture.now = 100;
    fixture.owner.submitTick(fixture.now, false, false, true);
    fixture.drain();
    assert(fixture.owner.initialHydrationReady() && !fixture.owner.initialHydrationPending());
    assert(fixture.adapter.reads == 1 && fixture.task_count == 1 && fixture.queues.size() == 1);
    return true;
}

bool denied_reservation_keeps_existing_retry()
{
    Fixture fixture;
    current = &fixture;
    fixture.free_bytes = 32U * 1024U;
    fixture.ble_bytes = fixture.radio_bytes = 0;
    app::AppContext app;
    BoardBase board;
    assert(runtime::bootstrapAppContext(app, {&board}, false));
    assert(fixture.task_count == 0);
    fixture.owner.arm(fixture.now, false);
    fixture.free_bytes = 64U * 1024U;
    fixture.now = 3000;
    fixture.owner.submitTick(fixture.now, false, false, true);
    fixture.drain();
    assert(fixture.owner.initialHydrationReady() && fixture.task_count == 1 && fixture.queues.size() == 1);
    return true;
}

bool failed_context_has_no_storage_side_effects()
{
    Fixture fixture;
    current = &fixture;
    fixture.init_ok = false;
    app::AppContext app;
    BoardBase board;
    assert(!runtime::bootstrapAppContext(app, {&board}, false));
    assert(fixture.task_count == 0 && fixture.prepare_count == 0 && fixture.queues.empty());
    return true;
}
} // namespace

QueueHandle_t xQueueCreate(UBaseType_t capacity, UBaseType_t item_size)
{
    auto queue = std::make_unique<Queue>();
    queue->capacity = capacity;
    queue->item_size = item_size;
    current->free_bytes -= capacity * item_size + 64U;
    current->queues.push_back(std::move(queue));
    return current->queues.back().get();
}
BaseType_t xQueueSend(QueueHandle_t handle, const void* item, TickType_t)
{
    auto& queue = *static_cast<Queue*>(handle);
    if (queue.items.size() == queue.capacity) return 0;
    const auto* bytes = static_cast<const uint8_t*>(item);
    queue.items.emplace_back(bytes, bytes + queue.item_size);
    return pdPASS;
}
BaseType_t xQueueReceive(QueueHandle_t handle, void* item, TickType_t)
{
    auto& queue = *static_cast<Queue*>(handle);
    if (queue.items.empty()) throw QueueBlocked{};
    std::memcpy(item, queue.items.front().data(), queue.item_size);
    queue.items.pop_front();
    return pdPASS;
}
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t entry, const char*, uint32_t stack_bytes,
                                   void* argument, UBaseType_t, TaskHandle_t* handle, BaseType_t)
{
    assert(current->free_bytes >= stack_bytes);
    current->free_bytes -= stack_bytes;
    ++current->task_count;
    current->task_entry = entry;
    current->task_argument = argument;
    *handle = current;
    return pdPASS;
}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 1024; }

namespace app
{
bool AppContext::init(BoardBase&, LoraBoard*, GpsBoard*, MotionBoard*, bool) { return current->init_ok; }
void bindAppFacade(AppContext&) {}
void unbindAppFacade() {}
} // namespace app
namespace platform::esp::arduino_common
{
AppContextPlatformBindings makeAppContextPlatformBindings() { return {}; }
AppEventRuntimeHooks makeAppEventRuntimeHooks() { return {}; }
std::unique_ptr<ble::BleManager> createBleManager(app::IAppBleFacade&)
{
    ++current->ble_count;
    current->free_bytes -= current->ble_bytes;
    return std::make_unique<ble::BleManager>();
}
BackgroundTaskStartResult startBackgroundTasks(LoraBoard*, chat::IMeshAdapter*)
{
    current->free_bytes -= current->radio_bytes;
    return BackgroundTaskStartResult::Started;
}
namespace storage
{
bool prepare_deferred_storage()
{
    ++current->prepare_count;
    return current->owner.prepareTask();
}
} // namespace storage
} // namespace platform::esp::arduino_common
int main()
{
    const bool cold = cold_boot_reserves_before_optional_services();
    const bool retry = denied_reservation_keeps_existing_retry();
    const bool failure = failed_context_has_no_storage_side_effects();
    std::printf("storage startup: cold=%d retry=%d init_failure=%d\n", cold, retry, failure);
    return cold && retry && failure ? 0 : 1;
}
