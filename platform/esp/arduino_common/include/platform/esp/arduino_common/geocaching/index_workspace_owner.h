#pragma once

namespace platform::esp::arduino_common::geocaching
{
// The session mutex serializes access. This lease spans asynchronous slices;
// the maintenance scheduler must advance the current holder before waiters.
class IndexWorkspaceOwner
{
  public:
    using Prepare = bool (*)(void*, const void*);
    // Configure while idle. The owner prepares/rebinds shared buffers before
    // handing out a lease; it may trim them only after consumers return.
    bool setPrepare(Prepare prepare, void* context)
    {
        if (owner_) return false;
        prepare_ = prepare;
        context_ = context;
        return true;
    }
    bool acquire(const void* owner)
    {
        if (!owner || (owner_ && owner_ != owner)) return false;
        if (!owner_ && prepare_ && !prepare_(context_, owner)) return false;
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
    Prepare prepare_ = nullptr;
    void* context_ = nullptr;
};
} // namespace platform::esp::arduino_common::geocaching
