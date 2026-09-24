#pragma once

namespace platform::esp::arduino_common::geocaching
{
// The session mutex serializes access. This lease spans asynchronous slices;
// the maintenance scheduler must advance the current holder before waiters.
class IndexWorkspaceOwner
{
  public:
    bool acquire(const void* owner)
    {
        if (!owner || (owner_ && owner_ != owner)) return false;
        owner_ = owner;
        return true;
    }
    void release(const void* owner)
    {
        if (owner_ == owner) owner_ = nullptr;
    }
    bool heldBy(const void* owner) const { return owner && owner_ == owner; }
    const void* holder() const { return owner_; }

  private:
    const void* owner_ = nullptr;
};
} // namespace platform::esp::arduino_common::geocaching
