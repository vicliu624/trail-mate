#pragma once

#include "chat/ports/i_mesh_peer_directory.h"
#include "chat/ports/i_mesh_peer_directory_blob_store.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace chat
{

class MeshPeerDirectoryCore final : public IMeshPeerDirectory
{
  public:
    static constexpr uint32_t kSaveIntervalMs = 5000;

    struct Options
    {
        MeshPeerDirectoryCapacity meshtastic_capacity{1024, 16};
        MeshPeerDirectoryCapacity meshcore_capacity{1024, 128};
        MeshPeerDirectoryCapacity reticulum_capacity{1024, 64};
        bool auto_save = true;
    };

    explicit MeshPeerDirectoryCore(IMeshPeerDirectoryBlobStore& blob_store);
    MeshPeerDirectoryCore(IMeshPeerDirectoryBlobStore& blob_store,
                          const Options& options);

    void setAutoSaveEnabled(bool enabled);

    MeshPeerDirectoryStatus begin() override;
    MeshPeerDirectoryStatus beginEmpty();
    MeshPeerDirectoryBlobLoadResult loadPersistenceBlob(
        std::vector<uint8_t>& out) const;
    MeshPeerDirectoryBlobLoadResult streamPersistenceBlob(
        IMeshPeerDirectoryBlobSink& sink) const;
    MeshPeerDirectoryStatus hydratePersistenceBlob(const uint8_t* data,
                                                   std::size_t len);
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

    bool persistencePending() const;
    uint32_t persistenceRevision() const;
    std::size_t persistenceSnapshotSize() const;
    bool encodePersistenceSnapshot(uint8_t* out,
                                   std::size_t out_len,
                                   uint32_t* out_revision) const;
    bool persistEncodedSnapshot(const uint8_t* data,
                                std::size_t len,
                                uint32_t revision);
    // Storage owner calls this only after the matching encoded snapshot was
    // accepted by its persistence port. A newer in-memory revision stays dirty.
    bool acknowledgePersistedRevision(uint32_t revision);

    std::size_t count(MeshProtocol protocol) const;
    void clear();

    static uint32_t computeBlobCrc(const uint8_t* data, std::size_t len);
    static bool decodeBlob(std::vector<MeshPeerRecord>& out,
                           const uint8_t* data,
                           std::size_t len);
    static void encodeBlob(std::vector<uint8_t>& out,
                           const std::vector<MeshPeerRecord>& records);

  private:
    std::size_t findIndex(const MeshPeerIdentity& identity) const;
    std::size_t countForProtocol(MeshProtocol protocol) const;
    void evictOldest(MeshProtocol protocol);
    void markDirty();
    MeshPeerDirectoryStatus saveRecords();
    void maybeSave();

    IMeshPeerDirectoryBlobStore& blob_store_;
    Options options_{};
    std::vector<MeshPeerRecord> records_;
    bool begun_ = false;
    bool dirty_ = false;
    uint32_t persistence_revision_ = 0;
};

} // namespace chat
