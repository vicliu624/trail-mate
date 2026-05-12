#include "platform/nrf52/arduino_common/mesh/nrf52_mesh_runtime_shell.h"

namespace platform::nrf52::arduino_common::mesh
{

Nrf52MeshRuntimeShell::Nrf52MeshRuntimeShell(::mesh::MeshSession& session)
    : session_(session)
{
}

bool Nrf52MeshRuntimeShell::start(const ::mesh::MeshRuntimeConfig& config)
{
    return session_.start(config);
}

void Nrf52MeshRuntimeShell::stop()
{
    session_.stop();
}

void Nrf52MeshRuntimeShell::tick()
{
    session_.tick();
}

::mesh::SendResult Nrf52MeshRuntimeShell::sendDirect(
    const ::mesh::DirectMessageCommand& command)
{
    return session_.sendDirect(command);
}

::mesh::MeshSessionState Nrf52MeshRuntimeShell::state() const
{
    return session_.state();
}

} // namespace platform::nrf52::arduino_common::mesh
