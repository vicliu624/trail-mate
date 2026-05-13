#pragma once

#include "ui_presentation/mesh/mesh_status_snapshot.h"

namespace ui::mesh
{

class IMeshStatusSource
{
  public:
    virtual ~IMeshStatusSource() = default;

    virtual bool buildMeshStatusSnapshot(MeshStatusSnapshot& out) const = 0;
};

} // namespace ui::mesh
