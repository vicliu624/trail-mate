#include "platform/linux/team_presence.h"

#include <algorithm>
#include <cstdio>

namespace platform::linux_runtime
{
LinuxTeamPresence::LinuxTeamPresence(team::TeamService& service, team::ITeamRuntime& runtime)
    : service_(service), runtime_(runtime)
{
}

void LinuxTeamPresence::update(const team::ui::TeamUiSnapshot& snapshot, uint32_t self_id, bool transport_ready)
{
    if (!snapshot.in_team || !snapshot.has_team_id || !snapshot.has_team_psk ||
        !snapshot.security_round || !service_.hasKeys() || !self_id || !transport_ready)
    {
        scheduled_ = false;
        return;
    }

    const auto now = runtime_.nowMillis();
    if (!scheduled_ || team_id_ != snapshot.team_id || key_id_ != snapshot.security_round ||
        leader_ != snapshot.self_is_leader)
    {
        team_id_ = snapshot.team_id;
        key_id_ = snapshot.security_round;
        leader_ = snapshot.self_is_leader;
        // Let the carrier's own startup announcement settle, and separate peers
        // restored at nearly the same time without spending additional entropy.
        next_send_ms_ = now + 2000 + self_id % 2000;
        scheduled_ = true;
    }
    if (static_cast<int32_t>(now - next_send_ms_) < 0) return;

    team::proto::TeamStatus status;
    status.key_id = snapshot.security_round;
    if (snapshot.self_is_leader)
    {
        status.has_members = true;
        status.leader_id = self_id;
        for (const auto& member : snapshot.members)
        {
            const auto id = member.node_id ? member.node_id : self_id;
            if (std::find(status.members.begin(), status.members.end(), id) == status.members.end())
                status.members.push_back(id);
        }
        if (std::find(status.members.begin(), status.members.end(), self_id) == status.members.end())
            status.members.push_back(self_id);
    }
    // An authenticated status with has_members=false is understood by existing
    // ESP receivers: touch the sender while retaining the leader's full roster.
    const bool sent = service_.sendStatus(status, chat::ChannelId::PRIMARY);
    next_send_ms_ = now + (sent ? 30000 : 5000) + self_id % 3000;
    std::printf("[Team] presence role=%s sent=%u members=%zu\n",
                snapshot.self_is_leader ? "leader" : "member", sent ? 1U : 0U, status.members.size());
}
} // namespace platform::linux_runtime
