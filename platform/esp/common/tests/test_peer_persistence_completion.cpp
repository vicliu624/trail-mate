#include "platform/esp/common/storage/storage_contracts.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <iostream>

namespace storage_contracts = platform::esp::common::storage;
using namespace storage_contracts;
struct MeshPeerDirectoryStatus
{
    int code = 0;
    bool succeeded() const { return code == 0; }
};
StorageOperationResultKind operationFailureKind(int) { return StorageOperationResultKind::RetryLater; }

// Supply only physical flush/lock behavior; maintenance methods come directly
// from the production repository. In particular, no test code sets Complete.
class SdProtocolPeerRepository
{
  public:
    StorageOperationResult beginMaintenance(StorageOperation, StorageOperationGeneration);
    StorageOperationResult stepMaintenance(StorageOperation, StorageOperationGeneration, const StorageOperationBudget&);
    StorageOperationResult stepPersistence(const StorageOperationBudget&);
    bool dirty = false;
    bool fail_flush_once = false;
    bool leased = false;
    unsigned flushes = 0;
    bool persistencePending() const { return dirty; }

  private:
    enum class MaintenancePhase
    {
        Idle,
        HydrationPrepare,
        PersistenceFlush,
        CompactionPrepare,
        Complete,
        Failed
    };
    struct Maintenance
    {
        StorageOperation operation = StorageOperation::None;
        StorageOperationGeneration generation = 0;
        MaintenancePhase phase = MaintenancePhase::Idle;
    } maintenance_;
    bool begun_ = true, hydrated_ = true, maintenance_persistence_locked_ = false;
    std::atomic<bool> hydrating_{false};
    static constexpr unsigned kPersistenceLeaseWaitTicks = 0;
    bool acquirePersistenceLease(unsigned)
    {
        if (leased) return false;
        return leased = true;
    }
    void releasePersistenceLease() { leased = false; }
    void releaseMaintenanceLease()
    {
        maintenance_persistence_locked_ = false;
        releasePersistenceLease();
    }
    StorageOperationResult maintenanceFailure(StorageOperationResultKind kind) const
    {
        return StorageOperationResult::failure(kind, maintenance_.operation, maintenance_.generation);
    }
    StorageOperationResult stepHydration(const StorageOperationBudget&)
    {
        assert(false);
        return {};
    }
    StorageOperationResult stepCompaction(const StorageOperationBudget&)
    {
        assert(false);
        return {};
    }
    MeshPeerDirectoryStatus flushPendingDeltas(std::size_t)
    {
        assert(leased);
        if (fail_flush_once)
        {
            fail_flush_once = false;
            return {1};
        }
        ++flushes;
        dirty = false;
        return {};
    }
};

#include "peer_maintenance_under_test.inc"

int main()
{
    SdProtocolPeerRepository peers;
    for (unsigned generation = 1; generation <= 3; ++generation)
    {
        peers.dirty = true;
        assert(peers.beginMaintenance(StorageOperation::Persist, generation).inProgress());
        if (generation == 2)
        {
            peers.fail_flush_once = true;
            assert(peers.stepMaintenance(StorageOperation::Persist, generation, {}).retryable());
            assert(peers.leased);
            assert(peers.beginMaintenance(StorageOperation::Persist, generation).inProgress());
        }
        assert(peers.stepMaintenance(StorageOperation::Persist, generation, {}).completed());
        assert(!peers.leased && !peers.dirty);
    }
    assert(peers.flushes == 3);
    std::cout << "Peer persistence completion and next-generation reuse passed\n";
}
