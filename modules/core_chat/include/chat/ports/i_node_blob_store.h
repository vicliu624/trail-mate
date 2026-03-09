#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace chat
{
namespace contacts
{

class INodeBlobStore
{
  public:
    virtual ~INodeBlobStore() = default;

    virtual bool loadBlob(std::vector<uint8_t>& out) = 0;
    virtual bool saveBlob(const uint8_t* data, size_t len) = 0;
    virtual void clearBlob() = 0;
};

} // namespace contacts
} // namespace chat
