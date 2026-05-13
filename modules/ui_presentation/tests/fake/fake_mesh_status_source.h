#pragma once

#include "ui_presentation/mesh/mesh_status_source.h"

namespace ui::tests
{

class FakeMeshStatusSource final : public ui::mesh::IMeshStatusSource
{
  public:
    bool buildMeshStatusSnapshot(ui::mesh::MeshStatusSnapshot& out) const override
    {
        if (!available)
        {
            return false;
        }
        out = snapshot_value;
        return true;
    }

    bool available = true;
    ui::mesh::MeshStatusSnapshot snapshot_value{};
};

} // namespace ui::tests
