#include "platform/nrf52/arduino_common/mesh/nrf52_settings_mesh_identity_store.h"

#include "platform/ui/settings_store.h"

#include <algorithm>
#include <cstring>

namespace platform::nrf52::arduino_common::mesh
{
namespace
{

struct PeerKeyEntry
{
    uint32_t node_id;
    uint32_t updated_at_ms;
    uint8_t key[32];
} __attribute__((packed));

bool isZero(const uint8_t* data, size_t size)
{
    if (!data)
    {
        return true;
    }
    for (size_t index = 0; index < size; ++index)
    {
        if (data[index] != 0)
        {
            return false;
        }
    }
    return true;
}

bool validIdentity(const ::mesh::LocalIdentity& identity)
{
    return identity.valid &&
           !isZero(identity.private_key, sizeof(identity.private_key));
}

} // namespace

Nrf52SettingsLocalIdentityStore::Nrf52SettingsLocalIdentityStore()
    : options_(Options{})
{
}

Nrf52SettingsLocalIdentityStore::Nrf52SettingsLocalIdentityStore(const Options& options)
    : options_(options)
{
}

::mesh::StoreResult Nrf52SettingsLocalIdentityStore::load(::mesh::LocalIdentity& out)
{
    out = ::mesh::LocalIdentity{};

    std::vector<uint8_t> public_blob;
    std::vector<uint8_t> private_blob;
    const bool have_public =
        ::platform::ui::settings_store::get_blob(options_.ns, options_.public_key, public_blob);
    const bool have_private =
        ::platform::ui::settings_store::get_blob(options_.ns, options_.private_key, private_blob);
    if (!have_public && !have_private)
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::NotFound);
    }
    if (!have_public || !have_private ||
        public_blob.size() != sizeof(out.public_key) ||
        private_blob.size() != sizeof(out.private_key))
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::Corrupt);
    }

    std::memcpy(out.public_key, public_blob.data(), sizeof(out.public_key));
    std::memcpy(out.private_key, private_blob.data(), sizeof(out.private_key));
    out.valid = !isZero(out.private_key, sizeof(out.private_key));
    if (!out.valid)
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::Corrupt);
    }
    return ::mesh::StoreResult::success();
}

::mesh::StoreResult Nrf52SettingsLocalIdentityStore::save(
    const ::mesh::LocalIdentity& identity)
{
    if (!validIdentity(identity))
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::InvalidArgument);
    }

    const bool public_ok = ::platform::ui::settings_store::put_blob(
        options_.ns,
        options_.public_key,
        identity.public_key,
        sizeof(identity.public_key));
    const bool private_ok = ::platform::ui::settings_store::put_blob(
        options_.ns,
        options_.private_key,
        identity.private_key,
        sizeof(identity.private_key));
    if (!public_ok || !private_ok)
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::IoError);
    }
    return ::mesh::StoreResult::success();
}

::mesh::StoreResult Nrf52SettingsLocalIdentityStore::clear()
{
    const char* keys[] = {options_.public_key, options_.private_key};
    ::platform::ui::settings_store::remove_keys(options_.ns, keys, 2);
    return ::mesh::StoreResult::success();
}

Nrf52SettingsPeerKeyStore::Nrf52SettingsPeerKeyStore()
    : options_(Options{})
{
}

Nrf52SettingsPeerKeyStore::Nrf52SettingsPeerKeyStore(const Options& options)
    : options_(options)
{
}

::mesh::StoreResult Nrf52SettingsPeerKeyStore::get(::mesh::NodeId node_id,
                                                   ::mesh::PeerPublicKey& out)
{
    if (!node_id.isValidUnicast())
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::InvalidArgument);
    }

    std::vector<::mesh::PeerPublicKey> keys;
    auto loaded = loadAll(keys);
    if (!loaded.ok)
    {
        return loaded;
    }

    for (const auto& key : keys)
    {
        if (key.node_id == node_id)
        {
            out = key;
            return ::mesh::StoreResult::success();
        }
    }
    return ::mesh::StoreResult::fail(::mesh::StoreFailure::NotFound);
}

::mesh::StoreResult Nrf52SettingsPeerKeyStore::put(const ::mesh::PeerPublicKey& key)
{
    if (!key.node_id.isValidUnicast())
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::InvalidArgument);
    }

    std::vector<::mesh::PeerPublicKey> keys;
    auto loaded = loadAll(keys);
    if (!loaded.ok && loaded.failure != ::mesh::StoreFailure::NotFound)
    {
        return loaded;
    }

    bool replaced = false;
    for (auto& existing : keys)
    {
        if (existing.node_id == key.node_id)
        {
            existing = key;
            replaced = true;
            break;
        }
    }
    if (!replaced)
    {
        keys.push_back(key);
    }
    return saveAll(keys);
}

