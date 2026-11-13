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
 * @file lxst_call_state_machine.h
 * @brief Pure Sideband/LXST telephony session state and transition policy.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace chat::reticulum::lxst::call
{

enum class Role : uint8_t
{
    Caller = 0,
    Callee = 1,
};

enum class Phase : uint8_t
{
    Idle = 0,
    CallerAwaitingLink,
    CallerAwaitingAvailability,
    CallerAwaitingRinging,
    CallerNegotiating,
    CallerConnecting,
    CalleeAwaitingLink,
    CalleeAwaitingIdentity,
    CalleeAwaitingAdmission,
    CalleeRinging,
    CalleeConnecting,
    Active,
    Closing,
    Closed,
};

enum class EventType : uint8_t
{
    LinkActive = 0,
    RemoteIdentified,
    AdmissionGranted,
    AdmissionDenied,
    RemoteSignal,
    LocalAccepted,
    MediaReady,
    LocalHangup,
    OperationFailed,
    Timeout,
    LinkClosed,
    ControlRetry,
    LocalModeSwitch,
};

struct Event
{
    EventType type = EventType::OperationFailed;
    uint16_t signal = 0;

    static Event remoteSignal(uint16_t value)
    {
        return Event{EventType::RemoteSignal, value};
    }
};

enum class Action : uint8_t
{
    SendAvailable = 0,
    SendIdentify,
    BeginRinging,
    SendRinging,
    SendPreferredProfile,
    SendPreferences,
    SendPreferredMode,
    SendConnecting,
    PrepareMedia,
    SendEstablished,
    ActivateMedia,
    SendBusy,
    SendRejected,
    CloseLink,
};

struct Transition
{
    static constexpr std::size_t kMaxActions = 4;

    bool accepted = false;
    Action actions[kMaxActions] = {};
    std::size_t action_count = 0;
};

struct State
{
    Role role = Role::Caller;
    Phase phase = Phase::Idle;
    uint16_t profile = 0;
    uint16_t mode = 0;
    uint16_t local_status = 0;
    uint16_t remote_status = 0;
    uint32_t phase_started_ms = 0;
    uint32_t last_available_ms = 0;
    bool media_prepared = false;
};

State makeCaller(uint16_t profile, uint32_t now_ms);
State makeCallee(uint16_t profile, uint32_t now_ms);
Transition dispatch(State* state, const Event& event, uint32_t now_ms);
uint32_t phaseTimeoutMs(const State& state);
bool phaseTimedOut(const State& state, uint32_t now_ms);
const char* phaseName(Phase phase);

} // namespace chat::reticulum::lxst::call
