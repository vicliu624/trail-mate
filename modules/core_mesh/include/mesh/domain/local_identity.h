#pragma once

#include <stdint.h>

namespace mesh
{

struct LocalIdentity
{
    uint8_t private_key[32]{};
    uint8_t public_key[32]{};
    bool valid = false;
};

} // namespace mesh
