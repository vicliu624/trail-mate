#include "platform/esp/arduino_common/mesh/esp_mesh_runtime_shell.h"

namespace platform::esp::arduino_common::mesh
{

EspMeshRuntimeShell::EspMeshRuntimeShell(::mesh::MeshSession& session)
    : session_(session)
{
}

bool EspMeshRuntimeShell::start(const ::mesh::MeshRuntimeConfig& config)
{
    return session_.start(config);
}

void EspMeshRuntimeShell::stop()
{
    session_.stop();
}

void EspMeshRuntimeShell::tick()
{
    session_.tick();
}

::mesh::SendResult EspMeshRuntimeShell::sendDirect(
    const ::mesh::DirectMessageCommand& command)
{
    return session_.sendDirect(command);
}

::mesh::MeshSessionState EspMeshRuntimeShell::state() const
{
    return session_.state();
}

} // namespace platform::esp::arduino_common::mesh
