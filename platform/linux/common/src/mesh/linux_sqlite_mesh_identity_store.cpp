#include "platform/linux/mesh/linux_sqlite_mesh_identity_store.h"

#include "platform/ui/settings_store.h"

#include <algorithm>
#include <cstring>

namespace platform::linux_runtime::mesh
{
namespace
{

constexpr std::size_t kPublicKeySize = 32;
constexpr std::size_t kPeerKeyEntryV2Size = 40;
constexpr std::size_t kPeerKeyEntryV1Size = 36;

bool isZero(const std::uint8_t* data, std::size_t size)
{
    if (!data)
    {
        return true;
    }
    for (std::size_t index = 0; index < size; ++index)
    {
        if (data[index] != 0)
        {
            return false;
        }
    }
    return true;
}

std::uint32_t readU32(const std::uint8_t* data)
{
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

void writeU32(std::uint8_t* data, std::uint32_t value)
{
    data[0] = static_cast<std::uint8_t>(value & 0xFFU);
    data[1] = static_cast<std::uint8_t>((value >> 8) & 0xFFU);
    data[2] = static_cast<std::uint8_t>((value >> 16) & 0xFFU);
    data[3] = static_cast<std::uint8_t>((value >> 24) & 0xFFU);
}

bool validIdentity(const ::mesh::LocalIdentity& identity)
{
    return identity.valid &&
           !isZero(identity.private_key, sizeof(identity.private_key));
}

} // namespace

LinuxSqliteLocalIdentityStore::LinuxSqliteLocalIdentityStore()
    : options_(Options{})
{
}

LinuxSqliteLocalIdentityStore::LinuxSqliteLocalIdentityStore(const Options& options)
    : options_(options)
{
}

::mesh::StoreResult LinuxSqliteLocalIdentityStore::load(::mesh::LocalIdentity& out)
{
    out = ::mesh::LocalIdentity{};

    std::vector<std::uint8_t> public_blob;
    std::vector<std::uint8_t> private_blob;
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

::mesh::StoreResult LinuxSqliteLocalIdentityStore::save(
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

::mesh::StoreResult LinuxSqliteLocalIdentityStore::clear()
{
    const char* keys[] = {options_.public_key, options_.private_key};
    ::platform::ui::settings_store::remove_keys(options_.ns, keys, 2);
    return ::mesh::StoreResult::success();
}

LinuxSqlitePeerKeyStore::LinuxSqlitePeerKeyStore()
    : options_(Options{})
{
}

LinuxSqlitePeerKeyStore::LinuxSqlitePeerKeyStore(const Options& options)
    : options_(options)
{
}

::mesh::StoreResult LinuxSqlitePeerKeyStore::get(::mesh::NodeId node_id,
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

::mesh::StoreResult LinuxSqlitePeerKeyStore::put(const ::mesh::PeerPublicKey& key)
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

::mesh::StoreResult LinuxSqlitePeerKeyStore::remove(::mesh::NodeId node_id)
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

::mesh::StoreResult LinuxSqlitePeerKeyStore::clear()
{
    const char* keys[] = {options_.key};
    ::platform::ui::settings_store::remove_keys(options_.ns, keys, 1);
    return ::mesh::StoreResult::success();
}

::mesh::StoreResult LinuxSqlitePeerKeyStore::loadAll(
    std::vector<::mesh::PeerPublicKey>& out)
{
    out.clear();

    std::vector<std::uint8_t> blob;
    if (!::platform::ui::settings_store::get_blob(options_.ns, options_.key, blob))
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::NotFound);
    }
    if (blob.empty())
    {
        return ::mesh::StoreResult::success();
    }

    const bool is_v2 = (blob.size() % kPeerKeyEntryV2Size) == 0;
    const bool is_v1 = !is_v2 && (blob.size() % kPeerKeyEntryV1Size) == 0;
    if (!is_v2 && !is_v1)
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::Corrupt);
    }

    const std::size_t entry_size = is_v2 ? kPeerKeyEntryV2Size : kPeerKeyEntryV1Size;
    std::size_t count = blob.size() / entry_size;
    if (count > options_.max_keys)
    {
        count = options_.max_keys;
    }
    out.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        const std::uint8_t* entry = blob.data() + (index * entry_size);
        const std::uint32_t node_id = readU32(entry);
        const std::uint8_t* public_key = is_v2 ? entry + 8 : entry + 4;
        if (node_id == 0 || isZero(public_key, kPublicKeySize))
        {
            continue;
        }

        ::mesh::PeerPublicKey key{};
        key.node_id = ::mesh::NodeId{node_id};
        key.updated_at_ms = is_v2 ? readU32(entry + 4) : 0;
        key.verified = false;
        std::memcpy(key.public_key, public_key, sizeof(key.public_key));
        out.push_back(key);
    }
    return ::mesh::StoreResult::success();
}

::mesh::StoreResult LinuxSqlitePeerKeyStore::replaceAll(
    const ::mesh::PeerPublicKey* keys,
    std::size_t count)
{
    if (count > 0 && !keys)
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::InvalidArgument);
    }

    std::vector<::mesh::PeerPublicKey> copy;
    copy.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        if (!keys[index].node_id.isValidUnicast())
        {
            return ::mesh::StoreResult::fail(::mesh::StoreFailure::InvalidArgument);
        }
        copy.push_back(keys[index]);
    }
    return saveAll(copy);
}

::mesh::StoreResult LinuxSqlitePeerKeyStore::saveAll(
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
                   keys.begin() + static_cast<std::ptrdiff_t>(keys.size() - options_.max_keys));
    }

    std::vector<std::uint8_t> blob(keys.size() * kPeerKeyEntryV2Size);
    for (std::size_t index = 0; index < keys.size(); ++index)
    {
        if (!keys[index].node_id.isValidUnicast())
        {
            return ::mesh::StoreResult::fail(::mesh::StoreFailure::InvalidArgument);
        }
        std::uint8_t* out = blob.data() + (index * kPeerKeyEntryV2Size);
        writeU32(out, keys[index].node_id.value);
        writeU32(out + 4, keys[index].updated_at_ms);
        std::memcpy(out + 8, keys[index].public_key, kPublicKeySize);
    }

    const bool persisted = ::platform::ui::settings_store::put_blob(
        options_.ns,
        options_.key,
        blob.data(),
        blob.size());
    if (!persisted)
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::IoError);
    }
    return ::mesh::StoreResult::success();
}

} // namespace platform::linux_runtime::mesh
