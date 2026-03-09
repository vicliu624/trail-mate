#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace chat
{

class IContactBlobStore
{
  public:
    virtual ~IContactBlobStore() = default;

    virtual bool loadBlob(std::vector<uint8_t>& out) = 0;
    virtual bool saveBlob(const uint8_t* data, size_t len) = 0;
};

} // namespace chat
