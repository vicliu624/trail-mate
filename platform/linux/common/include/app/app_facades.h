#pragma once

#include <cstdint>

namespace app
{

class IAppMessagingFacade
{
  public:
    virtual ~IAppMessagingFacade() = default;
    virtual std::uint32_t getSelfNodeId() const = 0;
};

} // namespace app
