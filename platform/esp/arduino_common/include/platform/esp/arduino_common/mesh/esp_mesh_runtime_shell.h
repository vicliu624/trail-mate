#pragma once

#include "mesh/usecase/mesh_session.h"

namespace platform::esp::arduino_common::mesh
{

class EspMeshRuntimeShell final
{
  public:
    explicit EspMeshRuntimeShell(::mesh::MeshSession& session);

    bool start(const ::mesh::MeshRuntimeConfig& config);
    void stop();
    void tick();
    ::mesh::SendResult sendDirect(const ::mesh::DirectMessageCommand& command);
    ::mesh::MeshSessionState state() const;

  private:
    ::mesh::MeshSession& session_;
};

} // namespace platform::esp::arduino_common::mesh
