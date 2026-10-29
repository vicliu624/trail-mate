#pragma once
#include "chat/ports/device_identity_restore.h"
#include "platform/linux/mesh/linux_sqlite_mesh_identity_store.h"
#include <array>
#include <filesystem>
#include <utility>

namespace platform::linux_runtime
{
struct RadioIdentitySnapshot
{
    bool present = false;
    uint32_t generation = 0;
    std::array<uint8_t, 32> public_key{};
    std::array<uint8_t, 64> private_key{};
    size_t private_size = 0;
    void clear();
    ~RadioIdentitySnapshot() { clear(); }
};
// Legacy storage is read without generating keys or rewriting its encoding.
// Generation zero represents a legacy/unmanaged key (or an empty slot).
class LinuxRadioIdentityStore
{
  public:
    explicit LinuxRadioIdentityStore(std::filesystem::path database,
                                     mesh::LinuxSqliteLocalIdentityStore::Options primary = {}) : database_(std::move(database)), primary_(primary) {}
    static bool validatePair(uint8_t kind, const uint8_t public_key[32], const uint8_t* private_key, size_t size, bool canonical = false);
    ::mesh::StoreResult read(uint8_t kind, RadioIdentitySnapshot&) const;
    // Caller quiesces live caches. Author and radio expectations are checked in
    // the same transaction; even restoring the same key advances radio epoch.
    chat::DeviceIdentityRestoreResult replace(uint8_t kind, uint32_t author_generation, const uint8_t author_public[64],
                                              uint32_t radio_generation, const uint8_t* expected_radio_public, const uint8_t public_key[32], const uint8_t* private_key, size_t private_size) const;

  private:
    std::filesystem::path database_;
    mesh::LinuxSqliteLocalIdentityStore::Options primary_;
};
} // namespace platform::linux_runtime
