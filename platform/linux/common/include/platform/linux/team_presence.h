#pragma once

#include "platform/ui/team_ui_snapshot_store.h"
#include "team/usecase/team_service.h"

namespace platform::linux_runtime
{
// Rejoin the existing Team status exchange after startup or carrier recovery.
// Members announce presence without claiming an authoritative member list.
class LinuxTeamPresence
{
  public:
    LinuxTeamPresence(team::TeamService& service, team::ITeamRuntime& runtime);
    void update(const team::ui::TeamUiSnapshot& snapshot, uint32_t self_id, bool transport_ready);

  private:
    team::TeamService& service_;
    team::ITeamRuntime& runtime_;
    team::TeamId team_id_{};
    uint32_t key_id_ = 0;
    uint32_t next_send_ms_ = 0;
    bool scheduled_ = false;
    bool leader_ = false;
};
} // namespace platform::linux_runtime
