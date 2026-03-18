#pragma once

#include "chat/runtime/self_identity_policy.h"

namespace chat::runtime
{

class SelfAnnouncementCore
{
  public:
    virtual ~SelfAnnouncementCore() = default;
};

} // namespace chat::runtime
