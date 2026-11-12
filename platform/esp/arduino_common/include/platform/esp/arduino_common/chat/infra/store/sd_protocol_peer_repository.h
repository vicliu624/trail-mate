#pragma once

#include "chat/ports/i_chat_store.h"
#include "chat/ports/i_protocol_peer_repository.h"
#include "platform/esp/arduino_common/chat/infra/store/fixed_slot_journal.h"
#include "platform/esp/arduino_common/chat/infra/store/protocol_peer_codec.h"
#include "platform/esp/arduino_common/memory/psram_allocator.h"
#include "platform/esp/common/storage/storage_contracts.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace chat
{

class SdProtocolPeerRepository final : public IProtocolPeerRepository
{
  public:
    explicit SdProtocolPeerRepository(IChatStore& chat_store);
    ~SdProtocolPeerRepository() override;

    SdProtocolPeerRepository(const SdProtocolPeerRepository&) = delete;
    SdProtocolPeerRepository& operator=(const SdProtocolPeerRepository&) = delete;

    MeshPeerDirectoryStatus begin() override;
    bool isHydrating() const
    {
        return hydrating_.load(std::memory_order_acquire);
    }
    bool persistencePending() const
    {
        return persistence_pending_.load(std::memory_order_acquire);
    }
    bool compactionPending() const
    {
        return compaction_pending_.load(std::memory_order_acquire);
    }

    // Same operation/generation resumes the current maintenance cursor after
    // a retryable physical SD/device transaction miss; logical maintenance
    // ownership remains with this operation until completion or cancellation.
    // A new generation starts fresh.
    platform::esp::common::storage::StorageOperationResult beginMaintenance(
        platform::esp::common::storage::StorageOperation operation,
        platform::esp::common::storage::StorageOperationGeneration generation);
    platform::esp::common::storage::StorageOperationResult stepMaintenance(
        platform::esp::common::storage::StorageOperation operation,
        platform::esp::common::storage::StorageOperationGeneration generation,
        const platform::esp::common::storage::StorageOperationBudget& budget);
    void cancelMaintenance(
        platform::esp::common::storage::StorageOperation operation,
        platform::esp::common::storage::StorageOperationGeneration generation);
    MeshPeerDirectoryStatus record(const MeshPeerRecord& record) override;
    MeshPeerDirectoryStatus find(const MeshPeerIdentity& identity,
                                 MeshPeerRecord& out_record) override;
    MeshPeerDirectoryStatus findByNodeId(MeshProtocol protocol,
                                         NodeId node_id,
                                         MeshPeerRecord& out_record) override;
    MeshPeerDirectoryStatus loadRecent(MeshProtocol protocol,
                                       MeshPeerRecord* out_records,
                                       std::size_t max_records,
                                       std::size_t* out_count) override;
    MeshPeerDirectoryStatus search(MeshProtocol protocol,
                                   const char* query,
                                   MeshPeerRecord* out_records,
                                   std::size_t max_records,
                                   std::size_t* out_count) override;
    MeshPeerDirectoryStatus visit(
        MeshProtocol protocol,
        MeshPeerDirectoryView view,
        IMeshPeerDirectoryVisitor& visitor) override;
    MeshPeerDirectoryStatus setUserAlias(
        const MeshPeerIdentity& identity,
        const char* alias) override;
    MeshPeerDirectoryStatus setUserFlags(
        const MeshPeerIdentity& identity,
        const MeshPeerUserFlags& flags) override;
    MeshPeerDirectoryStatus setKeyManuallyVerified(
        const MeshPeerIdentity& identity,
        bool verified) override;
    MeshPeerDirectoryStatus remove(const MeshPeerIdentity& identity) override;
    MeshPeerDirectoryStatus clearProtocol(MeshProtocol protocol) override;
    MeshPeerDirectoryCapacity capacityFor(MeshProtocol protocol) const override;
    MeshPeerDirectoryStatus flush() override;

  private:
    using PeerVector = std::vector<
        MeshPeerRecord,
        ::platform::esp::arduino_common::memory::PsramAllocator<MeshPeerRecord>>;
    using ContactVector = std::vector<
        storage::v2::ContactProjection,
        ::platform::esp::arduino_common::memory::PsramAllocator<
            storage::v2::ContactProjection>>;
    using PendingPeerVector = std::vector<
        storage::v2::PeerProjection,
        ::platform::esp::arduino_common::memory::PsramAllocator<
            storage::v2::PeerProjection>>;
    using PendingContactVector = std::vector<
        storage::v2::ContactProjection,
        ::platform::esp::arduino_common::memory::PsramAllocator<
            storage::v2::ContactProjection>>;
    using PsramByteVector = std::vector<
        uint8_t,
        ::platform::esp::arduino_common::memory::PsramAllocator<uint8_t>>;

    struct PartitionState
    {
        uint32_t peer_delta_count = 0;
        uint32_t contact_delta_count = 0;
    };

    bool ensureLayout();
    bool ensureProtocolLayout(MeshProtocol protocol);

    bool appendPeerDelta(const storage::v2::PeerProjection& projection);
    bool appendContactDelta(
        const storage::v2::ContactProjection& projection);
    bool queuePeerDelta(
        const storage::v2::PeerProjection& projection);
    bool queueContactDelta(
        const storage::v2::ContactProjection& projection);
    MeshPeerDirectoryStatus flushProtocolReset();
    MeshPeerDirectoryStatus flushPendingDeltas(std::size_t budget);
    void refreshPersistenceDemandLocked();
    bool rewritePeerSnapshotFrom(MeshProtocol protocol,
                                 const PeerVector& snapshot);
    bool acquirePersistenceLease(TickType_t wait_ticks);
    void releasePersistenceLease();
    void releaseMaintenanceLease();
    void prunePendingDeltasLocked();
    bool queueDeferredObservation(const MeshPeerRecord& record);
    void drainDeferredObservationsLocked();
    MeshPeerDirectoryStatus recordLocked(const MeshPeerRecord& record);
    bool applyPeerProjection(const storage::v2::PeerProjection& projection);
    bool applyContactProjection(
        const storage::v2::ContactProjection& projection);
    void overlayContactFacts();
    void overlayContactFactsForPeer(MeshPeerRecord& peer) const;
    void reconcileStableIdentities(MeshProtocol protocol);

    std::size_t findPeerIndex(const MeshPeerIdentity& identity) const;
    std::size_t findPeerIndexByNodeId(MeshProtocol protocol,
                                      NodeId node_id) const;
    std::size_t findContactIndex(const MeshPeerIdentity& identity) const;
    bool stableContactIdentity(const MeshPeerRecord& peer,
                               MeshPeerIdentity& out_identity) const;
    NodeId projectedNodeId(const MeshPeerRecord& peer) const;
    bool peerIsProtected(const MeshPeerRecord& peer) const;
    bool peerReferencedByConversation(const MeshPeerRecord& peer) const;
    bool peerReferencedByConversations(
        const MeshPeerRecord& peer,
        const std::vector<ConversationMeta>& conversations) const;
    std::size_t ephemeralCount(MeshProtocol protocol) const;
    bool evictOldestEphemeral(MeshProtocol protocol);

    bool persistContactFacts(const MeshPeerIdentity& identity,
                             const MeshPeerUserFlags& flags,
                             const char* alias,
                             bool deleted);

    static MeshProtocol normalizeProtocol(MeshProtocol protocol);
    static const char* protocolSlug(MeshProtocol protocol);
    static std::size_t protocolIndex(MeshProtocol protocol);
    static NodeId reticulumNodeId(const ReticulumPeerIdentity& identity);
    static bool ensureDirectory(const char* path);
    static void buildProtocolPath(MeshProtocol protocol,
                                  const char* name,
                                  char* out,
                                  std::size_t out_len);

    enum class MaintenancePhase : uint8_t
    {
        Idle,
        HydrationPrepare,
        HydrationJournal,
        HydrationFinalize,
        PersistenceFlush,
        CompactionPrepare,
        CompactionInspect,
        CompactionCreate,
        CompactionWrite,
        CompactionReplace,
        CompactionAdvance,
        Complete,
        Failed,
        ContactRepairPrepare,
        ContactRepairCopy,
        ContactRepairLatest,
        ContactRepairPublish,
        ContactRepairFinish,
    };

    struct MaintenanceState
    {
        MaintenancePhase phase = MaintenancePhase::Idle;
        platform::esp::common::storage::StorageOperation operation =
            platform::esp::common::storage::StorageOperation::None;
        platform::esp::common::storage::StorageOperationGeneration generation =
            0U;
        uint8_t protocol_index = 0U;
        uint8_t journal_index = 0U;
        bool journal_started = false;
        uint8_t compaction_projection_index = 0U;
        uint8_t compaction_inspection_index = 0U;
        uint32_t compaction_record_index = 0U;
        bool compact_peers = false;
        bool compact_contacts = false;
        uint32_t repair_slot_index = 0U;
        uint32_t repair_slot_count = 0U;
    };

    bool prepareMaintenanceJournal();
    bool applyHydrationJournalSlot(MeshProtocol protocol,
                                   storage::v2::JournalKind kind);
    platform::esp::common::storage::StorageOperationResult stepHydration(
        const platform::esp::common::storage::StorageOperationBudget& budget);
    platform::esp::common::storage::StorageOperationResult stepContactRepair();
    platform::esp::common::storage::StorageOperationResult stepPersistence(
        const platform::esp::common::storage::StorageOperationBudget& budget);
    platform::esp::common::storage::StorageOperationResult stepCompaction(
        const platform::esp::common::storage::StorageOperationBudget& budget);
    platform::esp::common::storage::StorageOperationResult maintenanceFailure(
        platform::esp::common::storage::StorageOperationResultKind kind) const;

    IChatStore& chat_store_;
    storage::v2::FixedSlotJournalEngine journal_{};
    PeerVector peers_{};
    ContactVector contacts_{};
    PendingPeerVector pending_peer_deltas_{};
    std::size_t pending_peer_head_ = 0U;
    PendingContactVector pending_contact_deltas_{};
    std::size_t pending_contact_head_ = 0U;
    uint32_t pending_peer_revision_ = 0U;
    uint32_t pending_contact_revision_ = 0U;
    bool protocol_reset_pending_[3]{};
    uint32_t protocol_reset_revision_[3]{};
    PeerVector pending_peer_observations_{};
    SemaphoreHandle_t pending_observation_mutex_ = nullptr;
    uint32_t dropped_peer_observations_ = 0U;
    PsramByteVector slot_scratch_{};
    PartitionState partitions_[3]{};
    MeshProtocol active_protocol_ = MeshProtocol::Meshtastic;
    bool begun_ = false;
    std::atomic<bool> hydrating_{false};
    bool hydrated_ = false;
    mutable SemaphoreHandle_t mutex_ = nullptr;
    SemaphoreHandle_t persistence_mutex_ = nullptr;
    bool maintenance_persistence_locked_ = false;
    std::atomic<bool> persistence_pending_{false};
    std::atomic<bool> compaction_pending_{false};
    storage::v2::FixedSlotJournalCursor maintenance_journal_{};
    PsramByteVector maintenance_scratch_{};
    PeerVector compaction_peers_{};
    ContactVector compaction_contacts_{};
    PendingPeerVector flush_peer_batch_{};
    PendingContactVector flush_contact_batch_{};
    PeerVector flush_peer_snapshot_{};
    uint32_t compaction_peer_revision_ = 0U;
    uint32_t compaction_contact_revision_ = 0U;
    uint32_t compaction_reset_revision_[3]{};
    bool compaction_force_peers_[3]{};
    bool compaction_force_contacts_[3]{};
    MaintenanceState maintenance_{};
    char maintenance_path_[96] = {};
    char maintenance_final_path_[96] = {};
    char maintenance_backup_path_[96] = {};
    char maintenance_delta_path_[96] = {};
    MeshProtocol maintenance_protocol_ = MeshProtocol::Meshtastic;
    storage::v2::JournalKind maintenance_kind_ =
        storage::v2::JournalKind::PeerSnapshot;
    std::size_t maintenance_slot_size_ = 0U;
};

} // namespace chat
