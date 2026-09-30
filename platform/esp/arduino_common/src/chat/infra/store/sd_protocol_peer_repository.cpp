#include "platform/esp/arduino_common/chat/infra/store/sd_protocol_peer_repository.h"

#include "platform/esp/arduino_common/storage/scoped_state_lock.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"

#include <Arduino.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace chat
{
namespace
{
namespace storage_runtime = ::platform::esp::arduino_common::storage;
namespace storage_v2 = ::chat::storage::v2;
namespace storage_contracts = ::platform::esp::common::storage;

storage_contracts::StorageOperationResultKind stateLockFailure(
    storage_runtime::StateLockResult result)
{
    return result == storage_runtime::StateLockResult::Unavailable
               ? storage_contracts::StorageOperationResultKind::
                     DeviceUnavailable
               : storage_contracts::StorageOperationResultKind::StateBusy;
}

MeshPeerDirectoryStatus stateLockStatus(
    storage_runtime::StateLockResult result)
{
    return MeshPeerDirectoryStatus::fail(
        result == storage_runtime::StateLockResult::Unavailable
            ? MeshPeerDirectoryStatusCode::DeviceUnavailable
            : MeshPeerDirectoryStatusCode::Busy);
}

MeshPeerDirectoryStatus ioFailureStatus()
{
    return MeshPeerDirectoryStatus::fail(
        storage_runtime::sd_card_ready()
            ? MeshPeerDirectoryStatusCode::IoError
            : MeshPeerDirectoryStatusCode::DeviceUnavailable);
}

storage_contracts::StorageOperationResultKind operationFailureKind(
    MeshPeerDirectoryStatusCode code)
{
    switch (code)
    {
    case MeshPeerDirectoryStatusCode::Busy:
        return storage_contracts::StorageOperationResultKind::StateBusy;
    case MeshPeerDirectoryStatusCode::DeviceUnavailable:
    case MeshPeerDirectoryStatusCode::StorageUnavailable:
        return storage_contracts::StorageOperationResultKind::
            DeviceUnavailable;
    case MeshPeerDirectoryStatusCode::IoError:
    default:
        return storage_contracts::StorageOperationResultKind::IoError;
    }
}

MeshPeerDirectoryStatus maintenanceStatus(
    const storage_contracts::StorageOperationResult& result)
{
    switch (result.kind)
    {
    case storage_contracts::StorageOperationResultKind::StateBusy:
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::Busy);
    case storage_contracts::StorageOperationResultKind::DeviceUnavailable:
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::DeviceUnavailable);
    case storage_contracts::StorageOperationResultKind::IoError:
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::IoError);
    case storage_contracts::StorageOperationResultKind::RetryLater:
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::Busy);
    case storage_contracts::StorageOperationResultKind::StaleGeneration:
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::Busy);
    case storage_contracts::StorageOperationResultKind::Cancelled:
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::StorageUnavailable);
    case storage_contracts::StorageOperationResultKind::Completed:
    case storage_contracts::StorageOperationResultKind::InProgress:
    default:
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::IoError);
    }
}

constexpr const char* kRoot = "/data/v2";
constexpr MeshProtocol kProtocols[] = {
    MeshProtocol::Meshtastic,
    MeshProtocol::MeshCore,
    MeshProtocol::Reticulum,
};
constexpr std::size_t kEphemeralPeerCapacity = 2048U;
constexpr std::size_t kProtectedContactCapacity = 4096U;
constexpr std::size_t kPeerHotCacheCapacity[] = {16U, 128U, 64U};
constexpr uint32_t kBootCompactionDeltaThreshold = 1024U;
constexpr std::size_t kPendingFlushBudget = 4U;
constexpr std::size_t kPendingObservationCapacity = 64U;
constexpr uint8_t kMaintenanceJournalCount = 4U;
constexpr TickType_t kPersistenceLeaseWaitTicks = pdMS_TO_TICKS(50U);
using ScopedRepositoryLock = storage_runtime::ScopedRecursiveStateLock;

bool hasText(const char* text)
{
    return text && text[0] != '\0';
}

bool textContains(const char* text, const char* query)
{
    return text && query && std::strstr(text, query) != nullptr;
}

const MeshPeerNodeFacts* nodeFacts(const MeshPeerRecord& record)
{
    if (record.identity.protocol == MeshProtocol::Meshtastic)
    {
        return &record.meshtastic.node;
    }
    if (record.identity.protocol == MeshProtocol::MeshCore)
    {
        return &record.meshcore.node;
    }
    return nullptr;
}

} // namespace

SdProtocolPeerRepository::SdProtocolPeerRepository(IChatStore& chat_store)
    : chat_store_(chat_store),
      mutex_(xSemaphoreCreateRecursiveMutex())
{
    peers_.reserve(256U);
    contacts_.reserve(64U);
    pending_peer_deltas_.reserve(16U);
    pending_contact_deltas_.reserve(16U);
    pending_peer_observations_.reserve(kPendingObservationCapacity);
    flush_peer_batch_.reserve(kPendingFlushBudget);
    flush_contact_batch_.reserve(kPendingFlushBudget);
    flush_peer_snapshot_.reserve(256U);
    pending_observation_mutex_ = xSemaphoreCreateMutex();
    persistence_mutex_ = xSemaphoreCreateMutex();
    slot_scratch_.resize(512U, 0U);
    maintenance_scratch_.resize(512U, 0U);
}

