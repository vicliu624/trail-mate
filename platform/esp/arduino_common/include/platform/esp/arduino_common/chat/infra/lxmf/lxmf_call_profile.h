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
 * @file lxmf_call_profile.h
 * @brief Single embedded LXST media-profile mapping for the Reticulum client
 */

#pragma once

#include "chat/domain/chat_types.h"
#include "chat/infra/reticulum/audio_call_wire.h"
#include "chat/infra/reticulum/lxst_telephony_wire.h"
#include "platform/ui/reticulum_call_runtime.h"

#include <cstdint>

#ifndef TRAIL_MATE_ENABLE_MESHCHAT_CALL_AUDIO_COMPAT
#define TRAIL_MATE_ENABLE_MESHCHAT_CALL_AUDIO_COMPAT 0
#endif

namespace chat::lxmf::call_profile
{

inline constexpr uint16_t kEmbeddedLxstProfile =
    reticulum::lxst::kProfileBandwidthLow;

inline reticulum::audio_call::Codec2Mode audioCodec2Mode(
    uint16_t lxst_profile)
{
    (void)lxst_profile;
    return reticulum::audio_call::Codec2Mode::Mode3200;
}

inline ::platform::ui::reticulum_call::Codec2Mode runtimeCodec2Mode(
    ReticulumCallWireProfile wire_profile,
    uint16_t lxst_profile)
{
#if TRAIL_MATE_ENABLE_MESHCHAT_CALL_AUDIO_COMPAT
    if (wire_profile == ReticulumCallWireProfile::MeshChatCallAudio)
    {
        return ::platform::ui::reticulum_call::Codec2Mode::Mode1200;
    }
#else
    (void)wire_profile;
#endif

    (void)lxst_profile;
    return ::platform::ui::reticulum_call::Codec2Mode::Mode3200;
}

inline ::platform::ui::reticulum_call::WireProfile runtimeWireProfile(
    ReticulumCallWireProfile wire_profile)
{
#if TRAIL_MATE_ENABLE_MESHCHAT_CALL_AUDIO_COMPAT
    return wire_profile == ReticulumCallWireProfile::MeshChatCallAudio
               ? ::platform::ui::reticulum_call::WireProfile::MeshChatCallAudio
               : ::platform::ui::reticulum_call::WireProfile::SidebandLxst;
#else
    (void)wire_profile;
    return ::platform::ui::reticulum_call::WireProfile::SidebandLxst;
#endif
}

} // namespace chat::lxmf::call_profile
