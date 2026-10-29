#pragma once
#include <filesystem>

namespace platform::linux_runtime
{
// Cooperating identity owners share one protected OFD lease file. A replacement
// upgrades its own descriptor only after other owners have stopped.
bool set_identity_owner_lock(int descriptor, bool exclusive);
bool open_shared_identity_owner(const std::filesystem::path&, int& descriptor);
std::filesystem::path radio_identity_owner_path(const std::filesystem::path& database);
class IdentityOwnerLease
{
  public:
    IdentityOwnerLease() = default;
    ~IdentityOwnerLease();
    IdentityOwnerLease(const IdentityOwnerLease&) = delete;
    IdentityOwnerLease& operator=(const IdentityOwnerLease&) = delete;
    bool acquireShared(const std::filesystem::path&);
    bool acquireExclusive();
    bool downgrade();
    void reset();

  private:
    int descriptor_ = -1;
    std::filesystem::path path_;
};
} // namespace platform::linux_runtime