SdProtocolPeerRepository::~SdProtocolPeerRepository()
{
    if (mutex_)
    {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
    if (pending_observation_mutex_)
    {
        vSemaphoreDelete(pending_observation_mutex_);
        pending_observation_mutex_ = nullptr;
    }
    if (persistence_mutex_)
    {
        vSemaphoreDelete(persistence_mutex_);
        persistence_mutex_ = nullptr;
    }
}

bool SdProtocolPeerRepository::acquirePersistenceLease(TickType_t wait_ticks)
{
    return persistence_mutex_ &&
           xSemaphoreTake(persistence_mutex_, wait_ticks) == pdTRUE;
}

void SdProtocolPeerRepository::releasePersistenceLease()
{
    if (persistence_mutex_)
    {
        xSemaphoreGive(persistence_mutex_);
    }
}

void SdProtocolPeerRepository::releaseMaintenanceLease()
{
    if (maintenance_persistence_locked_)
    {
        maintenance_persistence_locked_ = false;
        releasePersistenceLease();
    }
}

platform::esp::common::storage::StorageOperationResult
SdProtocolPeerRepository::beginMaintenance(
    platform::esp::common::storage::StorageOperation operation,
    platform::esp::common::storage::StorageOperationGeneration generation)
{
    if (operation != storage_contracts::StorageOperation::Hydrate &&
        operation != storage_contracts::StorageOperation::Persist &&
        operation != storage_contracts::StorageOperation::Compact)
    {
        return storage_contracts::StorageOperationResult::failure(
            storage_contracts::StorageOperationResultKind::Cancelled,
            operation,
            generation);
    }
    if (operation == storage_contracts::StorageOperation::Hydrate && hydrated_)
    {
        return storage_contracts::StorageOperationResult::completedResult(
            operation,
            generation);
    }
    if (operation == storage_contracts::StorageOperation::Persist &&
        !persistencePending())
    {
        return storage_contracts::StorageOperationResult::completedResult(
            operation,
            generation);
    }
    // A composite adapter may revisit this repository after a later stage in
    // the same generation asked the owner to retry. Preserve the completed
    // stage instead of resetting its cursor.
    if (maintenance_.operation == operation &&
        maintenance_.generation == generation &&
        maintenance_.phase == MaintenancePhase::Complete)
    {
        return storage_contracts::StorageOperationResult::completedResult(
            operation,
            generation);
    }

    // The persistence lease is the repository's logical maintenance
    // ownership, not the physical SD/SPI transaction lease. Keep that
    // ownership across a retry so foreground persistence cannot interleave
    // between generations.
    if (maintenance_.operation == operation &&
        maintenance_.generation == generation &&
        maintenance_.phase != MaintenancePhase::Idle &&
        maintenance_.phase != MaintenancePhase::Complete &&
        maintenance_.phase != MaintenancePhase::Failed)
    {
        if (!maintenance_persistence_locked_ &&
            !acquirePersistenceLease(kPersistenceLeaseWaitTicks))
        {
            return storage_contracts::StorageOperationResult::failure(
                storage_contracts::StorageOperationResultKind::StateBusy,
                operation,
                generation);
        }
        maintenance_persistence_locked_ = true;
        if (operation == storage_contracts::StorageOperation::Hydrate)
        {
            hydrating_.store(true, std::memory_order_release);
        }
        return storage_contracts::StorageOperationResult::inProgressResult(
            operation,
            generation);
    }

    if (!acquirePersistenceLease(kPersistenceLeaseWaitTicks))
    {
        return storage_contracts::StorageOperationResult::failure(
            storage_contracts::StorageOperationResultKind::StateBusy,
            operation,
            generation);
    }
    if (maintenance_.phase != MaintenancePhase::Idle &&
        maintenance_.phase != MaintenancePhase::Complete &&
        maintenance_.phase != MaintenancePhase::Failed)
    {
        releasePersistenceLease();
        return storage_contracts::StorageOperationResult::failure(
            storage_contracts::StorageOperationResultKind::StateBusy,
            operation,
            generation);
    }
    if (operation == storage_contracts::StorageOperation::Hydrate &&
        !begun_)
    {
        releasePersistenceLease();
        return storage_contracts::StorageOperationResult::failure(
            storage_contracts::StorageOperationResultKind::StateBusy,
            operation,
            generation);
    }

    maintenance_ = {};
    maintenance_.operation = operation;
    maintenance_.generation = generation;
    maintenance_.phase =
        operation == storage_contracts::StorageOperation::Hydrate
            ? MaintenancePhase::HydrationPrepare
        : operation == storage_contracts::StorageOperation::Persist
            ? MaintenancePhase::PersistenceFlush
            : MaintenancePhase::CompactionPrepare;
    if (operation == storage_contracts::StorageOperation::Hydrate)
    {
        hydrating_.store(true, std::memory_order_release);
    }
    maintenance_persistence_locked_ = true;
    return storage_contracts::StorageOperationResult::inProgressResult(
        operation,
        generation);
}

platform::esp::common::storage::StorageOperationResult
SdProtocolPeerRepository::stepMaintenance(
    platform::esp::common::storage::StorageOperation operation,
    platform::esp::common::storage::StorageOperationGeneration generation,
    const platform::esp::common::storage::StorageOperationBudget& budget)
{
    if (maintenance_.operation != operation ||
        maintenance_.generation != generation)
    {
        return storage_contracts::StorageOperationResult::failure(
            storage_contracts::StorageOperationResultKind::StaleGeneration,
            operation,
            generation);
    }
    if (maintenance_.phase == MaintenancePhase::Complete)
    {
        releaseMaintenanceLease();
        return storage_contracts::StorageOperationResult::completedResult(
            operation,
            generation);
    }
    if (maintenance_.phase == MaintenancePhase::Failed)
    {
        releaseMaintenanceLease();
        return maintenanceFailure(
            storage_contracts::StorageOperationResultKind::IoError);
    }
    const auto result =
        operation == storage_contracts::StorageOperation::Hydrate
            ? stepHydration(budget)
        : operation == storage_contracts::StorageOperation::Persist
            ? stepPersistence(budget)
            : stepCompaction(budget);
    if (result.completed() ||
        result.kind == storage_contracts::StorageOperationResultKind::Cancelled ||
        maintenance_.phase == MaintenancePhase::Failed)
    {
        releaseMaintenanceLease();
    }
    return result;
}

void SdProtocolPeerRepository::cancelMaintenance(
    platform::esp::common::storage::StorageOperation operation,
    platform::esp::common::storage::StorageOperationGeneration generation)
{
    if (maintenance_.operation != operation ||
        maintenance_.generation != generation)
    {
        return;
    }
    maintenance_journal_.reset();
    maintenance_.phase = MaintenancePhase::Failed;
    if (operation == storage_contracts::StorageOperation::Hydrate)
    {
        hydrating_.store(false, std::memory_order_release);
    }
    releaseMaintenanceLease();
}

platform::esp::common::storage::StorageOperationResult
SdProtocolPeerRepository::maintenanceFailure(
    platform::esp::common::storage::StorageOperationResultKind kind) const
{
    return storage_contracts::StorageOperationResult::failure(
        kind,
        maintenance_.operation,
        maintenance_.generation);
}

bool SdProtocolPeerRepository::prepareMaintenanceJournal()
{
    const MeshProtocol protocol = kProtocols[maintenance_.protocol_index];
    const uint8_t index = maintenance_.journal_index;
    const char* name = nullptr;
    storage_v2::JournalKind kind = storage_v2::JournalKind::PeerSnapshot;
    std::size_t slot_size = 0U;
    switch (index)
    {
    case 0U:
        name = "peers.snapshot";
        kind = storage_v2::JournalKind::PeerSnapshot;
        slot_size = storage_v2::peerSlotSize(protocol);
        break;
    case 1U:
        name = "peers.delta";
        kind = storage_v2::JournalKind::PeerDelta;
        slot_size = storage_v2::peerSlotSize(protocol);
        break;
    case 2U:
        name = "contacts.snapshot";
        kind = storage_v2::JournalKind::ContactSnapshot;
        slot_size = storage_v2::contactSlotSize(protocol);
        break;
    case 3U:
        name = "contacts.delta";
        kind = storage_v2::JournalKind::ContactDelta;
        slot_size = storage_v2::contactSlotSize(protocol);
        break;
    default:
        return false;
    }

    buildProtocolPath(protocol,
                      name,
                      maintenance_path_,
                      sizeof(maintenance_path_));
    maintenance_protocol_ = protocol;
    maintenance_kind_ = kind;
    maintenance_slot_size_ = slot_size;
    maintenance_.journal_started = maintenance_journal_.begin(
        journal_,
        maintenance_path_,
        protocol,
        kind,
        slot_size);
    return maintenance_.journal_started;
}

bool SdProtocolPeerRepository::applyHydrationJournalSlot(
    MeshProtocol protocol,
    storage_v2::JournalKind kind)
{
    if (maintenance_slot_size_ > maintenance_scratch_.size())
    {
        return false;
    }
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked())
    {
        return false;
    }
    if (kind == storage_v2::JournalKind::PeerSnapshot ||
        kind == storage_v2::JournalKind::PeerDelta)
    {
        storage_v2::PeerProjection projection{};
        if (!storage_v2::decodePeerSlot(protocol,
                                        maintenance_scratch_.data(),
                                        maintenance_slot_size_,
                                        projection))
        {
            return true;
        }
        (void)applyPeerProjection(projection);
        return true;
    }
    storage_v2::ContactProjection projection{};
    if (!storage_v2::decodeContactSlot(protocol,
                                       maintenance_scratch_.data(),
                                       maintenance_slot_size_,
                                       projection))
    {
        return true;
    }
    (void)applyContactProjection(projection);
    return true;
}

platform::esp::common::storage::StorageOperationResult
SdProtocolPeerRepository::stepHydration(
    const platform::esp::common::storage::StorageOperationBudget& budget)
{
    const uint8_t work_items = std::max<uint8_t>(1U, budget.max_work_items);
    for (uint8_t work = 0U; work < work_items; ++work)
    {
        switch (maintenance_.phase)
        {
        case MaintenancePhase::HydrationPrepare:
        {
            if (!storage_runtime::sd_card_ready() || !ensureLayout())
            {
                maintenance_.phase = MaintenancePhase::Failed;
                hydrating_.store(false, std::memory_order_release);
                return maintenanceFailure(
                    storage_contracts::StorageOperationResultKind::
                        DeviceUnavailable);
            }
            ScopedRepositoryLock lock(mutex_);
            if (!lock.locked())
            {
                return maintenanceFailure(
                    stateLockFailure(lock.result()));
            }
            peers_.clear();
            contacts_.clear();
            pending_peer_deltas_.clear();
            pending_peer_head_ = 0U;
            pending_contact_deltas_.clear();
            pending_contact_head_ = 0U;
            pending_peer_revision_ = 0U;
            pending_contact_revision_ = 0U;
            std::memset(protocol_reset_pending_, 0, sizeof(protocol_reset_pending_));
            std::memset(protocol_reset_revision_,
                        0,
                        sizeof(protocol_reset_revision_));
            std::memset(partitions_, 0, sizeof(partitions_));
            persistence_pending_.store(false,
                                       std::memory_order_release);
            compaction_pending_.store(false,
                                      std::memory_order_release);
            maintenance_.phase = MaintenancePhase::HydrationJournal;
            break;
        }

        case MaintenancePhase::HydrationJournal:
            if (!maintenance_.journal_started &&
                !prepareMaintenanceJournal())
            {
                maintenance_.phase = MaintenancePhase::Failed;
                hydrating_.store(false, std::memory_order_release);
                return maintenanceFailure(
                    storage_contracts::StorageOperationResultKind::IoError);
            }
            {
                const auto status = maintenance_journal_.next(
                    journal_,
                    maintenance_scratch_.data(),
                    maintenance_scratch_.size());
                if (status == storage_v2::FixedSlotJournalCursor::StepStatus::
                                  Item)
                {
                    if (!applyHydrationJournalSlot(maintenance_protocol_,
                                                   maintenance_kind_))
                    {
                        return maintenanceFailure(
                            storage_contracts::StorageOperationResultKind::
                                StateBusy);
                    }
                    break;
                }
                if (status == storage_v2::FixedSlotJournalCursor::StepStatus::
                                  Unavailable)
                {
                    return maintenanceFailure(
                        storage_contracts::StorageOperationResultKind::
                            RetryLater);
                }
                if (status == storage_v2::FixedSlotJournalCursor::StepStatus::
                                  Invalid)
                {
                    return maintenanceFailure(
                        storage_contracts::StorageOperationResultKind::IoError);
                }
                if (status == storage_v2::FixedSlotJournalCursor::StepStatus::
                                  Complete)
                {
                    const auto& inspection = maintenance_journal_.inspection();
                    if (maintenance_kind_ ==
                        storage_v2::JournalKind::PeerDelta)
                    {
                        partitions_[protocolIndex(maintenance_protocol_)]
                            .peer_delta_count = inspection.slot_count;
                    }
                    else if (maintenance_kind_ ==
                             storage_v2::JournalKind::ContactDelta)
                    {
                        partitions_[protocolIndex(maintenance_protocol_)]
                            .contact_delta_count = inspection.slot_count;
                    }
                }
                maintenance_journal_.reset();
                maintenance_.journal_started = false;
                ++maintenance_.journal_index;
                if (maintenance_.journal_index >= kMaintenanceJournalCount)
                {
                    maintenance_.journal_index = 0U;
                    ++maintenance_.protocol_index;
                    if (maintenance_.protocol_index >=
                        static_cast<uint8_t>(sizeof(kProtocols) /
                                             sizeof(kProtocols[0])))
                    {
                        maintenance_.phase = MaintenancePhase::HydrationFinalize;
                    }
                }
            }
            break;

        case MaintenancePhase::HydrationFinalize:
        {
            ScopedRepositoryLock lock(mutex_);
            if (!lock.locked())
            {
                return maintenanceFailure(
                    stateLockFailure(lock.result()));
            }
            for (MeshProtocol protocol : kProtocols)
            {
                reconcileStableIdentities(protocol);
            }
            overlayContactFacts();
            drainDeferredObservationsLocked();
            hydrated_ = true;
            hydrating_.store(false, std::memory_order_release);
            maintenance_.phase = MaintenancePhase::Complete;
            return storage_contracts::StorageOperationResult::completedResult(
                maintenance_.operation,
                maintenance_.generation);
        }

        case MaintenancePhase::Complete:
            return storage_contracts::StorageOperationResult::completedResult(
                maintenance_.operation,
                maintenance_.generation);

        default:
            return maintenanceFailure(
                storage_contracts::StorageOperationResultKind::StateBusy);
        }
    }
    return storage_contracts::StorageOperationResult::inProgressResult(
        maintenance_.operation,
        maintenance_.generation);
}

