#include "platform/linux/mesh/linux_mesh_runtime_shell.h"

namespace platform::linux_runtime::mesh
{

LinuxMeshRuntimeShell::LinuxMeshRuntimeShell(::mesh::MeshSession& session)
    : session_(session)
{
}

bool LinuxMeshRuntimeShell::start(const ::mesh::MeshRuntimeConfig& config)
{
    return session_.start(config);
}

void LinuxMeshRuntimeShell::stop()
{
    session_.stop();
}

void LinuxMeshRuntimeShell::tick()
{
    session_.tick();
}

::mesh::SendResult LinuxMeshRuntimeShell::sendDirect(
    const ::mesh::DirectMessageCommand& command)
{
    return session_.sendDirect(command);
}

::mesh::MeshSessionState LinuxMeshRuntimeShell::state() const
{
    return session_.state();
}

} // namespace platform::linux_runtime::mesh
