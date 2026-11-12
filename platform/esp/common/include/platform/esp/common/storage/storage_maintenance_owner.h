#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "platform/esp/common/storage/storage_maintenance_state_machine.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace platform::esp::common::storage
{

using StorageMaintenanceAdmitFn = bool (*)(void* context);
using StorageMaintenanceNowFn = uint32_t (*)(void* context);
using StorageMaintenanceStartedFn = void (*)(
    void* context,
    StorageOperation operation,
    StorageOperationGeneration generation);
using StorageMaintenanceFinishedFn = void (*)(
    void* context,
    StorageOperation operation,
    StorageOperationGeneration generation,
    StorageOperationResultKind result,
    uint32_t elapsed_ms,
    uint32_t stack_free_bytes);

struct StorageMaintenanceOwnerConfig
{
    const char* task_name = "storage_owner";
    uint32_t stack_bytes = 8U * 1024U;
    UBaseType_t priority = 1U;
    BaseType_t core = 1;
    StorageStartupGate startup_gate = StorageStartupGate::Immediate;
    StorageOperationBudget step_budget{};
    void* context = nullptr;
    ISemanticStorageAdapter* adapter = nullptr;
    StorageMaintenanceAdmitFn admit = nullptr;
    StorageMaintenanceNowFn now = nullptr;
    StorageMaintenanceStartedFn on_started = nullptr;
    StorageMaintenanceFinishedFn on_finished = nullptr;
};

// A stable active object for maintenance. Its task is created once and stays
// blocked on the command queue after maintenance reaches Done. Business state
// is owned by the task-local state machine; callers only enqueue events and
// read the published snapshot.
class StorageMaintenanceOwner final
{
  public:
    static constexpr std::size_t kQueueLength = 8U;

    StorageMaintenanceOwner() = default;

    StorageMaintenanceOwner(const StorageMaintenanceOwner&) = delete;
    StorageMaintenanceOwner& operator=(const StorageMaintenanceOwner&) = delete;

    // Reserve the existing worker while startup still has its memory budget.
    // No command is queued, so storage remains dormant until arm().
    bool prepareTask()
    {
        return ensureTask();
    }

    bool arm(uint32_t now_ms, bool startup_gate_satisfied)
    {
        if (armed_.exchange(true, std::memory_order_acq_rel))
        {
            return false;
        }

        initial_now_ms_ = now_ms;
        initial_gate_satisfied_ = startup_gate_satisfied;
        initial_hydration_ready_.store(false, std::memory_order_release);
        hydration_ready_generation_.store(0U, std::memory_order_release);
        foreground_storage_barrier_.store(
            startup_gate_satisfied ||
                config_.startup_gate == StorageStartupGate::Immediate,
            std::memory_order_release);
        arm_event_pending_.store(true, std::memory_order_release);
        ensureTask();
        enqueuePendingArm();

        if (startup_gate_satisfied &&
            config_.startup_gate == StorageStartupGate::DisplayTransaction)
        {
            const StorageRuntimeSnapshot current = snapshot();
            if (arm_event_pending_.load(std::memory_order_acquire) ||
                current.state == StorageRuntimeState::Dormant ||
                current.state == StorageRuntimeState::WaitingStartupGate ||
                current.pending_operation == StorageOperation::Hydrate)
            {
                foreground_storage_barrier_.store(true,
                                                  std::memory_order_release);
            }
        }
        return true;
    }

    bool submitTick(uint32_t now_ms,
                    bool is_sleeping,
                    bool saver_active,
                    bool startup_gate_satisfied,
                    StorageMaintenanceDemand demand = {})
    {
        if (!armed_.load(std::memory_order_acquire))
        {
            return false;
        }

        latest_tick_now_ms_.store(now_ms, std::memory_order_release);
        latest_is_sleeping_.store(is_sleeping, std::memory_order_release);
        latest_saver_active_.store(saver_active, std::memory_order_release);
        latest_gate_satisfied_.store(startup_gate_satisfied,
                                     std::memory_order_release);
        latest_persistence_pending_.store(
            demand.persistence_pending,
            std::memory_order_release);
        latest_compaction_pending_.store(
            demand.compaction_pending,
            std::memory_order_release);
        latest_tick_generation_.fetch_add(1U, std::memory_order_acq_rel);
        tick_event_pending_.store(true, std::memory_order_release);

        ensureTask();
        enqueuePendingArm();
        enqueuePendingStop();
        return enqueueLatestTick();
    }

    bool requestStop()
    {
        if (!armed_.load(std::memory_order_acquire))
        {
            return false;
        }
        stop_event_pending_.store(true, std::memory_order_release);
        ensureTask();
        enqueuePendingArm();
        return enqueuePendingStop();
    }

    void configure(const StorageMaintenanceOwnerConfig& config)
    {
        config_ = config;
    }

    // The owner is the sole authority for whether its command stream may
    // accept ticks or a new configuration. A queued Stop remains armed until
    // the owner task has cancelled the active operation at its boundary.
    bool isArmed() const
    {
        return armed_.load(std::memory_order_acquire);
    }

    StorageRuntimeSnapshot snapshot() const
    {
        StorageRuntimeSnapshot snapshot{};
        snapshot.state = static_cast<StorageRuntimeState>(
            published_state_.load(std::memory_order_acquire));
        snapshot.active_operation = static_cast<StorageOperation>(
            published_active_operation_.load(std::memory_order_acquire));
        snapshot.pending_operation = static_cast<StorageOperation>(
            published_pending_operation_.load(std::memory_order_acquire));
        snapshot.generation =
            published_generation_.load(std::memory_order_acquire);
        snapshot.retry_attempt =
            published_retry_attempt_.load(std::memory_order_acquire);
        snapshot.retry_due_ms =
            published_retry_due_ms_.load(std::memory_order_acquire);
        snapshot.startup_gate_satisfied =
            published_startup_gate_satisfied_.load(std::memory_order_acquire);
        return snapshot;
    }

    bool consumeHydrationReady(StorageOperationGeneration* generation = nullptr)
    {
        const StorageOperationGeneration ready_generation =
            hydration_ready_generation_.exchange(0U, std::memory_order_acq_rel);
        if (generation)
        {
            *generation = ready_generation;
        }
        return ready_generation != 0U;
    }

    // Unlike consumeHydrationReady(), this is a durable state. Consumers that
    // start after the one-shot edge was consumed can still safely begin their
    // own local restore without racing the authoritative hydration owner.
    bool initialHydrationReady() const
    {
        return initial_hydration_ready_.load(std::memory_order_acquire);
    }

    bool hydrationActive() const
    {
        return foreground_storage_barrier_.load(std::memory_order_acquire);
    }

    // Covers the arm-to-owner handoff as well as the display gate, active
    // hydration, and hydration retry backoff. Optional interactive reads use
    // this semantic state so they cannot preempt the first hydration attempt.
    bool initialHydrationPending() const
    {
        if (!armed_.load(std::memory_order_acquire))
        {
            return false;
        }

        const StorageRuntimeSnapshot current = snapshot();
        return current.state == StorageRuntimeState::Dormant ||
               isInitialHydrationPending(current);
    }

  private:
    static constexpr uint32_t kTaskRetryDelayMs = 2000U;

    enum class EventKind : uint8_t
    {
        Arm,
        Tick,
        Stop,
    };

    struct Event
    {
        EventKind kind = EventKind::Tick;
        uint32_t now_ms = 0U;
        bool is_sleeping = false;
        bool saver_active = false;
        bool startup_gate_satisfied = false;
        uint32_t tick_generation = 0U;
    };

    static void taskEntry(void* arg)
    {
        static_cast<StorageMaintenanceOwner*>(arg)->taskLoop();
    }

    bool ensureTask()
    {
        const uint32_t now_ms =
            config_.now ? config_.now(config_.context) : 0U;
        if (task_retry_due_ms_ != 0U &&
            static_cast<int32_t>(now_ms - task_retry_due_ms_) < 0)
        {
            return false;
        }

        if (queue_ == nullptr)
        {
            queue_ = xQueueCreate(kQueueLength, sizeof(Event));
            if (queue_ == nullptr)
            {
                task_retry_due_ms_ = now_ms + kTaskRetryDelayMs;
                return false;
            }
        }
        if (task_ != nullptr)
        {
            return true;
        }
        if (config_.admit && !config_.admit(config_.context))
        {
            task_retry_due_ms_ = now_ms + kTaskRetryDelayMs;
            return false;
        }

        if (xTaskCreatePinnedToCore(&taskEntry,
                                    config_.task_name,
                                    config_.stack_bytes,
                                    this,
                                    config_.priority,
                                    &task_,
                                    config_.core) != pdPASS)
        {
            task_ = nullptr;
            task_retry_due_ms_ = now_ms + kTaskRetryDelayMs;
            return false;
        }
        task_retry_due_ms_ = 0U;
        return true;
    }

    void enqueuePendingArm()
    {
        if (!queue_)
        {
            return;
        }

        bool expected = true;
        if (!arm_event_pending_.compare_exchange_strong(
                expected,
                false,
                std::memory_order_acq_rel))
        {
            return;
        }

        Event event{};
        event.kind = EventKind::Arm;
        event.now_ms = initial_now_ms_;
        event.startup_gate_satisfied = initial_gate_satisfied_;
        if (xQueueSend(queue_, &event, 0U) != pdPASS)
        {
            arm_event_pending_.store(true, std::memory_order_release);
        }
    }

    bool enqueueLatestTick()
    {
        if (!tick_event_pending_.load(std::memory_order_acquire) || !queue_)
        {
            return false;
        }
        bool expected = false;
        if (!tick_event_queued_.compare_exchange_strong(
                expected,
                true,
                std::memory_order_acq_rel))
        {
            return true;
        }

        Event event{};
        event.kind = EventKind::Tick;
        event.tick_generation =
            latest_tick_generation_.load(std::memory_order_acquire);
        if (xQueueSend(queue_, &event, 0U) == pdPASS)
        {
            return true;
        }
        tick_event_queued_.store(false, std::memory_order_release);
        return false;
    }

    bool enqueuePendingStop()
    {
        if (!stop_event_pending_.load(std::memory_order_acquire) || !queue_)
        {
            return false;
        }
        bool expected = true;
        if (!stop_event_pending_.compare_exchange_strong(
                expected,
                false,
                std::memory_order_acq_rel))
        {
            return false;
        }
        Event event{};
        event.kind = EventKind::Stop;
        if (xQueueSend(queue_, &event, 0U) != pdPASS)
        {
            stop_event_pending_.store(true, std::memory_order_release);
            return false;
        }
        return true;
    }

    void clearTickEventPending(uint32_t processed_generation)
    {
        if (latest_tick_generation_.load(std::memory_order_acquire) !=
            processed_generation)
        {
            return;
        }

        tick_event_pending_.store(false, std::memory_order_release);
        if (latest_tick_generation_.load(std::memory_order_acquire) !=
            processed_generation)
        {
            tick_event_pending_.store(true, std::memory_order_release);
        }
    }

    void taskLoop()
    {
        for (;;)
        {
            Event event{};
            if (xQueueReceive(queue_, &event, portMAX_DELAY) != pdPASS)
            {
                continue;
            }

            if (event.kind == EventKind::Stop)
            {
                const StorageRuntimeSnapshot current =
                    state_machine_.snapshot();
                const StorageOperation operation =
                    current.active_operation != StorageOperation::None
                        ? current.active_operation
                        : current.pending_operation;
                if (config_.adapter && operation != StorageOperation::None)
                {
                    config_.adapter->cancelAtStepBoundary(operation,
                                                          current.generation);
                }
                state_machine_.stop();
                publish();
                armed_.store(false, std::memory_order_release);
                tick_event_pending_.store(false, std::memory_order_release);
                tick_event_queued_.store(false, std::memory_order_release);
                continue;
            }

            StorageMaintenanceCommand command{};
            if (event.kind == EventKind::Arm)
            {
                command = state_machine_.arm(event.now_ms,
                                             config_.startup_gate,
                                             event.startup_gate_satisfied);
            }
            else
            {
                tick_event_queued_.store(false, std::memory_order_release);
                event.tick_generation =
                    latest_tick_generation_.load(std::memory_order_acquire);
                event.now_ms =
                    latest_tick_now_ms_.load(std::memory_order_acquire);
                event.is_sleeping =
                    latest_is_sleeping_.load(std::memory_order_acquire);
                event.saver_active =
                    latest_saver_active_.load(std::memory_order_acquire);
                event.startup_gate_satisfied =
                    latest_gate_satisfied_.load(std::memory_order_acquire);
                clearTickEventPending(event.tick_generation);
                command = state_machine_.tick(event.now_ms,
                                              event.is_sleeping,
                                              event.saver_active,
                                              event.startup_gate_satisfied,
                                              StorageMaintenanceDemand{
                                                  latest_persistence_pending_
                                                      .load(
                                                          std::memory_order_acquire),
                                                  latest_compaction_pending_
                                                      .load(
                                                          std::memory_order_acquire)});
            }
            publish();
            execute(command, event.now_ms);
            enqueuePendingStop();
            enqueueLatestTick();
        }
    }

    void execute(const StorageMaintenanceCommand& command, uint32_t event_now_ms)
    {
        if (command.kind == StorageMaintenanceCommandKind::None ||
            config_.adapter == nullptr)
        {
            return;
        }

        const uint32_t started_ms = currentTime(event_now_ms);
        if (command.kind == StorageMaintenanceCommandKind::Begin)
        {
            active_operation_started_ms_ = started_ms;
            active_operation_generation_ = command.generation;
            if (config_.on_started)
            {
                config_.on_started(config_.context,
                                   command.operation,
                                   command.generation);
            }
        }

        StorageOperationResult result =
            command.kind == StorageMaintenanceCommandKind::Begin
                ? config_.adapter->begin(command.operation, command.generation)
                : config_.adapter->step(command.operation,
                                        command.generation,
                                        config_.step_budget);
        if (result.operation == StorageOperation::None)
        {
            result.operation = command.operation;
        }
        if (result.generation == 0U)
        {
            result.generation = command.generation;
        }

        const uint32_t finished_ms = currentTime(event_now_ms);
        state_machine_.complete(result, finished_ms);
        const StorageRuntimeSnapshot completed_snapshot =
            state_machine_.snapshot();
        const bool terminal_without_completion =
            completed_snapshot.state == StorageRuntimeState::Done &&
            result.kind != StorageOperationResultKind::Completed &&
            result.kind != StorageOperationResultKind::InProgress;
        if (terminal_without_completion)
        {
            // A bounded retry budget is a terminal cancellation too. The
            // adapter must release any logical maintenance lease retained
            // across retryable physical I/O steps.
            config_.adapter->cancelAtStepBoundary(command.operation,
                                                  command.generation);
        }
        publish();

        if (result.kind != StorageOperationResultKind::InProgress &&
            config_.on_finished)
        {
            const uint32_t operation_started_ms =
                active_operation_generation_ == command.generation
                    ? active_operation_started_ms_
                    : started_ms;
            config_.on_finished(config_.context,
                                command.operation,
                                command.generation,
                                result.kind,
                                static_cast<uint32_t>(finished_ms -
                                                      operation_started_ms),
                                static_cast<uint32_t>(
                                    uxTaskGetStackHighWaterMark(nullptr) *
                                    sizeof(StackType_t)));
        }
        if (result.kind != StorageOperationResultKind::InProgress &&
            active_operation_generation_ == command.generation)
        {
            active_operation_started_ms_ = 0U;
            active_operation_generation_ = 0U;
        }
        if (completed_snapshot.state == StorageRuntimeState::Done)
        {
            // Done is terminal for this command stream, whether it was
            // reached through an explicit cancellation or retry exhaustion.
            // The stable task remains alive, but the next lifecycle may arm
            // it with a fresh configuration and generation.
            armed_.store(false, std::memory_order_release);
        }
    }

    uint32_t currentTime(uint32_t fallback_ms) const
    {
        return config_.now ? config_.now(config_.context) : fallback_ms;
    }

    void publish()
    {
        const StorageRuntimeSnapshot current = state_machine_.snapshot();
        published_state_.store(static_cast<uint8_t>(current.state),
                               std::memory_order_release);
        published_active_operation_.store(
            static_cast<uint8_t>(current.active_operation),
            std::memory_order_release);
        published_pending_operation_.store(
            static_cast<uint8_t>(current.pending_operation),
            std::memory_order_release);
        published_generation_.store(current.generation,
                                    std::memory_order_release);
        published_retry_attempt_.store(current.retry_attempt,
                                       std::memory_order_release);
        published_retry_due_ms_.store(current.retry_due_ms,
                                      std::memory_order_release);
        published_startup_gate_satisfied_.store(
            current.startup_gate_satisfied,
            std::memory_order_release);

        const StorageOperationGeneration ready_generation =
            state_machine_.takeHydrationReadyGeneration();
        if (ready_generation != 0U)
        {
            hydration_ready_generation_.store(ready_generation,
                                              std::memory_order_release);
            initial_hydration_ready_.store(true, std::memory_order_release);
        }

        if (current.state == StorageRuntimeState::Ready ||
            current.state == StorageRuntimeState::WaitingIdle ||
            current.state == StorageRuntimeState::Done)
        {
            foreground_storage_barrier_.store(false,
                                              std::memory_order_release);
        }
        else if (current.pending_operation == StorageOperation::Hydrate &&
                 (current.state == StorageRuntimeState::Hydrating ||
                  current.state == StorageRuntimeState::Backoff))
        {
            foreground_storage_barrier_.store(true,
                                              std::memory_order_release);
        }
    }

    StorageMaintenanceOwnerConfig config_{};
    StorageMaintenanceStateMachine state_machine_{};
    QueueHandle_t queue_ = nullptr;
    TaskHandle_t task_ = nullptr;
    std::atomic<bool> armed_{false};
    std::atomic<bool> arm_event_pending_{false};
    uint32_t initial_now_ms_ = 0U;
    bool initial_gate_satisfied_ = false;
    uint32_t task_retry_due_ms_ = 0U;
    std::atomic<uint32_t> latest_tick_now_ms_{0U};
    std::atomic<uint32_t> latest_tick_generation_{0U};
    std::atomic<bool> latest_is_sleeping_{false};
    std::atomic<bool> latest_saver_active_{false};
    std::atomic<bool> latest_gate_satisfied_{false};
    std::atomic<bool> latest_persistence_pending_{false};
    std::atomic<bool> latest_compaction_pending_{false};
    std::atomic<bool> tick_event_pending_{false};
    std::atomic<bool> tick_event_queued_{false};
    std::atomic<bool> stop_event_pending_{false};
    uint32_t active_operation_started_ms_ = 0U;
    StorageOperationGeneration active_operation_generation_ = 0U;

    std::atomic<uint8_t> published_state_{
        static_cast<uint8_t>(StorageRuntimeState::Dormant)};
    std::atomic<uint8_t> published_active_operation_{
        static_cast<uint8_t>(StorageOperation::None)};
    std::atomic<uint8_t> published_pending_operation_{
        static_cast<uint8_t>(StorageOperation::None)};
    std::atomic<uint32_t> published_generation_{0U};
    std::atomic<uint8_t> published_retry_attempt_{0U};
    std::atomic<uint32_t> published_retry_due_ms_{0U};
    std::atomic<bool> published_startup_gate_satisfied_{false};
    std::atomic<bool> foreground_storage_barrier_{false};
    std::atomic<StorageOperationGeneration> hydration_ready_generation_{0U};
    std::atomic<bool> initial_hydration_ready_{false};
};

} // namespace platform::esp::common::storage