platform::esp::common::storage::StorageOperationResult
SdProtocolPeerRepository::stepPersistence(
    const platform::esp::common::storage::StorageOperationBudget& budget)
{
    if (!begun_ || !hydrated_ ||
        hydrating_.load(std::memory_order_acquire))
    {
        return maintenanceFailure(
            storage_contracts::StorageOperationResultKind::StateBusy);
    }

    const MeshPeerDirectoryStatus flush_status = flushPendingDeltas(
        std::max<std::size_t>(1U, budget.max_work_items));
    if (!flush_status.succeeded())
    {
        return maintenanceFailure(operationFailureKind(flush_status.code));
    }
    if (!persistencePending())
    {
        maintenance_.phase = MaintenancePhase::Complete;
        return storage_contracts::StorageOperationResult::completedResult(
            maintenance_.operation,
            maintenance_.generation);
    }
    return storage_contracts::StorageOperationResult::inProgressResult(
        maintenance_.operation,
        maintenance_.generation);
}

platform::esp::common::storage::StorageOperationResult
SdProtocolPeerRepository::stepCompaction(
    const platform::esp::common::storage::StorageOperationBudget& budget)
{
    if (!begun_ || !hydrated_ ||
        hydrating_.load(std::memory_order_acquire))
    {
        return maintenanceFailure(
            storage_contracts::StorageOperationResultKind::StateBusy);
    }

    const uint8_t work_items = std::max<uint8_t>(1U, budget.max_work_items);
    for (uint8_t work = 0U; work < work_items; ++work)
    {
        switch (maintenance_.phase)
        {
        case MaintenancePhase::CompactionPrepare:
        {
            ScopedRepositoryLock lock(mutex_);
            if (!lock.locked())
            {
                return maintenanceFailure(
                    stateLockFailure(lock.result()));
            }
            compaction_peers_ = peers_;
            compaction_contacts_ = contacts_;
            maintenance_.protocol_index = 0U;
            maintenance_.compaction_projection_index = 0U;
            maintenance_.compaction_inspection_index = 0U;
            maintenance_.compaction_record_index = 0U;
            maintenance_.compact_peers = false;
            maintenance_.compact_contacts = false;
            std::memset(compaction_force_peers_,
                        0,
                        sizeof(compaction_force_peers_));
            std::memset(compaction_force_contacts_,
                        0,
                        sizeof(compaction_force_contacts_));
            std::memcpy(compaction_reset_revision_,
                        protocol_reset_revision_,
                        sizeof(compaction_reset_revision_));
            compaction_peer_revision_ = pending_peer_revision_;
            compaction_contact_revision_ = pending_contact_revision_;
            for (std::size_t index = pending_peer_head_;
                 index < pending_peer_deltas_.size();
                 ++index)
            {
                compaction_force_peers_[protocolIndex(
                    pending_peer_deltas_[index].record.identity.protocol)] =
                    true;
            }
            for (std::size_t index = pending_contact_head_;
                 index < pending_contact_deltas_.size();
                 ++index)
            {
                compaction_force_contacts_[protocolIndex(
                    pending_contact_deltas_[index].identity.protocol)] = true;
            }
            for (std::size_t index = 0U; index < 3U; ++index)
            {
                compaction_force_peers_[index] =
                    compaction_force_peers_[index] ||
                    protocol_reset_pending_[index];
            }
            maintenance_.phase = MaintenancePhase::CompactionInspect;
            break;
        }

        case MaintenancePhase::CompactionInspect:
        {
            if (maintenance_.protocol_index >=
                static_cast<uint8_t>(sizeof(kProtocols) /
                                     sizeof(kProtocols[0])))
            {
                maintenance_.phase = MaintenancePhase::Complete;
                return storage_contracts::StorageOperationResult::
                    completedResult(maintenance_.operation,
                                    maintenance_.generation);
            }
            const MeshProtocol protocol =
                kProtocols[maintenance_.protocol_index];
            const uint8_t index = maintenance_.compaction_inspection_index;
            const char* name = index == 0U ? "peers.delta"
                                           : "contacts.delta";
            const storage_v2::JournalKind kind =
                index == 0U ? storage_v2::JournalKind::PeerDelta
                            : storage_v2::JournalKind::ContactDelta;
            const std::size_t slot_size =
                index == 0U ? storage_v2::peerSlotSize(protocol)
                            : storage_v2::contactSlotSize(protocol);
            char path[96] = {};
            buildProtocolPath(protocol, name, path, sizeof(path));
            const auto inspection =
                journal_.inspect(path, protocol, kind, slot_size);
            if (inspection.state ==
                storage_v2::FixedSlotJournalEngine::State::IoError)
            {
                return maintenanceFailure(
                    storage_contracts::StorageOperationResultKind::
                        RetryLater);
            }
            if (inspection.state ==
                storage_v2::FixedSlotJournalEngine::State::Incompatible)
            {
                return maintenanceFailure(
                    storage_contracts::StorageOperationResultKind::IoError);
            }
            const bool should_compact =
                inspection.slot_count >= kBootCompactionDeltaThreshold ||
                (index == 0U ? compaction_force_peers_
                                   [protocolIndex(protocol)]
                             : compaction_force_contacts_
                                   [protocolIndex(protocol)]);
            if (index == 0U)
            {
                maintenance_.compact_peers = should_compact;
            }
            else
            {
                maintenance_.compact_contacts = should_compact;
            }
            ++maintenance_.compaction_inspection_index;
            if (maintenance_.compaction_inspection_index >= 2U)
            {
                maintenance_.compaction_inspection_index = 0U;
                maintenance_.compaction_projection_index = 0U;
                maintenance_.phase = MaintenancePhase::CompactionCreate;
            }
            break;
        }

        case MaintenancePhase::CompactionCreate:
        {
            while (maintenance_.compaction_projection_index < 2U)
            {
                const bool enabled =
                    maintenance_.compaction_projection_index == 0U
                        ? maintenance_.compact_peers
                        : maintenance_.compact_contacts;
                if (enabled)
                {
                    break;
                }
                ++maintenance_.compaction_projection_index;
            }
            if (maintenance_.compaction_projection_index >= 2U)
            {
                maintenance_.phase = MaintenancePhase::CompactionAdvance;
                break;
            }

            const MeshProtocol protocol =
                kProtocols[maintenance_.protocol_index];
            const bool peers =
                maintenance_.compaction_projection_index == 0U;
            const char* base = peers ? "peers" : "contacts";
            maintenance_kind_ =
                peers ? storage_v2::JournalKind::PeerSnapshot
                      : storage_v2::JournalKind::ContactSnapshot;
            maintenance_slot_size_ =
                peers ? storage_v2::peerSlotSize(protocol)
                      : storage_v2::contactSlotSize(protocol);
            maintenance_protocol_ = protocol;
            char final_name[40] = {};
            char temp_name[40] = {};
            char backup_name[40] = {};
            char delta_name[40] = {};
            std::snprintf(final_name,
                          sizeof(final_name),
                          "%s.snapshot",
                          base);
            std::snprintf(temp_name,
                          sizeof(temp_name),
                          "%s.snapshot.tmp",
                          base);
            std::snprintf(backup_name,
                          sizeof(backup_name),
                          "%s.snapshot.bak",
                          base);
            std::snprintf(delta_name,
                          sizeof(delta_name),
                          "%s.delta",
                          base);
            buildProtocolPath(protocol,
                              final_name,
                              maintenance_final_path_,
                              sizeof(maintenance_final_path_));
            buildProtocolPath(protocol,
                              temp_name,
                              maintenance_path_,
                              sizeof(maintenance_path_));
            buildProtocolPath(protocol,
                              backup_name,
                              maintenance_backup_path_,
                              sizeof(maintenance_backup_path_));
            buildProtocolPath(protocol,
                              delta_name,
                              maintenance_delta_path_,
                              sizeof(maintenance_delta_path_));
            (void)storage_runtime::sd_remove(maintenance_path_);
            if (!journal_.create(maintenance_path_,
                                 protocol,
                                 maintenance_kind_,
                                 maintenance_slot_size_))
            {
                return maintenanceFailure(
                    storage_contracts::StorageOperationResultKind::IoError);
            }
            maintenance_.compaction_record_index = 0U;
            maintenance_.phase = MaintenancePhase::CompactionWrite;
            break;
        }

        case MaintenancePhase::CompactionWrite:
        {
            const MeshProtocol protocol =
                kProtocols[maintenance_.protocol_index];
            const bool peers =
                maintenance_.compaction_projection_index == 0U;
            while (true)
            {
                if (peers)
                {
                    if (maintenance_.compaction_record_index >=
                        compaction_peers_.size())
                    {
                        break;
                    }
                    const MeshPeerRecord& peer =
                        compaction_peers_[maintenance_.compaction_record_index++];
                    if (!meshPeerSameProtocol(peer.identity.protocol,
                                              protocol))
                    {
                        continue;
                    }
                    const storage_v2::PeerProjection projection{peer, false};
                    if (!storage_v2::encodePeerSlot(
                            protocol,
                            projection,
                            maintenance_scratch_.data(),
                            maintenance_slot_size_))
                    {
                        return maintenanceFailure(
                            storage_contracts::StorageOperationResultKind::
                                IoError);
                    }
                }
                else
                {
                    if (maintenance_.compaction_record_index >=
                        compaction_contacts_.size())
                    {
                        break;
                    }
                    const auto& contact =
                        compaction_contacts_[maintenance_.compaction_record_index++];
                    if (!meshPeerSameProtocol(contact.identity.protocol,
                                              protocol))
                    {
                        continue;
                    }
                    if (!storage_v2::encodeContactSlot(
                            protocol,
                            contact,
                            maintenance_scratch_.data(),
                            maintenance_slot_size_))
                    {
                        return maintenanceFailure(
                            storage_contracts::StorageOperationResultKind::
                                IoError);
                    }
                }
                if (!journal_.append(maintenance_path_,
                                     protocol,
                                     maintenance_kind_,
                                     maintenance_slot_size_,
                                     maintenance_scratch_.data()))
                {
                    return maintenanceFailure(
                        storage_contracts::StorageOperationResultKind::IoError);
                }
                return storage_contracts::StorageOperationResult::
                    inProgressResult(maintenance_.operation,
                                     maintenance_.generation);
            }
            maintenance_.phase = MaintenancePhase::CompactionReplace;
            break;
        }

        case MaintenancePhase::CompactionReplace:
        {
            if (!storage_v2::replaceFileAtomically(
                    maintenance_path_,
                    maintenance_final_path_,
                    maintenance_backup_path_))
            {
                return maintenanceFailure(
                    storage_contracts::StorageOperationResultKind::IoError);
            }
            const MeshProtocol protocol =
                kProtocols[maintenance_.protocol_index];
            const bool peers =
                maintenance_.compaction_projection_index == 0U;
            const storage_v2::JournalKind delta_kind =
                peers ? storage_v2::JournalKind::PeerDelta
                      : storage_v2::JournalKind::ContactDelta;
            if (!journal_.create(maintenance_delta_path_,
                                 protocol,
                                 delta_kind,
                                 maintenance_slot_size_))
            {
                return maintenanceFailure(
                    storage_contracts::StorageOperationResultKind::IoError);
            }
            {
                ScopedRepositoryLock lock(mutex_);
                if (!lock.locked())
                {
                    return maintenanceFailure(
                        stateLockFailure(lock.result()));
                }
                if (peers)
                {
                    partitions_[protocolIndex(protocol)].peer_delta_count =
                        0U;
                }
                else
                {
                    partitions_[protocolIndex(protocol)].contact_delta_count =
                        0U;
                }
            }
            ++maintenance_.compaction_projection_index;
            maintenance_.phase = MaintenancePhase::CompactionCreate;
            break;
        }

        case MaintenancePhase::CompactionAdvance:
            ++maintenance_.protocol_index;
            if (maintenance_.protocol_index >=
                static_cast<uint8_t>(sizeof(kProtocols) /
                                     sizeof(kProtocols[0])))
            {
                ScopedRepositoryLock lock(mutex_);
                if (!lock.locked())
                {
                    return maintenanceFailure(
                        stateLockFailure(lock.result()));
                }
                if (pending_peer_revision_ == compaction_peer_revision_)
                {
                    pending_peer_deltas_.clear();
                    pending_peer_head_ = 0U;
                }
                if (pending_contact_revision_ == compaction_contact_revision_)
                {
                    pending_contact_deltas_.clear();
                    pending_contact_head_ = 0U;
                }
                for (std::size_t index = 0U; index < 3U; ++index)
                {
                    if (protocol_reset_pending_[index] &&
                        protocol_reset_revision_[index] ==
                            compaction_reset_revision_[index])
                    {
                        protocol_reset_pending_[index] = false;
                    }
                }
                refreshPersistenceDemandLocked();
                compaction_peers_.clear();
                compaction_contacts_.clear();
                maintenance_.phase = MaintenancePhase::Complete;
                return storage_contracts::StorageOperationResult::
                    completedResult(maintenance_.operation,
                                    maintenance_.generation);
            }
            maintenance_.compaction_inspection_index = 0U;
            maintenance_.compaction_projection_index = 0U;
            maintenance_.phase = MaintenancePhase::CompactionInspect;
            break;

        case MaintenancePhase::Complete:
            return storage_contracts::StorageOperationResult::completedResult(
                maintenance_.operation,
                maintenance_.generation);

        default:
            return maintenanceFailure(
                storage_contracts::StorageOperationResultKind::StateBusy);
        }
    }
    return storage_contracts::StorageOperationResult::inProgressResult(
        maintenance_.operation,
        maintenance_.generation);
}

