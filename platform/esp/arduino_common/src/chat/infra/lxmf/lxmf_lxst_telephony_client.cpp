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
 * @file lxmf_lxst_telephony_client.cpp
 * @brief Sideband/LXST telephony runtime owner.
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_lxst_telephony_client.h"

#include "chat/infra/reticulum/lxst_telephony_wire.h"

#ifndef TRAIL_MATE_ENABLE_MESHCHAT_CALL_AUDIO_COMPAT
#define TRAIL_MATE_ENABLE_MESHCHAT_CALL_AUDIO_COMPAT 0
#endif

namespace chat::lxmf::runtime
{

uint8_t* LxstTelephonyClient::scratch()
{
    return scratch_;
}

const uint8_t* LxstTelephonyClient::scratch() const
{
    return scratch_;
}

std::size_t LxstTelephonyClient::scratchCapacity() const
{
    return sizeof(scratch_);
}

bool LxstTelephonyClient::isCallSession(const LinkSession& session) const
{
    return session.destination == LocalDestinationKind::CallAudio;
}

bool LxstTelephonyClient::isSidebandSession(const LinkSession& session) const
{
    return isCallSession(session) &&
           session.call_wire_profile == ReticulumCallWireProfile::SidebandLxst;
}

bool LxstTelephonyClient::runtimeStarted(const LinkSession& session) const
{
    return session.call_runtime_started;
}

void LxstTelephonyClient::markRuntimeStarted(LinkSession& session, bool started)
{
    if (isCallSession(session))
    {
        session.call_runtime_started = started;
    }
}

void LxstTelephonyClient::beginCallerSession(
    LinkSession& session,
    ReticulumCallWireProfile wire_profile,
    uint16_t profile,
    uint32_t now_ms)
{
#if TRAIL_MATE_ENABLE_MESHCHAT_CALL_AUDIO_COMPAT
    session.call_wire_profile = wire_profile;
#else
    (void)wire_profile;
    session.call_wire_profile = ReticulumCallWireProfile::SidebandLxst;
#endif
    session.call_runtime_started = false;
    session.lxst_call = reticulum::lxst::call::makeCaller(profile, now_ms);
}

void LxstTelephonyClient::beginSidebandCalleeSession(LinkSession& session,
                                                     uint16_t profile,
                                                     uint32_t now_ms)
{
    session.call_wire_profile = ReticulumCallWireProfile::SidebandLxst;
    session.call_runtime_started = false;
    session.lxst_call = reticulum::lxst::call::makeCallee(profile, now_ms);
}

uint16_t LxstTelephonyClient::profile(const LinkSession& session) const
{
    return session.lxst_call.profile;
}

uint16_t LxstTelephonyClient::mode(const LinkSession& session) const
{
    return session.lxst_call.mode;
}

reticulum::lxst::call::Phase LxstTelephonyClient::phase(
    const LinkSession& session) const
{
    return session.lxst_call.phase;
}

const reticulum::lxst::call::State& LxstTelephonyClient::state(
    const LinkSession& session) const
{
    return session.lxst_call;
}

bool LxstTelephonyClient::phaseTimedOut(const LinkSession& session,
                                        uint32_t now_ms) const
{
    return isSidebandSession(session) &&
           reticulum::lxst::call::phaseTimedOut(session.lxst_call, now_ms);
}

reticulum::lxst::call::Transition LxstTelephonyClient::dispatch(
    LinkSession& session,
    const reticulum::lxst::call::Event& event,
    uint32_t now_ms,
    reticulum::lxst::call::Phase* out_previous_phase)
{
    if (out_previous_phase)
    {
        *out_previous_phase = session.lxst_call.phase;
    }
    if (!isSidebandSession(session))
    {
        return {};
    }
    return reticulum::lxst::call::dispatch(&session.lxst_call, event, now_ms);
}

bool LxstTelephonyClient::encodeSignal(uint16_t signal,
                                       uint8_t** out_payload,
                                       std::size_t* out_len)
{
    if (!out_payload || !out_len)
    {
        return false;
    }
    *out_payload = scratch_;
    *out_len = sizeof(scratch_);
    return reticulum::lxst::encodeSignalling(signal, scratch_, out_len);
}

bool LxstTelephonyClient::encodeSignals(const uint16_t* signals,
                                        std::size_t signal_count,
                                        uint8_t** out_payload,
                                        std::size_t* out_len)
{
    if (!out_payload || !out_len)
    {
        return false;
    }
    *out_payload = scratch_;
    *out_len = sizeof(scratch_);
    return reticulum::lxst::encodeSignalling(
        signals, signal_count, scratch_, out_len);
}

} // namespace chat::lxmf::runtime
