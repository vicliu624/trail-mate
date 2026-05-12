#pragma once

#include "mesh/ports/i_local_identity_store.h"
#include "mesh/ports/i_peer_key_store.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace platform::linux_runtime::mesh
{

class LinuxSqliteLocalIdentityStore final : public ::mesh::ILocalIdentityStore
{
  public:
    struct Options
    {
        const char* ns = "chat";
        const char* public_key = "pki_pub";
        const char* private_key = "pki_priv";
    };

    LinuxSqliteLocalIdentityStore();
    explicit LinuxSqliteLocalIdentityStore(const Options& options);

    ::mesh::StoreResult load(::mesh::LocalIdentity& out) override;
    ::mesh::StoreResult save(const ::mesh::LocalIdentity& identity) override;
    ::mesh::StoreResult clear() override;

  private:
    Options options_;
};

class LinuxSqlitePeerKeyStore final : public ::mesh::IPeerKeyStore
{
  public:
    struct Options
    {
        const char* ns = "chat_pki";
        const char* key = "pki_nodes";
        std::size_t max_keys = 16;
    };

    LinuxSqlitePeerKeyStore();
    explicit LinuxSqlitePeerKeyStore(const Options& options);

    ::mesh::StoreResult get(::mesh::NodeId node_id, ::mesh::PeerPublicKey& out) override;
    ::mesh::StoreResult put(const ::mesh::PeerPublicKey& key) override;
    ::mesh::StoreResult remove(::mesh::NodeId node_id) override;
    ::mesh::StoreResult clear() override;

    ::mesh::StoreResult loadAll(std::vector<::mesh::PeerPublicKey>& out);
    ::mesh::StoreResult replaceAll(const ::mesh::PeerPublicKey* keys, std::size_t count);

  private:
    ::mesh::StoreResult saveAll(std::vector<::mesh::PeerPublicKey>& keys);

    Options options_;
};

} // namespace platform::linux_runtime::mesh
