#include "ui/screens/team/team_page_deferred_dispatch.h"

#include <algorithm>

namespace team
{
namespace ui
{
namespace
{

TeamPageDeferredDispatchFailure makeFailure(
    TeamPageDeferredDispatchAction action,
    TeamPageDeferredDispatchFailureKind kind,
    bool needs_keys,
    team::TeamService::SendError error = team::TeamService::SendError::None)
{
    TeamPageDeferredDispatchFailure failure;
    failure.action = action;
    failure.kind = kind;
    failure.needs_keys = needs_keys;
    failure.error = error;
    return failure;
}

} // namespace

TeamPageDeferredDispatchQueue::TeamPageDeferredDispatchQueue(
    TeamPageDeferredDispatchConfig config)
    : config_(config)
{
}

void TeamPageDeferredDispatchQueue::clearAll()
{
    clearKeyDist();
    clearStatusBroadcasts();
}

void TeamPageDeferredDispatchQueue::clearKeyDist()
{
    keydist_pending_.clear();
}

void TeamPageDeferredDispatchQueue::clearStatusBroadcasts()
{
    status_pending_.clear();
    next_presence_s_ = 0;
}

void TeamPageDeferredDispatchQueue::enqueueKeyDist(uint32_t node_id,
                                                   uint32_t key_id,
                                                   uint32_t now_s)
{
    for (const auto& item : keydist_pending_)
    {
        if (item.node_id == node_id && item.key_id == key_id)
        {
            return;
        }
    }

    KeyDistPending item;
    item.node_id = node_id;
    item.key_id = key_id;
    item.next_retry_s = now_s + config_.keydist_retry_interval_s;
    keydist_pending_.push_back(item);
}

void TeamPageDeferredDispatchQueue::confirmKeyDist(uint32_t node_id,
                                                   uint32_t key_id)
{
    keydist_pending_.erase(
        std::remove_if(keydist_pending_.begin(),
                       keydist_pending_.end(),
                       [&](const KeyDistPending& item)
                       {
                           return item.node_id == node_id &&
                                  item.key_id == key_id;
                       }),
        keydist_pending_.end());
}

void TeamPageDeferredDispatchQueue::scheduleStatusBroadcast(uint8_t repeats,
                                                            uint32_t now_s,
                                                            uint32_t delay_s)
{
    if (repeats == 0)
    {
        return;
    }

    StatusBroadcastPending item;
    item.next_send_s = now_s + delay_s;
    item.remaining = repeats;
    status_pending_.push_back(item);
}

bool TeamPageDeferredDispatchQueue::statusDue(
    const TeamPageDeferredDispatchState& state,
    uint32_t now_s) const
{
    if (!state.in_team || !state.has_team_id)
    {
        return next_presence_s_ != 0 || !status_pending_.empty();
    }
    if (!state.self_is_leader && !status_pending_.empty())
    {
        return true;
    }
    for (const auto& item : status_pending_)
    {
        if (static_cast<int32_t>(now_s - item.next_send_s) >= 0)
        {
            return true;
        }
    }
    if (!state.has_team_psk || state.security_round == 0)
    {
        return next_presence_s_ != 0;
    }
    return next_presence_s_ == 0 ||
           static_cast<int32_t>(now_s - next_presence_s_) >= 0;
}

TeamPageDeferredDispatchEffects
TeamPageDeferredDispatchQueue::processKeyDistRetries(
    const TeamPageDeferredDispatchState& state,
    ITeamPageDeferredDispatchPort& port,
    uint32_t now_s)
{
    TeamPageDeferredDispatchEffects effects;
    if (keydist_pending_.empty() || !state.has_team_psk ||
        !state.has_team_id || !port.hasController())
    {
        return effects;
    }

    for (auto it = keydist_pending_.begin(); it != keydist_pending_.end();)
    {
        if (now_s < it->next_retry_s)
        {
            ++it;
            continue;
        }

        if (it->attempts >= config_.keydist_max_retries)
        {
            effects.failures.push_back(makeFailure(
                TeamPageDeferredDispatchAction::KeyDist,
                TeamPageDeferredDispatchFailureKind::SendFailed,
                false));
            it = keydist_pending_.erase(it);
            continue;
        }

        team::proto::TeamKeyDist key_dist;
        key_dist.team_id = state.team_id;
        key_dist.key_id = it->key_id;
        key_dist.channel_psk_len =
            static_cast<uint8_t>(state.team_psk.size());
        key_dist.channel_psk = state.team_psk;

        const bool ok =
            port.sendKeyDistPlain(key_dist,
                                  chat::ChannelId::PRIMARY,
                                  it->node_id);
        effects.sent_keydist = true;
        if (!ok)
        {
            effects.failures.push_back(makeFailure(
                TeamPageDeferredDispatchAction::KeyDist,
                TeamPageDeferredDispatchFailureKind::SendFailedDetail,
                false,
                port.lastSendError()));
        }

        it->attempts += 1;
        it->next_retry_s = now_s + config_.keydist_retry_interval_s;
        ++it;
    }

    return effects;
}

TeamPageDeferredDispatchEffects
TeamPageDeferredDispatchQueue::processStatusBroadcasts(
    const TeamPageDeferredDispatchState& state,
    const team::proto::TeamStatus& status,
    ITeamPageDeferredDispatchPort& port,
    uint32_t now_s)
{
    TeamPageDeferredDispatchEffects effects;
    if (!state.in_team || !state.has_team_id)
    {
        clearStatusBroadcasts();
        return effects;
    }
    if (!state.self_is_leader)
    {
        status_pending_.clear();
    }

    if (!port.hasController())
    {
        return effects;
    }

    for (auto it = status_pending_.begin(); it != status_pending_.end();)
    {
        if (static_cast<int32_t>(now_s - it->next_send_s) < 0)
        {
            ++it;
            continue;
        }

        if (!port.sendStatus(status, chat::ChannelId::PRIMARY, 0))
        {
            effects.failures.push_back(makeFailure(
                TeamPageDeferredDispatchAction::Status,
                TeamPageDeferredDispatchFailureKind::SendFailed,
                true));
        }
        effects.sent_status = true;
        next_presence_s_ = now_s + 30U;

        if (!port.sendStatusPlain(status, chat::ChannelId::PRIMARY, 0))
        {
            effects.failures.push_back(makeFailure(
                TeamPageDeferredDispatchAction::Status,
                TeamPageDeferredDispatchFailureKind::SendFailed,
                false));
        }
        effects.sent_status = true;

        if (it->remaining > 0)
        {
            it->remaining -= 1;
        }
        if (it->remaining == 0)
        {
            it = status_pending_.erase(it);
        }
        else
        {
            it->next_send_s = now_s + config_.status_rebroadcast_interval_s;
            ++it;
        }
    }

    // This schedule survives the finite enrollment retry queue. Only leaders
    // advertise a full roster; members send an authenticated, roster-free
    // presence. It needs one deadline and never allocates a periodic queue item.
    if (!state.has_team_psk || state.security_round == 0)
    {
        next_presence_s_ = 0;
        return effects;
    }
    if (next_presence_s_ == 0)
    {
        next_presence_s_ = now_s + 5U;
        return effects;
    }
    if (static_cast<int32_t>(now_s - next_presence_s_) < 0)
    {
        return effects;
    }
    next_presence_s_ = now_s + 5U;
    if (status.key_id != state.security_round ||
        (!state.self_is_leader && status.has_members))
    {
        return effects;
    }
    const bool sent = port.sendStatus(status, chat::ChannelId::PRIMARY, 0);
    effects.sent_status = sent;
    if (sent)
    {
        next_presence_s_ = now_s + 30U;
    }
    // Background presence retries silently when the carrier is unavailable.
    // It is not an explicit user send and must not open a failure notice on
    // every retry or mutate membership/keys.

    return effects;
}

size_t TeamPageDeferredDispatchQueue::keyDistPendingCount() const
{
    return keydist_pending_.size();
}

size_t TeamPageDeferredDispatchQueue::statusBroadcastPendingCount() const
{
    return status_pending_.size();
}

} // namespace ui
} // namespace team
