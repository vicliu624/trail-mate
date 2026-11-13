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
 * @file lxmf_lxst_telephony_client.h
 * @brief Sideband/LXST telephony runtime owner.
 */

#pragma once

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_runtime_state.h"

#include <cstddef>

namespace chat::lxmf::runtime
{

class LxstTelephonyClient
{
  public:
    LxstTelephonyClient() = default;
    LxstTelephonyClient(const LxstTelephonyClient&) = delete;
    LxstTelephonyClient& operator=(const LxstTelephonyClient&) = delete;
    LxstTelephonyClient(LxstTelephonyClient&&) = delete;
    LxstTelephonyClient& operator=(LxstTelephonyClient&&) = delete;

    uint8_t* scratch();
    const uint8_t* scratch() const;
    std::size_t scratchCapacity() const;

    bool isCallSession(const LinkSession& session) const;
    bool isSidebandSession(const LinkSession& session) const;
    bool runtimeStarted(const LinkSession& session) const;
    void markRuntimeStarted(LinkSession& session, bool started);
    void beginCallerSession(LinkSession& session,
                            ReticulumCallWireProfile wire_profile,
                            uint16_t profile,
                            uint32_t now_ms);
    void beginSidebandCalleeSession(LinkSession& session,
                                    uint16_t profile,
                                    uint32_t now_ms);
    uint16_t profile(const LinkSession& session) const;
    uint16_t mode(const LinkSession& session) const;
    reticulum::lxst::call::Phase phase(const LinkSession& session) const;
    const reticulum::lxst::call::State& state(
        const LinkSession& session) const;
    bool phaseTimedOut(const LinkSession& session, uint32_t now_ms) const;
    reticulum::lxst::call::Transition dispatch(
        LinkSession& session,
        const reticulum::lxst::call::Event& event,
        uint32_t now_ms,
        reticulum::lxst::call::Phase* out_previous_phase);
    bool encodeSignal(uint16_t signal,
                      uint8_t** out_payload,
                      std::size_t* out_len);
    bool encodeSignals(const uint16_t* signals,
                       std::size_t signal_count,
                       uint8_t** out_payload,
                       std::size_t* out_len);

  private:
    uint8_t scratch_[reticulum::kReticulumMtu] = {};
};

} // namespace chat::lxmf::runtime
