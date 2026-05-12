#pragma once

#include "mesh/usecase/mesh_session.h"

namespace platform::nrf52::arduino_common::mesh
{

class Nrf52MeshRuntimeShell final
{
  public:
    explicit Nrf52MeshRuntimeShell(::mesh::MeshSession& session);

    bool start(const ::mesh::MeshRuntimeConfig& config);
    void stop();
    void tick();
    ::mesh::SendResult sendDirect(const ::mesh::DirectMessageCommand& command);
    ::mesh::MeshSessionState state() const;

  private:
    ::mesh::MeshSession& session_;
};

} // namespace platform::nrf52::arduino_common::mesh