MeshPeerDirectoryStatus SdProtocolPeerRepository::begin()
{
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked())
    {
        return stateLockStatus(lock.result());
    }
    if (begun_)
    {
        return MeshPeerDirectoryStatus::success();
    }

    begun_ = true;
    Serial.printf("[PeerStoreV2] begun=1 hydration=pending root=%s\n", kRoot);
    return MeshPeerDirectoryStatus::success();
}

bool SdProtocolPeerRepository::ensureLayout()
{
    if (!storage_runtime::sd_card_ready() || !ensureDirectory("/data") ||
        !ensureDirectory(kRoot))
    {
        return false;
    }
    bool ok = true;
    for (MeshProtocol protocol : kProtocols)
    {
        ok = ensureProtocolLayout(protocol) && ok;
    }
    return ok;
}

bool SdProtocolPeerRepository::ensureProtocolLayout(MeshProtocol protocol)
{
    char path[64] = {};
    buildProtocolPath(protocol, nullptr, path, sizeof(path));
    return ensureDirectory(path);
}

bool SdProtocolPeerRepository::rewritePeerSnapshotFrom(
    MeshProtocol protocol,
    const PeerVector& snapshot)
{
    protocol = normalizeProtocol(protocol);
    char target[96] = {};
    char temp[96] = {};
    char backup[96] = {};
    char delta[96] = {};
    buildProtocolPath(protocol, "peers.snapshot", target, sizeof(target));
    buildProtocolPath(protocol, "peers.snapshot.tmp", temp, sizeof(temp));
    buildProtocolPath(protocol,
                      "peers.snapshot.bak",
                      backup,
                      sizeof(backup));
    buildProtocolPath(protocol, "peers.delta", delta, sizeof(delta));

    (void)storage_runtime::sd_remove(temp);
    const std::size_t slot_size = storage_v2::peerSlotSize(protocol);
    if (slot_size == 0U || slot_size > slot_scratch_.size() ||
        !journal_.create(temp,
                         protocol,
                         storage_v2::JournalKind::PeerSnapshot,
                         slot_size))
    {
        return false;
    }

    for (const MeshPeerRecord& peer : snapshot)
    {
        if (!meshPeerSameProtocol(peer.identity.protocol, protocol))
        {
            continue;
        }
        const storage_v2::PeerProjection projection{peer, false};
        if (!storage_v2::encodePeerSlot(protocol,
                                        projection,
                                        slot_scratch_.data(),
                                        slot_size) ||
            !journal_.append(temp,
                             protocol,
                             storage_v2::JournalKind::PeerSnapshot,
                             slot_size,
                             slot_scratch_.data()))
        {
            (void)storage_runtime::sd_remove(temp);
            return false;
        }
    }

    if (!storage_v2::replaceFileAtomically(temp, target, backup) ||
        !journal_.create(delta,
                         protocol,
                         storage_v2::JournalKind::PeerDelta,
                         slot_size))
    {
        return false;
    }
    return true;
}

bool SdProtocolPeerRepository::appendPeerDelta(
    const storage_v2::PeerProjection& projection)
{
    const MeshProtocol protocol = normalizeProtocol(
        projection.record.identity.protocol);
    const std::size_t slot_size = storage_v2::peerSlotSize(protocol);
    if (slot_size == 0U || slot_size > slot_scratch_.size() ||
        !storage_v2::encodePeerSlot(protocol,
                                    projection,
                                    slot_scratch_.data(),
                                    slot_size))
    {
        return false;
    }
    char path[96] = {};
    buildProtocolPath(protocol, "peers.delta", path, sizeof(path));
    if (!journal_.append(path,
                         protocol,
                         storage_v2::JournalKind::PeerDelta,
                         slot_size,
                         slot_scratch_.data()))
    {
        return false;
    }
    return true;
}

bool SdProtocolPeerRepository::appendContactDelta(
    const storage_v2::ContactProjection& projection)
{
    const MeshProtocol protocol = normalizeProtocol(projection.identity.protocol);
    const std::size_t slot_size = storage_v2::contactSlotSize(protocol);
    if (slot_size == 0U || slot_size > slot_scratch_.size() ||
        !storage_v2::encodeContactSlot(protocol,
                                       projection,
                                       slot_scratch_.data(),
                                       slot_size))
    {
        return false;
    }
    char path[96] = {};
    buildProtocolPath(protocol, "contacts.delta", path, sizeof(path));
    if (!journal_.append(path,
                         protocol,
                         storage_v2::JournalKind::ContactDelta,
                         slot_size,
                         slot_scratch_.data()))
    {
        return false;
    }
    return true;
}

