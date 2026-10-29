#pragma once
#include "chat/infra/mesh_peer_directory_core.h"
#include <memory>

namespace platform::linux_runtime
{
// Native serialization boundary around the shared directory owner. Runtime
// records acknowledge memory admission; administrative mutations and flush()
// wait for persistence. Blob I/O never runs under the query/mutation mutex.
class LinuxPeerDirectory final : public ::chat::IMeshPeerDirectory
{
  public:
    explicit LinuxPeerDirectory(::chat::IMeshPeerDirectoryBlobStore&);
    ~LinuxPeerDirectory() override;
    void shutdown();
    ::chat::MeshPeerDirectoryStatus begin() override;
    ::chat::MeshPeerDirectoryStatus record(const ::chat::MeshPeerRecord&) override;
    ::chat::MeshPeerDirectoryStatus find(const ::chat::MeshPeerIdentity&, ::chat::MeshPeerRecord&) override;
    ::chat::MeshPeerDirectoryStatus findByNodeId(::chat::MeshProtocol, ::chat::NodeId, ::chat::MeshPeerRecord&) override;
    ::chat::MeshPeerDirectoryStatus loadRecent(::chat::MeshProtocol, ::chat::MeshPeerRecord*, size_t, size_t*) override;
    ::chat::MeshPeerDirectoryStatus search(::chat::MeshProtocol, const char*, ::chat::MeshPeerRecord*, size_t, size_t*) override;
    ::chat::MeshPeerDirectoryStatus visit(::chat::MeshProtocol, ::chat::MeshPeerDirectoryView, ::chat::IMeshPeerDirectoryVisitor&) override;
    ::chat::MeshPeerDirectoryStatus setUserAlias(const ::chat::MeshPeerIdentity&, const char*) override;
    ::chat::MeshPeerDirectoryStatus setUserFlags(const ::chat::MeshPeerIdentity&, const ::chat::MeshPeerUserFlags&) override;
    ::chat::MeshPeerDirectoryStatus setKeyManuallyVerified(const ::chat::MeshPeerIdentity&, bool) override;
    ::chat::MeshPeerDirectoryStatus remove(const ::chat::MeshPeerIdentity&) override;
    ::chat::MeshPeerDirectoryStatus clearProtocol(::chat::MeshProtocol) override;
    ::chat::MeshPeerDirectoryCapacity capacityFor(::chat::MeshProtocol) const override;
    ::chat::MeshPeerDirectoryStatus flush() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
