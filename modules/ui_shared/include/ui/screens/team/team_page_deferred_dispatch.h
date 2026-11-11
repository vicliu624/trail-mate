#pragma once

#include "chat/domain/chat_types.h"
#include "team/domain/team_types.h"
#include "team/protocol/team_mgmt.h"
#include "team/usecase/team_service.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace team
{
namespace ui
{

class TeamPageRuntimePort;

class ITeamPageDeferredDispatchPort
{
  public:
    virtual ~ITeamPageDeferredDispatchPort() = default;

    virtual bool hasController() const = 0;
    virtual bool sendKeyDistPlain(const team::proto::TeamKeyDist& key_dist,
                                  chat::ChannelId channel,
                                  chat::NodeId dest) = 0;
    virtual bool sendStatus(const team::proto::TeamStatus& status,
                            chat::ChannelId channel,
                            chat::NodeId dest) = 0;
    virtual bool sendStatusPlain(const team::proto::TeamStatus& status,
                                 chat::ChannelId channel,
                                 chat::NodeId dest) = 0;
    virtual team::TeamService::SendError lastSendError() const = 0;
};

struct TeamPageDeferredDispatchConfig
{
    uint8_t keydist_max_retries = 3;
    uint32_t keydist_retry_interval_s = 5;
    uint32_t status_rebroadcast_interval_s = 2;
};

struct TeamPageDeferredDispatchState
{
    bool in_team = false;
    bool has_team_id = false;
    bool self_is_leader = false;
    TeamId team_id{};
    uint32_t security_round = 0;
    std::array<uint8_t, team::proto::kTeamChannelPskSize> team_psk{};
    bool has_team_psk = false;
};

enum class TeamPageDeferredDispatchAction : uint8_t
{
    KeyDist,
    Status
};

enum class TeamPageDeferredDispatchFailureKind : uint8_t
{
    SendFailed,
    SendFailedDetail
};

struct TeamPageDeferredDispatchFailure
{
    TeamPageDeferredDispatchAction action =
        TeamPageDeferredDispatchAction::Status;
    TeamPageDeferredDispatchFailureKind kind =
        TeamPageDeferredDispatchFailureKind::SendFailed;
    bool needs_keys = false;
    team::TeamService::SendError error = team::TeamService::SendError::None;
};

struct TeamPageDeferredDispatchEffects
{
    std::vector<TeamPageDeferredDispatchFailure> failures;
    bool sent_keydist = false;
    bool sent_status = false;
};

class TeamPageDeferredDispatchQueue
{
  public:
    explicit TeamPageDeferredDispatchQueue(
        TeamPageDeferredDispatchConfig config = {});

    void clearAll();
    void clearKeyDist();
    void clearStatusBroadcasts();

    void enqueueKeyDist(uint32_t node_id,
                        uint32_t key_id,
                        uint32_t now_s);
    void confirmKeyDist(uint32_t node_id, uint32_t key_id);
    void scheduleStatusBroadcast(uint8_t repeats,
                                 uint32_t now_s,
                                 uint32_t delay_s);
    bool statusDue(const TeamPageDeferredDispatchState& state,
                   uint32_t now_s) const;

    TeamPageDeferredDispatchEffects processKeyDistRetries(
        const TeamPageDeferredDispatchState& state,
        ITeamPageDeferredDispatchPort& port,
        uint32_t now_s);
    TeamPageDeferredDispatchEffects processStatusBroadcasts(
        const TeamPageDeferredDispatchState& state,
        const team::proto::TeamStatus& status,
        ITeamPageDeferredDispatchPort& port,
        uint32_t now_s);

    size_t keyDistPendingCount() const;
    size_t statusBroadcastPendingCount() const;

  private:
    struct KeyDistPending
    {
        uint32_t node_id = 0;
        uint32_t key_id = 0;
        uint8_t attempts = 0;
        uint32_t next_retry_s = 0;
    };

    struct StatusBroadcastPending
    {
        uint32_t next_send_s = 0;
        uint8_t remaining = 0;
    };

    TeamPageDeferredDispatchConfig config_;
    std::vector<KeyDistPending> keydist_pending_;
    std::vector<StatusBroadcastPending> status_pending_;
    uint32_t next_presence_s_ = 0;
};

class TeamPageDeferredDispatchRuntimeAdapter final
    : public ITeamPageDeferredDispatchPort
{
  public:
    explicit TeamPageDeferredDispatchRuntimeAdapter(
        const TeamPageRuntimePort& runtime);

    bool hasController() const override;
    bool sendKeyDistPlain(const team::proto::TeamKeyDist& key_dist,
                          chat::ChannelId channel,
                          chat::NodeId dest) override;
    bool sendStatus(const team::proto::TeamStatus& status,
                    chat::ChannelId channel,
                    chat::NodeId dest) override;
    bool sendStatusPlain(const team::proto::TeamStatus& status,
                         chat::ChannelId channel,
                         chat::NodeId dest) override;
    team::TeamService::SendError lastSendError() const override;

  private:
    const TeamPageRuntimePort& runtime_;
};

} // namespace ui
} // namespace team