bool SdProtocolPeerRepository::queuePeerDelta(
    const storage_v2::PeerProjection& projection)
{
    prunePendingDeltasLocked();
    if (!pending_peer_deltas_.empty())
    {
        storage_v2::PeerProjection& newest = pending_peer_deltas_.back();
        if (sameMeshPeerIdentity(newest.record.identity,
                                 projection.record.identity))
        {
            newest = projection;
            ++pending_peer_revision_;
            refreshPersistenceDemandLocked();
            return false;
        }
    }
    pending_peer_deltas_.push_back(projection);
    ++pending_peer_revision_;
    refreshPersistenceDemandLocked();
    return false;
}

bool SdProtocolPeerRepository::queueContactDelta(
    const storage_v2::ContactProjection& projection)
{
    prunePendingDeltasLocked();
    if (!pending_contact_deltas_.empty())
    {
        storage_v2::ContactProjection& newest = pending_contact_deltas_.back();
        if (sameMeshPeerIdentity(newest.identity, projection.identity))
        {
            newest = projection;
            ++pending_contact_revision_;
            refreshPersistenceDemandLocked();
            return true;
        }
    }
    pending_contact_deltas_.push_back(projection);
    ++pending_contact_revision_;
    refreshPersistenceDemandLocked();
    return true;
}

void SdProtocolPeerRepository::prunePendingDeltasLocked()
{
    if (pending_peer_head_ == pending_peer_deltas_.size())
    {
        pending_peer_deltas_.clear();
        pending_peer_head_ = 0U;
    }
    if (pending_contact_head_ == pending_contact_deltas_.size())
    {
        pending_contact_deltas_.clear();
        pending_contact_head_ = 0U;
    }
}

void SdProtocolPeerRepository::refreshPersistenceDemandLocked()
{
    const bool has_pending_deltas =
        pending_peer_head_ < pending_peer_deltas_.size() ||
        pending_contact_head_ < pending_contact_deltas_.size();
    persistence_pending_.store(has_pending_deltas,
                               std::memory_order_release);

    bool needs_compaction = false;
    for (std::size_t index = 0U; index < 3U; ++index)
    {
        needs_compaction =
            needs_compaction || protocol_reset_pending_[index] ||
            partitions_[index].peer_delta_count >=
                kBootCompactionDeltaThreshold ||
            partitions_[index].contact_delta_count >=
                kBootCompactionDeltaThreshold;
    }
    compaction_pending_.store(needs_compaction,
                              std::memory_order_release);
}

bool SdProtocolPeerRepository::applyPeerProjection(
    const storage_v2::PeerProjection& projection)
{
    if (!meshPeerRecordIsValid(projection.record))
    {
        return false;
    }
    const std::size_t index = findPeerIndex(projection.record.identity);
    if (projection.deleted)
    {
        if (index < peers_.size())
        {
            peers_.erase(peers_.begin() + static_cast<std::ptrdiff_t>(index));
        }
        return true;
    }
    if (index < peers_.size())
    {
        peers_[index] = projection.record;
    }
    else
    {
        peers_.push_back(projection.record);
    }
    return true;
}

bool SdProtocolPeerRepository::applyContactProjection(
    const storage_v2::ContactProjection& projection)
{
    if (!meshPeerIdentityIsValid(projection.identity))
    {
        return false;
    }
    const std::size_t index = findContactIndex(projection.identity);
    if (projection.deleted)
    {
        if (index < contacts_.size())
        {
            contacts_.erase(contacts_.begin() +
                            static_cast<std::ptrdiff_t>(index));
        }
        return true;
    }
    if (index < contacts_.size())
    {
        contacts_[index] = projection;
    }
    else
    {
        contacts_.push_back(projection);
    }
    return true;
}

void SdProtocolPeerRepository::overlayContactFacts()
{
    for (MeshPeerRecord& peer : peers_)
    {
        peer.flags = MeshPeerUserFlags{};
        peer.user_alias[0] = '\0';
    }
    for (const storage_v2::ContactProjection& contact : contacts_)
    {
        std::size_t peer_index = findPeerIndex(contact.identity);
        if (peer_index >= peers_.size())
        {
            MeshPeerRecord placeholder{};
            placeholder.valid = true;
            placeholder.identity = contact.identity;
            placeholder.source = MeshPeerSource::Manual;
            if (placeholder.identity.protocol == MeshProtocol::MeshCore)
            {
                placeholder.meshcore.has_public_key = true;
                placeholder.meshcore.node_id_hint = contact.node_id_hint;
                std::memcpy(placeholder.meshcore.public_key,
                            placeholder.identity.public_key,
                            kMeshPeerMeshCorePublicKeyLen);
            }
            else if (placeholder.identity.protocol == MeshProtocol::Reticulum)
            {
                placeholder.reticulum.identity =
                    placeholder.identity.reticulum;
            }
            peers_.push_back(placeholder);
            peer_index = peers_.size() - 1U;
        }
        MeshPeerRecord& peer = peers_[peer_index];
        peer.flags = contact.flags;
        copyMeshPeerText(peer.user_alias,
                         sizeof(peer.user_alias),
                         contact.alias);
        if (peer.identity.protocol == MeshProtocol::MeshCore &&
            peer.meshcore.node_id_hint == 0U)
        {
            peer.meshcore.node_id_hint = contact.node_id_hint;
        }
    }
}

void SdProtocolPeerRepository::overlayContactFactsForPeer(
    MeshPeerRecord& peer) const
{
    peer.flags = MeshPeerUserFlags{};
    peer.user_alias[0] = '\0';
    MeshPeerIdentity stable{};
    if (!stableContactIdentity(peer, stable))
    {
        return;
    }
    const std::size_t contact_index = findContactIndex(stable);
    if (contact_index >= contacts_.size())
    {
        return;
    }
    const storage_v2::ContactProjection& contact = contacts_[contact_index];
    peer.flags = contact.flags;
    copyMeshPeerText(peer.user_alias,
                     sizeof(peer.user_alias),
                     contact.alias);
    if (peer.identity.protocol == MeshProtocol::MeshCore &&
        peer.meshcore.node_id_hint == 0U)
    {
        peer.meshcore.node_id_hint = contact.node_id_hint;
    }
}

void SdProtocolPeerRepository::reconcileStableIdentities(MeshProtocol protocol)
{
    protocol = normalizeProtocol(protocol);
    for (std::size_t stable_index = 0U; stable_index < peers_.size();
         ++stable_index)
    {
        MeshPeerRecord& stable = peers_[stable_index];
        if (!meshPeerSameProtocol(stable.identity.protocol, protocol) ||
            stable.identity.kind == MeshPeerIdentityKind::NodeId)
        {
            continue;
        }
        const NodeId node_id = projectedNodeId(stable);
        if (node_id == 0U)
        {
            continue;
        }
        for (std::size_t unresolved_index = 0U;
             unresolved_index < peers_.size(); ++unresolved_index)
        {
            if (unresolved_index == stable_index)
            {
                continue;
            }
            const MeshPeerRecord& unresolved = peers_[unresolved_index];
            if (!meshPeerSameProtocol(unresolved.identity.protocol, protocol) ||
                unresolved.identity.kind != MeshPeerIdentityKind::NodeId ||
                unresolved.identity.node_id != node_id)
            {
                continue;
            }
            MeshPeerRecord merged = mergeMeshPeerRecordFacts(unresolved, stable);
            merged.identity = stable.identity;
            peers_[stable_index] = merged;
            peers_.erase(peers_.begin() +
                         static_cast<std::ptrdiff_t>(unresolved_index));
            if (unresolved_index < stable_index)
            {
                --stable_index;
            }
            break;
        }
    }
}

bool SdProtocolPeerRepository::queueDeferredObservation(
    const MeshPeerRecord& record)
{
    if (!pending_observation_mutex_ ||
        xSemaphoreTake(pending_observation_mutex_, pdMS_TO_TICKS(5)) != pdTRUE)
    {
        return false;
    }
    if (pending_peer_observations_.size() >= kPendingObservationCapacity)
    {
        pending_peer_observations_.erase(pending_peer_observations_.begin());
        ++dropped_peer_observations_;
        Serial.printf("[PeerStoreV2] deferred observation drop_oldest dropped=%lu\n",
                      static_cast<unsigned long>(dropped_peer_observations_));
    }
    pending_peer_observations_.push_back(record);
    xSemaphoreGive(pending_observation_mutex_);
    return true;
}

void SdProtocolPeerRepository::drainDeferredObservationsLocked()
{
    if (!pending_observation_mutex_ ||
        xSemaphoreTake(pending_observation_mutex_, pdMS_TO_TICKS(5)) != pdTRUE)
    {
        return;
    }
    for (const MeshPeerRecord& observation : pending_peer_observations_)
    {
        const MeshPeerDirectoryStatus status = recordLocked(observation);
        if (!status.succeeded())
        {
            Serial.printf("[PeerStoreV2] deferred observation replay failed code=%u\n",
                          static_cast<unsigned>(status.code));
        }
    }
    pending_peer_observations_.clear();
    xSemaphoreGive(pending_observation_mutex_);
}

