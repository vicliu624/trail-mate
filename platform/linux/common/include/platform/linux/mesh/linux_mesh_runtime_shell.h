#pragma once

#include "mesh/usecase/mesh_session.h"

namespace platform::linux_runtime::mesh
{

class LinuxMeshRuntimeShell final
{
  public:
    explicit LinuxMeshRuntimeShell(::mesh::MeshSession& session);

    bool start(const ::mesh::MeshRuntimeConfig& config);
    void stop();
    void tick();
    ::mesh::SendResult sendDirect(const ::mesh::DirectMessageCommand& command);
    ::mesh::MeshSessionState state() const;

  private:
    ::mesh::MeshSession& session_;
};

} // namespace platform::linux_runtime::mesh
