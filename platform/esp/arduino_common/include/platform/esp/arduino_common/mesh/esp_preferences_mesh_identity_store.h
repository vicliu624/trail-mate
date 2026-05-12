#pragma once

#include "mesh/ports/i_local_identity_store.h"
#include "mesh/ports/i_peer_key_store.h"

#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace platform::esp::arduino_common::mesh
{

class EspPreferencesLocalIdentityStore final : public ::mesh::ILocalIdentityStore
{
  public:
    struct Options
    {
        const char* ns = "chat";
        const char* public_key = "pki_pub";
        const char* private_key = "pki_priv";
    };

    EspPreferencesLocalIdentityStore();
    explicit EspPreferencesLocalIdentityStore(const Options& options);

    ::mesh::StoreResult load(::mesh::LocalIdentity& out) override;
    ::mesh::StoreResult save(const ::mesh::LocalIdentity& identity) override;
    ::mesh::StoreResult clear() override;

  private:
    Options options_;
};

class EspPreferencesPeerKeyStore final : public ::mesh::IPeerKeyStore
{
  public:
    struct Options
    {
        const char* ns = "chat_pki";
        const char* key = "pki_nodes";
        const char* version_key = "pki_nodes_ver";
        uint8_t version = 2;
        size_t max_keys = 16;
    };

    EspPreferencesPeerKeyStore();
    explicit EspPreferencesPeerKeyStore(const Options& options);

    ::mesh::StoreResult get(::mesh::NodeId node_id, ::mesh::PeerPublicKey& out) override;
    ::mesh::StoreResult put(const ::mesh::PeerPublicKey& key) override;
    ::mesh::StoreResult remove(::mesh::NodeId node_id) override;
    ::mesh::StoreResult clear() override;

    ::mesh::StoreResult loadAll(std::vector<::mesh::PeerPublicKey>& out);
    ::mesh::StoreResult replaceAll(const ::mesh::PeerPublicKey* keys, size_t count);

  private:
    ::mesh::StoreResult saveAll(std::vector<::mesh::PeerPublicKey>& keys);

    Options options_;
};

} // namespace platform::esp::arduino_common::mesh