MeshPeerDirectoryStatus SdProtocolPeerRepository::record(
    const MeshPeerRecord& input)
{
    if (!meshPeerRecordIsValid(input))
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::InvalidArgument);
    }
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked())
    {
        (void)queueDeferredObservation(input);
        return stateLockStatus(lock.result());
    }
    if (!begun_ || !hydrated_ ||
        hydrating_.load(std::memory_order_acquire))
    {
        (void)queueDeferredObservation(input);
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::StorageUnavailable);
    }
    drainDeferredObservationsLocked();
    return recordLocked(input);
}

MeshPeerDirectoryStatus SdProtocolPeerRepository::recordLocked(
    const MeshPeerRecord& input)
{

    MeshPeerRecord incoming = input;
    incoming.user_alias[0] = '\0';
    incoming.flags = {};
    incoming.identity.protocol = normalizeProtocol(incoming.identity.protocol);
    if (incoming.identity.protocol == MeshProtocol::Reticulum &&
        incoming.identity.kind == MeshPeerIdentityKind::ReticulumDestination)
    {
        incoming.reticulum.identity = incoming.identity.reticulum;
    }
    const std::size_t exact_index = findPeerIndex(incoming.identity);
    std::size_t merge_index = exact_index;
    if (merge_index >= peers_.size() &&
        incoming.identity.kind != MeshPeerIdentityKind::NodeId)
    {
        const NodeId node_id = projectedNodeId(incoming);
        if (node_id != 0U)
        {
            merge_index = findPeerIndexByNodeId(incoming.identity.protocol,
                                                node_id);
        }
    }

    if (merge_index >= peers_.size() &&
        ephemeralCount(incoming.identity.protocol) >= kEphemeralPeerCapacity &&
        !evictOldestEphemeral(incoming.identity.protocol))
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::CapacityExceeded);
    }

    MeshPeerRecord next = incoming;
    MeshPeerIdentity replaced_identity{};
    bool identity_upgrade = false;
    if (merge_index < peers_.size())
    {
        replaced_identity = peers_[merge_index].identity;
        next = mergeMeshPeerRecordFacts(peers_[merge_index], incoming);
        if (!sameMeshPeerIdentity(replaced_identity, incoming.identity) &&
            incoming.identity.kind != MeshPeerIdentityKind::NodeId)
        {
            next.identity = incoming.identity;
            identity_upgrade = true;
        }
    }
    next.valid = true;
    next.identity.protocol = normalizeProtocol(next.identity.protocol);
    if (next.last_seen_s < next.first_seen_s)
    {
        next.last_seen_s = next.first_seen_s;
    }

    const storage_v2::PeerProjection next_projection{next, false};
    (void)queuePeerDelta(next_projection);
    if (identity_upgrade)
    {
        storage_v2::PeerProjection tombstone{};
        tombstone.record = peers_[merge_index];
        tombstone.deleted = true;
        (void)queuePeerDelta(tombstone);
    }

    if (merge_index < peers_.size())
    {
        peers_[merge_index] = next;
    }
    else
    {
        peers_.push_back(next);
    }
    if (merge_index < peers_.size())
    {
        overlayContactFactsForPeer(peers_[merge_index]);
    }
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus SdProtocolPeerRepository::find(
    const MeshPeerIdentity& identity,
    MeshPeerRecord& out_record)
{
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked())
    {
        return stateLockStatus(lock.result());
    }
    const std::size_t index = findPeerIndex(identity);
    if (index >= peers_.size())
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::NotFound);
    }
    out_record = peers_[index];
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus SdProtocolPeerRepository::findByNodeId(
    MeshProtocol protocol,
    NodeId node_id,
    MeshPeerRecord& out_record)
{
    if (node_id == 0U)
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::InvalidArgument);
    }
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked())
    {
        return stateLockStatus(lock.result());
    }
    const std::size_t index = findPeerIndexByNodeId(protocol, node_id);
    if (index >= peers_.size())
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::NotFound);
    }
    out_record = peers_[index];
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus SdProtocolPeerRepository::loadRecent(
    MeshProtocol protocol,
    MeshPeerRecord* out_records,
    std::size_t max_records,
    std::size_t* out_count)
{
    if (!out_count || (!out_records && max_records > 0U))
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::InvalidArgument);
    }
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked())
    {
        return stateLockStatus(lock.result());
    }
    protocol = normalizeProtocol(protocol);
    using PeerPtrVector = std::vector<
        const MeshPeerRecord*,
        ::platform::esp::arduino_common::memory::PsramAllocator<
            const MeshPeerRecord*>>;
    PeerPtrVector matches;
    matches.reserve(peers_.size());
    for (const MeshPeerRecord& peer : peers_)
    {
        if (meshPeerSameProtocol(peer.identity.protocol, protocol))
        {
            matches.push_back(&peer);
        }
    }
    std::sort(matches.begin(),
              matches.end(),
              [](const MeshPeerRecord* lhs, const MeshPeerRecord* rhs)
              {
                  return lhs->last_seen_s > rhs->last_seen_s;
              });
    *out_count = std::min(max_records, matches.size());
    for (std::size_t index = 0U; index < *out_count; ++index)
    {
        out_records[index] = *matches[index];
    }
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus SdProtocolPeerRepository::search(
    MeshProtocol protocol,
    const char* query,
    MeshPeerRecord* out_records,
    std::size_t max_records,
    std::size_t* out_count)
{
    if (!query || !out_count || (!out_records && max_records > 0U))
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::InvalidArgument);
    }
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked())
    {
        return stateLockStatus(lock.result());
    }
    protocol = normalizeProtocol(protocol);
    using PeerPtrVector = std::vector<
        const MeshPeerRecord*,
        ::platform::esp::arduino_common::memory::PsramAllocator<
            const MeshPeerRecord*>>;
    PeerPtrVector matches;
    matches.reserve(peers_.size());
    for (const MeshPeerRecord& peer : peers_)
    {
        const MeshPeerNodeFacts* facts = nodeFacts(peer);
        if (!meshPeerSameProtocol(peer.identity.protocol, protocol) ||
            (!textContains(peer.user_alias, query) &&
             !textContains(peer.display_name, query) &&
             (!facts ||
              (!textContains(facts->short_name, query) &&
               !textContains(facts->long_name, query)))))
        {
            continue;
        }
        matches.push_back(&peer);
    }
    std::sort(matches.begin(),
              matches.end(),
              [](const MeshPeerRecord* lhs, const MeshPeerRecord* rhs)
              {
                  return lhs->last_seen_s > rhs->last_seen_s;
              });
    *out_count = std::min(max_records, matches.size());
    for (std::size_t index = 0U; index < *out_count; ++index)
    {
        out_records[index] = *matches[index];
    }
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus SdProtocolPeerRepository::setUserFlags(
    const MeshPeerIdentity& identity,
    const MeshPeerUserFlags& flags)
{
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked())
    {
        return stateLockStatus(lock.result());
    }
    const std::size_t peer_index = findPeerIndex(identity);
    if (peer_index >= peers_.size())
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::NotFound);
    }
    MeshPeerIdentity stable{};
    if (!stableContactIdentity(peers_[peer_index], stable))
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::Unsupported);
    }
    const bool remove_projection = !flags.favorite && !flags.ignored &&
                                   !flags.trusted &&
                                   peers_[peer_index].user_alias[0] == '\0';
    if (!persistContactFacts(stable,
                             flags,
                             peers_[peer_index].user_alias,
                             remove_projection))
    {
        return MeshPeerDirectoryStatus::fail(MeshPeerDirectoryStatusCode::IoError);
    }
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus SdProtocolPeerRepository::visit(
    MeshProtocol protocol,
    MeshPeerDirectoryView view,
    IMeshPeerDirectoryVisitor& visitor)
{
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked())
    {
        return stateLockStatus(lock.result());
    }
    if (!begun_ || !hydrated_ ||
        hydrating_.load(std::memory_order_acquire))
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::StorageUnavailable);
    }
    const MeshProtocol normalized = normalizeProtocol(protocol);
    for (const MeshPeerRecord& peer : peers_)
    {
        if (!meshPeerSameProtocol(peer.identity.protocol, normalized))
        {
            continue;
        }
        const bool contact = meshPeerIsContact(peer);
        const bool matches =
            view == MeshPeerDirectoryView::All ||
            (view == MeshPeerDirectoryView::Contacts && contact) ||
            (view == MeshPeerDirectoryView::Nearby && !contact &&
             !peer.flags.ignored) ||
            (view == MeshPeerDirectoryView::Ignored && !contact &&
             peer.flags.ignored);
        if (matches && !visitor.visit(peer))
        {
            break;
        }
    }
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus SdProtocolPeerRepository::setUserAlias(
    const MeshPeerIdentity& identity,
    const char* alias)
{
    if (!alias || std::strlen(alias) > kMeshPeerUserAliasMaxLen)
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::InvalidArgument);
    }
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked())
    {
        return stateLockStatus(lock.result());
    }
    const std::size_t peer_index = findPeerIndex(identity);
    if (peer_index >= peers_.size())
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::NotFound);
    }
    MeshPeerIdentity stable{};
    if (!stableContactIdentity(peers_[peer_index], stable))
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::Unsupported);
    }
    MeshPeerUserFlags flags = peers_[peer_index].flags;
    flags.favorite = alias[0] != '\0';
    const bool remove_projection = alias[0] == '\0' && !flags.ignored &&
                                   !flags.trusted;
    return persistContactFacts(stable, flags, alias, remove_projection)
               ? MeshPeerDirectoryStatus::success()
               : MeshPeerDirectoryStatus::fail(
                     MeshPeerDirectoryStatusCode::IoError);
}