::mesh::StoreResult Nrf52SettingsPeerKeyStore::remove(::mesh::NodeId node_id)
{
    if (!node_id.isValidUnicast())
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::InvalidArgument);
    }

    std::vector<::mesh::PeerPublicKey> keys;
    auto loaded = loadAll(keys);
    if (!loaded.ok)
    {
        return loaded.failure == ::mesh::StoreFailure::NotFound
                   ? ::mesh::StoreResult::success()
                   : loaded;
    }

    keys.erase(std::remove_if(keys.begin(),
                              keys.end(),
                              [node_id](const ::mesh::PeerPublicKey& key)
                              {
                                  return key.node_id == node_id;
                              }),
               keys.end());
    return saveAll(keys);
}

::mesh::StoreResult Nrf52SettingsPeerKeyStore::clear()
{
    const char* keys[] = {options_.key};
    ::platform::ui::settings_store::remove_keys(options_.ns, keys, 1);
    return ::mesh::StoreResult::success();
}

::mesh::StoreResult Nrf52SettingsPeerKeyStore::loadAll(
    std::vector<::mesh::PeerPublicKey>& out)
{
    out.clear();

    std::vector<uint8_t> blob;
    if (!::platform::ui::settings_store::get_blob(options_.ns, options_.key, blob))
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::NotFound);
    }
    if (blob.empty())
    {
        return ::mesh::StoreResult::success();
    }
    if ((blob.size() % sizeof(PeerKeyEntry)) != 0)
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::Corrupt);
    }

    size_t count = blob.size() / sizeof(PeerKeyEntry);
    if (count > options_.max_keys)
    {
        count = options_.max_keys;
    }
    const auto* entries = reinterpret_cast<const PeerKeyEntry*>(blob.data());
    out.reserve(count);
    for (size_t index = 0; index < count; ++index)
    {
        if (entries[index].node_id == 0)
        {
            continue;
        }
        ::mesh::PeerPublicKey key{};
        key.node_id = ::mesh::NodeId{entries[index].node_id};
        key.updated_at_ms = entries[index].updated_at_ms;
        key.verified = false;
        std::memcpy(key.public_key, entries[index].key, sizeof(key.public_key));
        out.push_back(key);
    }
    return ::mesh::StoreResult::success();
}

::mesh::StoreResult Nrf52SettingsPeerKeyStore::replaceAll(
    const ::mesh::PeerPublicKey* keys,
    size_t count)
{
    if (count > 0 && !keys)
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::InvalidArgument);
    }

    std::vector<::mesh::PeerPublicKey> copy;
    copy.reserve(count);
    for (size_t index = 0; index < count; ++index)
    {
        if (!keys[index].node_id.isValidUnicast())
        {
            return ::mesh::StoreResult::fail(::mesh::StoreFailure::InvalidArgument);
        }
        copy.push_back(keys[index]);
    }
    return saveAll(copy);
}

::mesh::StoreResult Nrf52SettingsPeerKeyStore::saveAll(
    std::vector<::mesh::PeerPublicKey>& keys)
{
    if (keys.empty())
    {
        return clear();
    }

    if (keys.size() > options_.max_keys)
    {
        std::sort(keys.begin(),
                  keys.end(),
                  [](const ::mesh::PeerPublicKey& left,
                     const ::mesh::PeerPublicKey& right)
                  {
                      return left.updated_at_ms < right.updated_at_ms;
                  });
        keys.erase(keys.begin(),
                   keys.begin() + static_cast<long>(keys.size() - options_.max_keys));
    }

    std::vector<PeerKeyEntry> entries;
    entries.reserve(keys.size());
    for (const auto& key : keys)
    {
        if (!key.node_id.isValidUnicast())
        {
            return ::mesh::StoreResult::fail(::mesh::StoreFailure::InvalidArgument);
        }
        PeerKeyEntry entry{};
        entry.node_id = key.node_id.value;
        entry.updated_at_ms = key.updated_at_ms;
        std::memcpy(entry.key, key.public_key, sizeof(entry.key));
        entries.push_back(entry);
    }

    const bool persisted = ::platform::ui::settings_store::put_blob(
        options_.ns,
        options_.key,
        entries.data(),
        entries.size() * sizeof(PeerKeyEntry));
    if (!persisted)
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::IoError);
    }
    return ::mesh::StoreResult::success();
}

} // namespace platform::nrf52::arduino_common::mesh
