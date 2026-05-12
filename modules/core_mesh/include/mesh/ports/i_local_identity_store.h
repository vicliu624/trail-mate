#pragma once

#include "mesh/domain/local_identity.h"
#include "mesh/domain/mesh_result.h"

namespace mesh
{

class ILocalIdentityStore
{
  public:
    virtual ~ILocalIdentityStore() = default;

    virtual StoreResult load(LocalIdentity& out) = 0;
    virtual StoreResult save(const LocalIdentity& identity) = 0;
    virtual StoreResult clear() = 0;
};

} // namespace mesh