MeshPeerDirectoryStatus SdProtocolPeerRepository::setKeyManuallyVerified(
    const MeshPeerIdentity& identity,
    bool verified)
{
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked())
    {
        return stateLockStatus(lock.result());
    }
    const std::size_t index = findPeerIndex(identity);
    if (index >= peers_.size())
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::NotFound);
    }
    MeshPeerRecord next = peers_[index];
    if (next.identity.protocol == MeshProtocol::Meshtastic &&
        next.meshtastic.has_public_key)
    {
        next.meshtastic.key_manually_verified = verified;
    }
    else if (next.identity.protocol == MeshProtocol::MeshCore &&
             (next.meshcore.has_public_key ||
              next.identity.kind == MeshPeerIdentityKind::PublicKey))
    {
        next.meshcore.public_key_verified = verified;
    }
    else
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::Unsupported);
    }
    (void)queuePeerDelta(storage_v2::PeerProjection{next, false});
    peers_[index] = next;
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus SdProtocolPeerRepository::remove(
    const MeshPeerIdentity& identity)
{
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked())
    {
        return stateLockStatus(lock.result());
    }
    const std::size_t index = findPeerIndex(identity);
    if (index >= peers_.size())
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::NotFound);
    }
    if (peerIsProtected(peers_[index]))
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::Unsupported);
    }
    storage_v2::PeerProjection tombstone{};
    tombstone.record = peers_[index];
    tombstone.deleted = true;
    (void)queuePeerDelta(tombstone);
    peers_.erase(peers_.begin() + static_cast<std::ptrdiff_t>(index));
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus SdProtocolPeerRepository::clearProtocol(
    MeshProtocol protocol)
{
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked())
    {
        return stateLockStatus(lock.result());
    }
    if (!begun_)
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::StorageUnavailable);
    }
    protocol = normalizeProtocol(protocol);
    peers_.erase(std::remove_if(peers_.begin(),
                                peers_.end(),
                                [protocol](const MeshPeerRecord& peer)
                                {
                                    return meshPeerSameProtocol(
                                        peer.identity.protocol,
                                        protocol);
                                }),
                 peers_.end());
    if (pending_peer_head_ > 0U)
    {
        pending_peer_deltas_.erase(
            pending_peer_deltas_.begin(),
            pending_peer_deltas_.begin() +
                static_cast<std::ptrdiff_t>(pending_peer_head_));
        pending_peer_head_ = 0U;
    }
    pending_peer_deltas_.erase(
        std::remove_if(pending_peer_deltas_.begin(),
                       pending_peer_deltas_.end(),
                       [protocol](const storage_v2::PeerProjection& projection)
                       {
                           return meshPeerSameProtocol(
                               projection.record.identity.protocol,
                               protocol);
                       }),
        pending_peer_deltas_.end());
    prunePendingDeltasLocked();
    ++pending_peer_revision_;
    overlayContactFacts();
    const std::size_t index = protocolIndex(protocol);
    protocol_reset_pending_[index] = true;
    ++protocol_reset_revision_[index];
    if (protocol_reset_revision_[index] == 0U)
    {
        protocol_reset_revision_[index] = 1U;
    }
    refreshPersistenceDemandLocked();
    Serial.printf("[PeerStoreV2] protocol_reset queued protocol=%s\n",
                  protocolSlug(protocol));
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryCapacity SdProtocolPeerRepository::capacityFor(
    MeshProtocol protocol) const
{
    const std::size_t index = protocolIndex(protocol);
    return MeshPeerDirectoryCapacity{kEphemeralPeerCapacity,
                                     kPeerHotCacheCapacity[index]};
}

MeshPeerDirectoryStatus SdProtocolPeerRepository::flushProtocolReset()
{
    MeshProtocol protocol = MeshProtocol::Meshtastic;
    uint32_t reset_revision = 0U;
    {
        ScopedRepositoryLock lock(mutex_);
        if (!lock.locked())
        {
            return stateLockStatus(lock.result());
        }
        std::size_t index = 0U;
        while (index < 3U && !protocol_reset_pending_[index])
        {
            ++index;
        }
        if (index >= 3U)
        {
            return MeshPeerDirectoryStatus::success();
        }
        protocol = kProtocols[index];
        reset_revision = protocol_reset_revision_[index];
        flush_peer_snapshot_ = peers_;
    }

    if (!rewritePeerSnapshotFrom(protocol, flush_peer_snapshot_))
    {
        return ioFailureStatus();
    }

    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked())
    {
        return stateLockStatus(lock.result());
    }
    const std::size_t index = protocolIndex(protocol);
    if (protocol_reset_revision_[index] == reset_revision)
    {
        protocol_reset_pending_[index] = false;
        partitions_[index].peer_delta_count = 0U;
    }
    refreshPersistenceDemandLocked();
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus SdProtocolPeerRepository::flushPendingDeltas(
    std::size_t budget)
{
    flush_peer_batch_.clear();
    flush_contact_batch_.clear();
    std::size_t peer_start = 0U;
    std::size_t contact_start = 0U;
    uint32_t peer_revision = 0U;
    uint32_t contact_revision = 0U;
    {
        ScopedRepositoryLock lock(mutex_);
        if (!lock.locked())
        {
            return stateLockStatus(lock.result());
        }
        prunePendingDeltasLocked();
        peer_start = pending_peer_head_;
        contact_start = pending_contact_head_;
        peer_revision = pending_peer_revision_;
        contact_revision = pending_contact_revision_;
        const std::size_t peer_end =
            std::min(pending_peer_deltas_.size(), peer_start + budget);
        const std::size_t contact_end =
            std::min(pending_contact_deltas_.size(), contact_start + budget);
        flush_peer_batch_.assign(pending_peer_deltas_.begin() +
                                     static_cast<std::ptrdiff_t>(peer_start),
                                 pending_peer_deltas_.begin() +
                                     static_cast<std::ptrdiff_t>(peer_end));
        flush_contact_batch_.assign(pending_contact_deltas_.begin() +
                                        static_cast<std::ptrdiff_t>(
                                            contact_start),
                                    pending_contact_deltas_.begin() +
                                        static_cast<std::ptrdiff_t>(contact_end));
    }

    std::size_t peer_written = 0U;
    std::size_t contact_written = 0U;
    uint32_t peer_counts[3] = {};
    uint32_t contact_counts[3] = {};
    for (const auto& projection : flush_peer_batch_)
    {
        if (!appendPeerDelta(projection))
        {
            break;
        }
        ++peer_written;
        ++peer_counts[protocolIndex(projection.record.identity.protocol)];
    }
    if (peer_written != flush_peer_batch_.size())
    {
        ScopedRepositoryLock lock(mutex_);
        if (!lock.locked())
        {
            return stateLockStatus(lock.result());
        }
        for (std::size_t index = 0U; index < 3U; ++index)
        {
            partitions_[index].peer_delta_count += peer_counts[index];
        }
        if (pending_peer_revision_ == peer_revision)
        {
            pending_peer_head_ = peer_start + peer_written;
            prunePendingDeltasLocked();
        }
        refreshPersistenceDemandLocked();
        return ioFailureStatus();
    }
    for (const auto& projection : flush_contact_batch_)
    {
        if (!appendContactDelta(projection))
        {
            break;
        }
        ++contact_written;
        ++contact_counts[protocolIndex(projection.identity.protocol)];
    }

    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked())
    {
        return stateLockStatus(lock.result());
    }
    for (std::size_t index = 0U; index < 3U; ++index)
    {
        partitions_[index].peer_delta_count += peer_counts[index];
        partitions_[index].contact_delta_count += contact_counts[index];
    }
    if (pending_peer_revision_ == peer_revision)
    {
        pending_peer_head_ = peer_start + peer_written;
        prunePendingDeltasLocked();
    }
    if (pending_contact_revision_ == contact_revision)
    {
        pending_contact_head_ = contact_start + contact_written;
        prunePendingDeltasLocked();
    }
    refreshPersistenceDemandLocked();
    return contact_written == flush_contact_batch_.size()
               ? MeshPeerDirectoryStatus::success()
               : ioFailureStatus();
}

MeshPeerDirectoryStatus SdProtocolPeerRepository::flush()
{
    if (!acquirePersistenceLease(kPersistenceLeaseWaitTicks))
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::Busy);
    }
    bool begun = false;
    MeshPeerDirectoryStatus lock_status =
        MeshPeerDirectoryStatus::success();
    {
        ScopedRepositoryLock lock(mutex_);
        if (!lock.locked())
        {
            lock_status = stateLockStatus(lock.result());
        }
        else
        {
            begun = begun_;
        }
    }
    if (!lock_status.succeeded())
    {
        releasePersistenceLease();
        return lock_status;
    }
    if (!begun)
    {
        releasePersistenceLease();
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::StorageUnavailable);
    }

    const MeshPeerDirectoryStatus reset_status = flushProtocolReset();
    if (!reset_status.succeeded())
    {
        releasePersistenceLease();
        return reset_status;
    }
    const MeshPeerDirectoryStatus deltas_status =
        flushPendingDeltas(kPendingFlushBudget);
    releasePersistenceLease();
    return deltas_status;
}

std::size_t SdProtocolPeerRepository::findPeerIndex(
    const MeshPeerIdentity& identity) const
{
    for (std::size_t index = 0U; index < peers_.size(); ++index)
    {
        if (sameMeshPeerIdentity(peers_[index].identity, identity))
        {
            return index;
        }
    }
    return peers_.size();
}

