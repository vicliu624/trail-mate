#pragma once

#include "ui_presentation/mesh/mesh_status_source.h"

namespace ui::mesh
{

class MeshStatusModel
{
  public:
    explicit MeshStatusModel(IMeshStatusSource& source);

    MeshStatusSnapshot snapshot() const;

  private:
    IMeshStatusSource& source_;
};

} // namespace ui::mesh
