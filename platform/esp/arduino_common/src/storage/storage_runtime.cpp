#include "platform/esp/arduino_common/storage/storage_runtime.h"
#include "platform/esp/arduino_common/geocaching/browse_runtime.h"

#include "platform/esp/arduino_common/chat/infra/store/sd_protocol_peer_repository.h"
#include "platform/esp/arduino_common/chat/infra/store/sd_store.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/boards/board_runtime.h"
#include "platform/esp/common/memory_budget.h"
#include "platform/esp/common/storage/storage_maintenance_owner.h"
#include "platform/ui/screen_runtime.h"

#include <Arduino.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace platform::esp::arduino_common::storage
{
namespace
{

constexpr uint32_t kStorageTaskStackBytes = 8U * 1024U;
constexpr UBaseType_t kStorageTaskPriority = 0;
constexpr size_t kStorageInternalReservation = kStorageTaskStackBytes;
constexpr size_t kStorageInternalFloor = 40U * 1024U;

struct WorkerContext
{
    chat::SdStore* chat_store = nullptr;
    chat::SdProtocolPeerRepository* peer_directory = nullptr;
    chat::MeshProtocol active_protocol = chat::MeshProtocol::Meshtastic;
};

using Owner = platform::esp::common::storage::StorageMaintenanceOwner;
using OwnerConfig =
    platform::esp::common::storage::StorageMaintenanceOwnerConfig;
using Adapter = platform::esp::common::storage::ISemanticStorageAdapter;
using Operation = platform::esp::common::storage::StorageOperation;
using OperationGeneration =
    platform::esp::common::storage::StorageOperationGeneration;
using Demand =
    platform::esp::common::storage::StorageMaintenanceDemand;
using Result = platform::esp::common::storage::StorageOperationResult;
using ResultKind =
    platform::esp::common::storage::StorageOperationResultKind;

WorkerContext s_context{};
Owner s_owner{};

Result makeResult(Operation operation,
                  OperationGeneration generation,
                  bool ok)
{
    if (ok)
    {
        return Result::completedResult(operation, generation);
    }
    if (!sd_card_ready())
    {
        return Result::failure(ResultKind::DeviceUnavailable,
                               operation,
                               generation);
    }
    return Result::failure(ResultKind::IoError, operation, generation);
}

class SdMaintenanceAdapter final : public Adapter
{
  public:
    explicit SdMaintenanceAdapter(WorkerContext& context) : context_(context) {}

    Result begin(Operation operation, OperationGeneration generation) override
    {
        // A retry keeps the backend's logical cursor and lease. Fairness may
        // choose another service only for a new owner generation; otherwise a
        // Geocaching slice could complete a generation still held by chat/peers.
        const bool retry = operation == previous_operation_ && generation == previous_generation_;
        previous_operation_ = operation;
        previous_generation_ = generation;
        next_step_ = Step::None;
        if (!retry && operation == Operation::Persist && geocaching::browse_runtime::workPending())
        {
            const bool other_pending = (context_.chat_store && context_.chat_store->persistencePending()) ||
                                       (context_.peer_directory && context_.peer_directory->persistencePending());
            if (geocaching_turn_ || !other_pending)
            {
                geocaching_turn_ = false;
                geocaching_steps_ = 8;
                next_step_ = Step::Geocaching;
                return Result::inProgressResult(operation, generation);
            }
        }
        geocaching_turn_ = true;
        if (operation == Operation::Hydrate)
        {
            const Result chat_result = hydrateChat(generation);
            if (!chat_result.completed())
            {
                return chat_result;
            }
            if (!context_.peer_directory)
            {
                return chat_result;
            }
            const auto peer_result =
                context_.peer_directory->beginMaintenance(operation, generation);
            if (!peer_result.inProgress())
            {
                return peer_result;
            }
            next_step_ = Step::HydratePeer;
            return Result::inProgressResult(operation, generation);
        }

        if (operation == Operation::Persist)
        {
            const Result chat_result = persistChat(generation);
            if (!chat_result.completed())
            {
                return chat_result;
            }
            if (!context_.peer_directory)
            {
                return chat_result;
            }
            const auto peer_result =
                context_.peer_directory->beginMaintenance(operation,
                                                          generation);
            if (!peer_result.inProgress())
            {
                return peer_result;
            }
            next_step_ = Step::PersistPeer;
            return Result::inProgressResult(operation, generation);
        }

        if (operation == Operation::Compact)
        {
            const Result chat_result = compactChat(generation);
            if (!chat_result.completed())
            {
                return chat_result;
            }
            if (!context_.peer_directory)
            {
                return chat_result;
            }
            const auto peer_result =
                context_.peer_directory->beginMaintenance(operation, generation);
            if (!peer_result.inProgress())
            {
                return peer_result;
            }
            next_step_ = Step::CompactPeer;
            return Result::inProgressResult(operation, generation);
        }

        return Result::failure(ResultKind::Cancelled, operation, generation);
    }

    Result step(Operation operation,
                OperationGeneration generation,
                const platform::esp::common::storage::StorageOperationBudget&
                    budget) override
    {
        const Step step = next_step_;
        if (step == Step::Geocaching)
        {
            if (budget.max_work_items == 0) return Result::inProgressResult(operation, generation);
            if (geocaching::browse_runtime::workPending()) geocaching::browse_runtime::step();
            // Recheck demand after every slice: messages or contacts may have
            // become dirty after this Geocaching batch was admitted. Return to
            // the shared owner instead of consuming the remaining batch first.
            const bool other_pending = (context_.chat_store && context_.chat_store->persistencePending()) ||
                                       (context_.peer_directory && context_.peer_directory->persistencePending());
            if (geocaching_steps_) --geocaching_steps_;
            if (!other_pending && geocaching_steps_ && geocaching::browse_runtime::workPending())
                return Result::inProgressResult(operation, generation);
            next_step_ = Step::None;
            return Result::completedResult(operation, generation);
        }
        if (step == Step::Chat)
        {
            if (!context_.chat_store)
            {
                if (!context_.peer_directory)
                {
                    next_step_ = Step::None;
                    return makeResult(operation, generation, true);
                }
                const auto peer_result =
                    context_.peer_directory->beginMaintenance(operation,
                                                              generation);
                if (!peer_result.inProgress())
                {
                    next_step_ = Step::None;
                    return peer_result;
                }
                next_step_ =
                    operation == Operation::Hydrate
                        ? Step::HydratePeer
                    : operation == Operation::Persist
                        ? Step::PersistPeer
                        : Step::CompactPeer;
                return peer_result;
            }
            const Result result = context_.chat_store->stepMaintenance(
                operation,
                generation,
                budget);
            if (!result.completed())
            {
                return result;
            }
            if (context_.peer_directory)
            {
                const auto peer_result =
                    context_.peer_directory->beginMaintenance(operation,
                                                              generation);
                if (!peer_result.inProgress())
                {
                    return peer_result;
                }
                next_step_ = operation == Operation::Hydrate
                                 ? Step::HydratePeer
                             : operation == Operation::Persist
                                 ? Step::PersistPeer
                                 : Step::CompactPeer;
            }
            else
            {
                next_step_ = Step::None;
            }
            return next_step_ == Step::None
                       ? result
                       : Result::inProgressResult(operation, generation);
        }

        if ((operation == Operation::Hydrate && step == Step::HydratePeer) ||
            (operation == Operation::Persist && step == Step::PersistPeer) ||
            (operation == Operation::Compact && step == Step::CompactPeer))
        {
            if (!context_.peer_directory)
            {
                next_step_ = Step::None;
                return Result::failure(ResultKind::DeviceUnavailable,
                                       operation,
                                       generation);
            }
            const Result result = context_.peer_directory->stepMaintenance(
                operation,
                generation,
                budget);
            if (!result.inProgress())
            {
                next_step_ = Step::None;
            }
            return result;
        }
        next_step_ = Step::None;
        return Result::failure(ResultKind::Cancelled, operation, generation);
    }

    void cancelAtStepBoundary(Operation operation,
                              OperationGeneration generation) override
    {
        if (context_.chat_store)
        {
            context_.chat_store->cancelMaintenance(operation, generation);
        }
        if (context_.peer_directory)
        {
            context_.peer_directory->cancelMaintenance(operation, generation);
        }
        next_step_ = Step::None;
    }

  private:
    enum class Step : uint8_t
    {
        None,
        Chat,
        HydratePeer,
        PersistPeer,
        CompactPeer,
        Geocaching,
    };

    Result hydrateChat(OperationGeneration generation)
    {
        if (context_.chat_store == nullptr)
        {
            return Result::completedResult(Operation::Hydrate, generation);
        }
        next_step_ = Step::Chat;
        return context_.chat_store->beginMaintenance(
            Operation::Hydrate,
            generation);
    }

    Result compactChat(OperationGeneration generation)
    {
        if (context_.chat_store == nullptr)
        {
            return Result::completedResult(Operation::Compact, generation);
        }
        next_step_ = Step::Chat;
        return context_.chat_store->beginMaintenance(
            Operation::Compact,
            generation);
    }

    Result persistChat(OperationGeneration generation)
    {
        if (context_.chat_store == nullptr)
        {
            return Result::completedResult(Operation::Persist, generation);
        }
        next_step_ = Step::Chat;
        return context_.chat_store->beginMaintenance(
            Operation::Persist,
            generation);
    }

    WorkerContext& context_;
    Operation previous_operation_ = Operation::None;
    OperationGeneration previous_generation_ = 0;
    Step next_step_ = Step::None;
    bool geocaching_turn_ = true;
    uint8_t geocaching_steps_ = 0;
};

SdMaintenanceAdapter s_adapter(s_context);

bool admitOwner(void*)
{
    return ::platform::esp::common::memory::admit("storage_owner",
                                                  kStorageInternalReservation,
                                                  0,
                                                  0,
                                                  kStorageInternalFloor,
                                                  0);
}

uint32_t ownerNow(void*)
{
    return millis();
}

const char* operationName(Operation operation)
{
    if (operation == Operation::Hydrate)
    {
        return "hydrate";
    }
    if (operation == Operation::Persist)
    {
        return "persist";
    }
    return "compact";
}

void ownerStarted(void*,
                  Operation operation,
                  OperationGeneration generation)
{
    Serial.printf("[Storage] owner begin mode=%s generation=%lu active_protocol=%u\n",
                  operationName(operation),
                  static_cast<unsigned long>(generation),
                  static_cast<unsigned>(s_context.active_protocol));
}

void ownerFinished(void*,
                   Operation operation,
                   OperationGeneration generation,
                   ResultKind result,
                   uint32_t elapsed_ms,
                   uint32_t stack_free_bytes)
{
    Serial.printf("[Storage] owner end mode=%s generation=%lu ok=%u result=%u "
                  "elapsed_ms=%lu stack_free_bytes=%lu\n",
                  operationName(operation),
                  static_cast<unsigned long>(generation),
                  (result == ResultKind::Completed ||
                   result == ResultKind::InProgress)
                      ? 1U
                      : 0U,
                  static_cast<unsigned>(result),
                  static_cast<unsigned long>(elapsed_ms),
                  static_cast<unsigned long>(stack_free_bytes));
}

bool startupGateSatisfied()
{
    return ::platform::esp::boards::storageStartupGateSatisfied();
}

} // namespace

void start_deferred_storage(chat::SdStore* chat_store,
                            chat::SdProtocolPeerRepository* peer_store,
                            chat::MeshProtocol active_protocol)
{
    if (s_owner.isArmed())
    {
        return;
    }

    if (!chat_store && !peer_store)
    {
        Serial.printf("[Storage] maintenance skipped backend=ram\n");
        return;
    }

    s_context.chat_store = chat_store;
    s_context.peer_directory = peer_store;
    s_context.active_protocol = active_protocol;
    const bool shared_storage_topology =
        ::platform::esp::boards::storageCapabilities()
            .requiresDisplayTransactionGate();

    OwnerConfig config{};
    config.task_name = "storage_owner";
    config.stack_bytes = kStorageTaskStackBytes;
    config.priority = kStorageTaskPriority;
    config.core = 1;
    config.startup_gate = shared_storage_topology
                              ? platform::esp::common::storage::StorageStartupGate::
                                    DisplayTransaction
                              : platform::esp::common::storage::StorageStartupGate::Immediate;
    config.context = &s_context;
    config.adapter = &s_adapter;
    config.admit = &admitOwner;
    config.now = &ownerNow;
    config.on_started = &ownerStarted;
    config.on_finished = &ownerFinished;
    s_owner.configure(config);
    (void)s_owner.arm(millis(), startupGateSatisfied());
}

void tick_deferred_storage()
{
    if (!s_owner.isArmed())
    {
        return;
    }

    Demand demand{};
    demand.persistence_pending =
        geocaching::browse_runtime::workPending() ||
        (s_context.chat_store &&
         s_context.chat_store->persistencePending()) ||
        (s_context.peer_directory &&
         s_context.peer_directory->persistencePending());
    demand.compaction_pending =
        (s_context.chat_store &&
         s_context.chat_store->compactionPending()) ||
        (s_context.peer_directory &&
         s_context.peer_directory->compactionPending());
    (void)s_owner.submitTick(millis(),
                             ::platform::ui::screen::is_sleeping(),
                             ::platform::ui::screen::is_saver_active(),
                             startupGateSatisfied(),
                             demand);
}

void stop_deferred_storage()
{
    (void)s_owner.requestStop();
}

bool hydration_active()
{
    return s_owner.hydrationActive();
}

bool initial_hydration_pending()
{
    return s_owner.initialHydrationPending();
}

bool initial_hydration_ready()
{
    return s_owner.initialHydrationReady();
}

bool consume_hydration_ready()
{
    return s_owner.consumeHydrationReady();
}

} // namespace platform::esp::arduino_common::storage