std::size_t SdProtocolPeerRepository::findPeerIndexByNodeId(
    MeshProtocol protocol,
    NodeId node_id) const
{
    protocol = normalizeProtocol(protocol);
    for (std::size_t index = 0U; index < peers_.size(); ++index)
    {
        const MeshPeerRecord& peer = peers_[index];
        if (meshPeerSameProtocol(peer.identity.protocol, protocol) &&
            projectedNodeId(peer) == node_id)
        {
            return index;
        }
    }
    return peers_.size();
}

std::size_t SdProtocolPeerRepository::findContactIndex(
    const MeshPeerIdentity& identity) const
{
    for (std::size_t index = 0U; index < contacts_.size(); ++index)
    {
        if (sameMeshPeerIdentity(contacts_[index].identity, identity))
        {
            return index;
        }
    }
    return contacts_.size();
}

bool SdProtocolPeerRepository::stableContactIdentity(
    const MeshPeerRecord& peer,
    MeshPeerIdentity& out_identity) const
{
    const MeshProtocol protocol = normalizeProtocol(peer.identity.protocol);
    if ((protocol == MeshProtocol::Meshtastic &&
         peer.identity.kind == MeshPeerIdentityKind::NodeId) ||
        (protocol == MeshProtocol::MeshCore &&
         peer.identity.kind == MeshPeerIdentityKind::PublicKey) ||
        (protocol == MeshProtocol::Reticulum &&
         peer.identity.kind == MeshPeerIdentityKind::ReticulumDestination))
    {
        out_identity = peer.identity;
        out_identity.protocol = protocol;
        return true;
    }
    out_identity = MeshPeerIdentity{};
    return false;
}

NodeId SdProtocolPeerRepository::projectedNodeId(
    const MeshPeerRecord& peer) const
{
    const MeshProtocol protocol = normalizeProtocol(peer.identity.protocol);
    if (peer.identity.kind == MeshPeerIdentityKind::NodeId)
    {
        return peer.identity.node_id;
    }
    if (protocol == MeshProtocol::MeshCore)
    {
        return peer.meshcore.node_id_hint;
    }
    if (protocol == MeshProtocol::Reticulum &&
        peer.identity.kind == MeshPeerIdentityKind::ReticulumDestination)
    {
        return reticulumNodeId(peer.identity.reticulum);
    }
    return 0U;
}

bool SdProtocolPeerRepository::peerIsProtected(
    const MeshPeerRecord& peer) const
{
    MeshPeerIdentity stable{};
    if (stableContactIdentity(peer, stable) &&
        findContactIndex(stable) < contacts_.size())
    {
        return true;
    }
    return peerReferencedByConversation(peer);
}

bool SdProtocolPeerRepository::peerReferencedByConversation(
    const MeshPeerRecord& peer) const
{
    const MeshProtocol protocol = normalizeProtocol(peer.identity.protocol);
    const std::vector<ConversationMeta> conversations =
        chat_store_.loadConversationPageForProtocol(protocol, 0U, 0U, nullptr);
    return peerReferencedByConversations(peer, conversations);
}

bool SdProtocolPeerRepository::peerReferencedByConversations(
    const MeshPeerRecord& peer,
    const std::vector<ConversationMeta>& conversations) const
{
    const MeshProtocol protocol = normalizeProtocol(peer.identity.protocol);
    const NodeId node_id = projectedNodeId(peer);
    for (const ConversationMeta& conversation : conversations)
    {
        if (normalizeProtocol(conversation.id.protocol) != protocol)
        {
            continue;
        }
        if (protocol == MeshProtocol::Reticulum &&
            peer.identity.kind == MeshPeerIdentityKind::ReticulumDestination &&
            hasReticulumDestinationIdentity(
                conversation.id.reticulum_identity) &&
            sameReticulumDestinationHash(
                conversation.id.reticulum_identity,
                peer.identity.reticulum))
        {
            return true;
        }
        if (node_id != 0U && conversation.id.peer == node_id)
        {
            return true;
        }
    }
    return false;
}

std::size_t SdProtocolPeerRepository::ephemeralCount(
    MeshProtocol protocol) const
{
    protocol = normalizeProtocol(protocol);
    const std::vector<ConversationMeta> conversations =
        chat_store_.loadConversationPageForProtocol(protocol, 0U, 0U, nullptr);
    std::size_t count = 0U;
    for (const MeshPeerRecord& peer : peers_)
    {
        MeshPeerIdentity stable{};
        const bool is_contact =
            stableContactIdentity(peer, stable) &&
            findContactIndex(stable) < contacts_.size();
        if (meshPeerSameProtocol(peer.identity.protocol, protocol) &&
            !is_contact &&
            !peerReferencedByConversations(peer, conversations))
        {
            ++count;
        }
    }
    return count;
}

bool SdProtocolPeerRepository::evictOldestEphemeral(MeshProtocol protocol)
{
    protocol = normalizeProtocol(protocol);
    const std::vector<ConversationMeta> conversations =
        chat_store_.loadConversationPageForProtocol(protocol, 0U, 0U, nullptr);
    std::size_t candidate = peers_.size();
    uint32_t oldest_seen = UINT32_MAX;
    for (std::size_t index = 0U; index < peers_.size(); ++index)
    {
        const MeshPeerRecord& peer = peers_[index];
        MeshPeerIdentity stable{};
        const bool is_contact =
            stableContactIdentity(peer, stable) &&
            findContactIndex(stable) < contacts_.size();
        if (!meshPeerSameProtocol(peer.identity.protocol, protocol) ||
            is_contact ||
            peerReferencedByConversations(peer, conversations))
        {
            continue;
        }
        if (candidate >= peers_.size() || peer.last_seen_s < oldest_seen)
        {
            candidate = index;
            oldest_seen = peer.last_seen_s;
        }
    }
    if (candidate >= peers_.size())
    {
        return false;
    }
    storage_v2::PeerProjection tombstone{};
    tombstone.record = peers_[candidate];
    tombstone.deleted = true;
    (void)queuePeerDelta(tombstone);
    peers_.erase(peers_.begin() + static_cast<std::ptrdiff_t>(candidate));
    return true;
}

bool SdProtocolPeerRepository::persistContactFacts(
    const MeshPeerIdentity& identity,
    const MeshPeerUserFlags& flags,
    const char* alias,
    bool deleted)
{
    const std::size_t existing_contact_index = findContactIndex(identity);
    if (!deleted && existing_contact_index >= contacts_.size() &&
        contacts_.size() >= kProtectedContactCapacity)
    {
        Serial.printf("[PeerStoreV2] contact capacity reached count=%u\n",
                      static_cast<unsigned>(contacts_.size()));
        return false;
    }
    storage_v2::ContactProjection projection{};
    projection.identity = identity;
    projection.flags = flags;
    projection.deleted = deleted;
    copyMeshPeerText(projection.alias,
                     sizeof(projection.alias),
                     alias ? alias : "");
    const std::size_t peer_index = findPeerIndex(identity);
    if (peer_index < peers_.size())
    {
        projection.node_id_hint = projectedNodeId(peers_[peer_index]);
    }
    (void)queueContactDelta(projection);
    (void)applyContactProjection(projection);
    if (peer_index < peers_.size())
    {
        overlayContactFactsForPeer(peers_[peer_index]);
    }
    return true;
}

MeshProtocol SdProtocolPeerRepository::normalizeProtocol(
    MeshProtocol protocol)
{
    return protocol == MeshProtocol::RNode ? MeshProtocol::Reticulum
                                           : protocol;
}

const char* SdProtocolPeerRepository::protocolSlug(MeshProtocol protocol)
{
    switch (normalizeProtocol(protocol))
    {
    case MeshProtocol::Meshtastic:
        return "mt";
    case MeshProtocol::MeshCore:
        return "mc";
    case MeshProtocol::Reticulum:
        return "rt";
    default:
        return "unknown";
    }
}

std::size_t SdProtocolPeerRepository::protocolIndex(MeshProtocol protocol)
{
    switch (normalizeProtocol(protocol))
    {
    case MeshProtocol::MeshCore:
        return 1U;
    case MeshProtocol::Reticulum:
        return 2U;
    case MeshProtocol::Meshtastic:
    default:
        return 0U;
    }
}

NodeId SdProtocolPeerRepository::reticulumNodeId(
    const ReticulumPeerIdentity& identity)
{
    if (!hasReticulumDestinationIdentity(identity))
    {
        return 0U;
    }
    const uint8_t* hash = identity.destination_hash;
    return (static_cast<NodeId>(hash[12]) << 24U) |
           (static_cast<NodeId>(hash[13]) << 16U) |
           (static_cast<NodeId>(hash[14]) << 8U) |
           static_cast<NodeId>(hash[15]);
}

bool SdProtocolPeerRepository::ensureDirectory(const char* path)
{
    return path && path[0] != '\0' &&
           (storage_runtime::sd_exists(path) ||
            storage_runtime::sd_mkdir(path));
}

void SdProtocolPeerRepository::buildProtocolPath(MeshProtocol protocol,
                                                 const char* name,
                                                 char* out,
                                                 std::size_t out_len)
{
    if (!out || out_len == 0U)
    {
        return;
    }
    if (name && name[0] != '\0')
    {
        std::snprintf(out,
                      out_len,
                      "%s/%s/%s",
                      kRoot,
                      protocolSlug(protocol),
                      name);
    }
    else
    {
        std::snprintf(out,
                      out_len,
                      "%s/%s",
                      kRoot,
                      protocolSlug(protocol));
    }
}

} // namespace chat
