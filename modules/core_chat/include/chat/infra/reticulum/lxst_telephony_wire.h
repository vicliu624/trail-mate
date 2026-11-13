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
 * @file lxst_telephony_wire.h
 * @brief LXST telephony signalling and Codec2 frame MsgPack subset.
 */

#pragma once

#include "chat/infra/reticulum/audio_call_wire.h"

#include <cstddef>
#include <cstdint>

namespace chat::reticulum::lxst
{

constexpr uint16_t kStatusBusy = 0x00;
constexpr uint16_t kStatusRejected = 0x01;
constexpr uint16_t kStatusCalling = 0x02;
constexpr uint16_t kStatusAvailable = 0x03;
constexpr uint16_t kStatusRinging = 0x04;
constexpr uint16_t kStatusConnecting = 0x05;
constexpr uint16_t kStatusEstablished = 0x06;
constexpr uint16_t kPreferredMode = 0xF0;
constexpr uint16_t kPreferredProfile = 0xFF;

constexpr uint16_t kModeFullDuplex = 0x01;
constexpr uint16_t kModeHalfDuplex = 0x02;
constexpr uint16_t kDefaultMode = kModeFullDuplex;

constexpr uint16_t kProfileBandwidthUltraLow = 0x10;
constexpr uint16_t kProfileBandwidthVeryLow = 0x20;
constexpr uint16_t kProfileBandwidthLow = 0x30;
constexpr uint16_t kProfileQualityMedium = 0x40;
constexpr uint16_t kProfileQualityHigh = 0x50;
constexpr uint16_t kProfileQualityMax = 0x60;
constexpr uint16_t kProfileLatencyUltraLow = 0x70;
constexpr uint16_t kProfileLatencyLow = 0x80;

constexpr uint8_t kCodecRaw = 0x00;
constexpr uint8_t kCodecOpus = 0x01;
constexpr uint8_t kCodec2 = 0x02;

struct DecodedAudioFrame
{
    uint8_t codec = 0xFF;
    audio_call::Codec2Mode codec2_mode = audio_call::Codec2Mode::Mode1200;
    bool codec2_mode_valid = false;
    const uint8_t* encoded = nullptr;
    std::size_t encoded_len = 0;
};

struct DecodedPacket
{
    static constexpr std::size_t kMaxSignals = 4;
    static constexpr std::size_t kMaxFrames = 4;

    uint16_t signals[kMaxSignals] = {};
    std::size_t signal_count = 0;
    DecodedAudioFrame frames[kMaxFrames] = {};
    std::size_t frame_count = 0;
};

bool profileToCodec2Mode(uint16_t profile,
                         audio_call::Codec2Mode* out_mode);
bool codec2ModeToProfile(audio_call::Codec2Mode mode,
                         uint16_t* out_profile);

bool encodeSignalling(uint16_t signal,
                      uint8_t* out,
                      std::size_t* inout_len);
bool encodeSignalling(const uint16_t* signals,
                      std::size_t signal_count,
                      uint8_t* out,
                      std::size_t* inout_len);
bool encodeCodec2Frames(audio_call::Codec2Mode mode,
                        const uint8_t* encoded,
                        std::size_t encoded_len,
                        uint8_t* out,
                        std::size_t* inout_len);
bool decodePacket(const uint8_t* data,
                  std::size_t len,
                  DecodedPacket* out_packet);

} // namespace chat::reticulum::lxst
