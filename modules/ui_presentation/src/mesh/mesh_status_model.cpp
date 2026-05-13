#include "ui_presentation/mesh/mesh_status_model.h"

namespace ui::mesh
{

MeshStatusModel::MeshStatusModel(IMeshStatusSource& source)
    : source_(source)
{
}

MeshStatusSnapshot MeshStatusModel::snapshot() const
{
    MeshStatusSnapshot out{};
    if (!source_.buildMeshStatusSnapshot(out))
    {
        out.header.valid = false;
    }
    return out;
}

} // namespace ui::mesh
