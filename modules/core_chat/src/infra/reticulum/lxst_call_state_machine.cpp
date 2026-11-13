/*
 * LXST-derived portions: https://github.com/markqvist/LXST
 * Copyright 2024-2026, Mark Qvist, unless otherwise noted.
 * Upstream reference terms: LICENSES/LXST-CC-BY-NC-ND-4.0.txt.
 * Sharing adapted LXST material requires separate authorization from Mark Qvist.
 * This notice does not grant that authorization.
 * Any RNS/LXMF-derived portions retain their original copyright and Reticulum
 * License; see LICENSES/Reticulum-Derivatives.txt, LICENSES/Reticulum-RNS.txt
 * and LICENSES/Reticulum-LXMF.txt. Other contributions retain their own terms.
 * Component scope: docs/licensing/reticulum-sources.csv; see LICENSE.
 */

/**
 * @file lxst_call_state_machine.cpp
 * @brief Pure Sideband/LXST telephony session state and transition policy.
 */

#include "chat/infra/reticulum/lxst_call_state_machine.h"

#include "chat/infra/reticulum/lxst_telephony_wire.h"

namespace chat::reticulum::lxst::call
{
namespace
{

constexpr uint32_t kIdentifyTimeoutMs = 15000;
constexpr uint32_t kIdentifyAvailableRetryMs = 1200;
constexpr uint32_t kRingTimeoutMs = 60000;
constexpr uint32_t kDialTimeoutMs = 70000;
constexpr uint32_t kMediaSetupTimeoutMs = 5000;

void appendAction(Transition& transition, Action action)
{
    if (transition.action_count < Transition::kMaxActions)
    {
        transition.actions[transition.action_count++] = action;
    }
}

void enterPhase(State& state, Phase phase, uint32_t now_ms)
{
    state.phase = phase;
    state.phase_started_ms = now_ms;
}

bool callerIsWaiting(const State& state)
{
    return state.phase == Phase::CallerAwaitingAvailability ||
           state.phase == Phase::CallerAwaitingRinging ||
           state.phase == Phase::CallerNegotiating ||
           state.phase == Phase::CallerConnecting;
}

bool calleeCanNegotiateProfile(const State& state)
{
    return state.phase == Phase::CalleeRinging ||
           state.phase == Phase::CalleeConnecting ||
           state.phase == Phase::Active;
}

Transition closeTransition(State& state, uint32_t now_ms)
{
    Transition transition{};
    transition.accepted = true;
    enterPhase(state, Phase::Closing, now_ms);
    appendAction(transition, Action::CloseLink);
    return transition;
}

Transition handlePreferredProfile(State& state,
                                  uint16_t requested_profile)
{
    Transition transition{};
    const bool phase_allows_profile =
        (state.role == Role::Caller && callerIsWaiting(state)) ||
        (state.role == Role::Callee && calleeCanNegotiateProfile(state));
    if (!phase_allows_profile)
    {
        return transition;
    }

    transition.accepted = true;
    if (requested_profile != state.profile)
    {
        appendAction(transition, Action::SendPreferredProfile);
    }
    return transition;
}

Transition handleCallerSignal(State& state,
                              uint16_t signal,
                              uint32_t now_ms)
{
    Transition transition{};
    if (signal == kStatusBusy || signal == kStatusRejected)
    {
        if (!callerIsWaiting(state))
        {
            return transition;
        }
        state.remote_status = signal;
        return closeTransition(state, now_ms);
    }

    if (signal == kStatusAvailable)
    {
        if (state.phase == Phase::CallerAwaitingRinging)
        {
            transition.accepted = true;
            return transition;
        }
        if (state.phase != Phase::CallerAwaitingAvailability)
        {
            return transition;
        }
        state.remote_status = signal;
        enterPhase(state, Phase::CallerAwaitingRinging, now_ms);
        transition.accepted = true;
        appendAction(transition, Action::SendIdentify);
        return transition;
    }

    if (signal == kStatusRinging)
    {
        if (state.phase == Phase::CallerNegotiating)
        {
            transition.accepted = true;
            return transition;
        }
        if (state.phase != Phase::CallerAwaitingRinging)
        {
            return transition;
        }
        state.remote_status = signal;
        enterPhase(state, Phase::CallerNegotiating, now_ms);
        transition.accepted = true;
        appendAction(transition, Action::SendPreferences);
        return transition;
    }

    if (signal == kStatusConnecting)
    {
        if (state.phase == Phase::CallerConnecting)
        {
            transition.accepted = true;
            return transition;
        }
        if (state.phase != Phase::CallerNegotiating)
        {
            return transition;
        }
        state.remote_status = signal;
        enterPhase(state, Phase::CallerConnecting, now_ms);
        transition.accepted = true;
        appendAction(transition, Action::PrepareMedia);
        return transition;
    }

    if (signal == kStatusEstablished)
    {
        if (state.phase == Phase::Active)
        {
            transition.accepted = true;
            return transition;
        }
        if (state.phase != Phase::CallerNegotiating &&
            state.phase != Phase::CallerConnecting)
        {
            return transition;
        }
        state.remote_status = signal;
        transition.accepted = true;
        if (state.media_prepared)
        {
            enterPhase(state, Phase::Active, now_ms);
            appendAction(transition, Action::ActivateMedia);
        }
        else
        {
            enterPhase(state, Phase::CallerConnecting, now_ms);
            appendAction(transition, Action::PrepareMedia);
        }
        return transition;
    }
    return transition;
}

Transition handleCalleeSignal(State& state,
                              uint16_t signal,
                              uint32_t now_ms)
{
    Transition transition{};

    // LXST ignores status signals from the caller until the local user answers.
    if (state.phase == Phase::CalleeAwaitingLink ||
        state.phase == Phase::CalleeAwaitingIdentity ||
        state.phase == Phase::CalleeAwaitingAdmission ||
        state.phase == Phase::CalleeRinging)
    {
        return transition;
    }

    if ((signal == kStatusBusy || signal == kStatusRejected) &&
        (state.phase == Phase::CalleeConnecting ||
         state.phase == Phase::Active))
    {
        state.remote_status = signal;
        return closeTransition(state, now_ms);
    }
    return transition;
}

} // namespace

State makeCaller(uint16_t profile, uint32_t now_ms)
{
    State state{};
    state.role = Role::Caller;
    state.phase = Phase::CallerAwaitingLink;
    state.profile = profile;
    state.mode = kDefaultMode;
    state.local_status = kStatusCalling;
    state.remote_status = kStatusCalling;
    state.phase_started_ms = now_ms;
    return state;
}

State makeCallee(uint16_t profile, uint32_t now_ms)
{
    State state{};
    state.role = Role::Callee;
    state.phase = Phase::CalleeAwaitingLink;
    state.profile = profile;
    state.mode = kDefaultMode;
    state.local_status = kStatusCalling;
    state.remote_status = kStatusCalling;
    state.phase_started_ms = now_ms;
    return state;
}

Transition dispatch(State* state, const Event& event, uint32_t now_ms)
{
    Transition transition{};
    if (!state)
    {
        return transition;
    }

    if (event.type == EventType::LinkClosed)
    {
        transition.accepted = true;
        enterPhase(*state, Phase::Closed, now_ms);
        return transition;
    }
    if (state->phase == Phase::Closing || state->phase == Phase::Closed)
    {
        return transition;
    }
    if (event.type == EventType::ControlRetry)
    {
        if (state->role == Role::Callee &&
            state->phase == Phase::CalleeAwaitingIdentity &&
            now_ms - state->last_available_ms >= kIdentifyAvailableRetryMs)
        {
            state->last_available_ms = now_ms;
            transition.accepted = true;
            appendAction(transition, Action::SendAvailable);
        }
        return transition;
    }
    if (event.type == EventType::LocalModeSwitch)
    {
        if (state->phase != Phase::Active ||
            (event.signal != kModeFullDuplex &&
             event.signal != kModeHalfDuplex))
        {
            return transition;
        }
        transition.accepted = true;
        if (state->mode != event.signal)
        {
            state->mode = event.signal;
            appendAction(transition, Action::SendPreferredMode);
        }
        return transition;
    }
    if (event.type == EventType::LocalHangup ||
        event.type == EventType::OperationFailed ||
        event.type == EventType::Timeout)
    {
        transition.accepted = true;
        const bool reject_ringing_call =
            event.type == EventType::LocalHangup &&
            state->role == Role::Callee &&
            state->phase == Phase::CalleeRinging;
        enterPhase(*state, Phase::Closing, now_ms);
        if (reject_ringing_call)
        {
            state->local_status = kStatusRejected;
            appendAction(transition, Action::SendRejected);
        }
        appendAction(transition, Action::CloseLink);
        return transition;
    }

    if (event.type == EventType::RemoteSignal)
    {
        if (event.signal >= kPreferredProfile)
        {
            return handlePreferredProfile(*state,
                                          event.signal - kPreferredProfile);
        }
        if (event.signal >= kPreferredMode)
        {
            const uint16_t mode = event.signal - kPreferredMode;
            if (mode != kModeFullDuplex && mode != kModeHalfDuplex)
            {
                return transition;
            }
            state->mode = mode;
            transition.accepted = true;
            return transition;
        }
        return state->role == Role::Caller
                   ? handleCallerSignal(*state, event.signal, now_ms)
                   : handleCalleeSignal(*state, event.signal, now_ms);
    }

    if (event.type == EventType::LinkActive)
    {
        transition.accepted = true;
        if (state->role == Role::Caller &&
            state->phase == Phase::CallerAwaitingLink)
        {
            enterPhase(*state, Phase::CallerAwaitingAvailability, now_ms);
            return transition;
        }
        if (state->role == Role::Callee &&
            state->phase == Phase::CalleeAwaitingLink)
        {
            state->local_status = kStatusAvailable;
            state->last_available_ms = now_ms;
            enterPhase(*state, Phase::CalleeAwaitingIdentity, now_ms);
            appendAction(transition, Action::SendAvailable);
            return transition;
        }
        transition.accepted = false;
        return transition;
    }

    if (event.type == EventType::RemoteIdentified &&
        state->role == Role::Callee &&
        state->phase == Phase::CalleeAwaitingIdentity)
    {
        transition.accepted = true;
        enterPhase(*state, Phase::CalleeAwaitingAdmission, now_ms);
        appendAction(transition, Action::BeginRinging);
        return transition;
    }

    if (event.type == EventType::AdmissionGranted &&
        state->role == Role::Callee &&
        state->phase == Phase::CalleeAwaitingAdmission)
    {
        transition.accepted = true;
        state->local_status = kStatusRinging;
        enterPhase(*state, Phase::CalleeRinging, now_ms);
        appendAction(transition, Action::SendRinging);
        return transition;
    }

    if (event.type == EventType::AdmissionDenied &&
        state->role == Role::Callee &&
        state->phase == Phase::CalleeAwaitingAdmission)
    {
        transition.accepted = true;
        state->local_status = kStatusBusy;
        enterPhase(*state, Phase::Closing, now_ms);
        appendAction(transition, Action::SendBusy);
        appendAction(transition, Action::CloseLink);
        return transition;
    }

    if (event.type == EventType::LocalAccepted &&
        state->role == Role::Callee &&
        state->phase == Phase::CalleeRinging)
    {
        transition.accepted = true;
        state->local_status = kStatusConnecting;
        enterPhase(*state, Phase::CalleeConnecting, now_ms);
        appendAction(transition, Action::SendConnecting);
        appendAction(transition, Action::PrepareMedia);
        return transition;
    }

    if (event.type == EventType::MediaReady)
    {
        if (state->phase == Phase::CallerConnecting)
        {
            transition.accepted = true;
            state->media_prepared = true;
            if (state->remote_status == kStatusEstablished)
            {
                enterPhase(*state, Phase::Active, now_ms);
                appendAction(transition, Action::ActivateMedia);
            }
            return transition;
        }
        if (state->phase == Phase::CalleeConnecting)
        {
            transition.accepted = true;
            state->media_prepared = true;
            state->local_status = kStatusEstablished;
            enterPhase(*state, Phase::Active, now_ms);
            appendAction(transition, Action::SendEstablished);
            appendAction(transition, Action::ActivateMedia);
            return transition;
        }
    }
    return transition;
}

uint32_t phaseTimeoutMs(const State& state)
{
    switch (state.phase)
    {
    case Phase::CallerAwaitingAvailability:
    case Phase::CallerAwaitingRinging:
    case Phase::CalleeAwaitingIdentity:
        return kIdentifyTimeoutMs;
    case Phase::CallerNegotiating:
        return kDialTimeoutMs;
    case Phase::CalleeRinging:
        return kRingTimeoutMs;
    case Phase::CallerConnecting:
    case Phase::CalleeAwaitingAdmission:
    case Phase::CalleeConnecting:
        return kMediaSetupTimeoutMs;
    default:
        return 0;
    }
}

bool phaseTimedOut(const State& state, uint32_t now_ms)
{
    const uint32_t timeout_ms = phaseTimeoutMs(state);
    return timeout_ms != 0 &&
           now_ms - state.phase_started_ms >= timeout_ms;
}

const char* phaseName(Phase phase)
{
    switch (phase)
    {
    case Phase::Idle:
        return "idle";
    case Phase::CallerAwaitingLink:
        return "caller_await_link";
    case Phase::CallerAwaitingAvailability:
        return "caller_await_available";
    case Phase::CallerAwaitingRinging:
        return "caller_await_ringing";
    case Phase::CallerNegotiating:
        return "caller_negotiating";
    case Phase::CallerConnecting:
        return "caller_connecting";
    case Phase::CalleeAwaitingLink:
        return "callee_await_link";
    case Phase::CalleeAwaitingIdentity:
        return "callee_await_identity";
    case Phase::CalleeAwaitingAdmission:
        return "callee_admission";
    case Phase::CalleeRinging:
        return "callee_ringing";
    case Phase::CalleeConnecting:
        return "callee_connecting";
    case Phase::Active:
        return "active";
    case Phase::Closing:
        return "closing";
    case Phase::Closed:
        return "closed";
    }
    return "unknown";
}

} // namespace chat::reticulum::lxst::call
