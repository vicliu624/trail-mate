/*
 * Reticulum License - RNS/LXMF-derived portions.
 * Derived from https://github.com/markqvist/Reticulum
 * Copyright (c) 2016-2026 Mark Qvist (Reticulum-derived portions).
 * Derived from https://github.com/markqvist/LXMF
 * Copyright (c) 2020-2026 Mark Qvist (LXMF-derived portions).
 * Full license: LICENSES/Reticulum-Derivatives.txt.
 * Original notices: LICENSES/Reticulum-RNS.txt and LICENSES/Reticulum-LXMF.txt.
 * Other contributions retain their applicable terms; see LICENSE.
 * Component scope: docs/licensing/reticulum-sources.csv.
 */

/**
 * @file lxmf_propagation_stamp_runtime.h
 * @brief Incremental official LXMF propagation stamp generation for ESP
 */

#pragma once

#include "chat/infra/reticulum/reticulum_wire.h"

#include <cstddef>
#include <cstdint>

#include <mbedtls/sha256.h>

namespace chat::lxmf::runtime
{

class PropagationStampRuntime
{
  public:
    enum class State : uint8_t
    {
        Idle = 0,
        Expanding = 1,
        Searching = 2,
        Complete = 3,
        Failed = 4,
    };

    PropagationStampRuntime();
    ~PropagationStampRuntime();

    PropagationStampRuntime(const PropagationStampRuntime&) = delete;
    PropagationStampRuntime& operator=(const PropagationStampRuntime&) = delete;

    bool begin(const uint8_t transient_id[reticulum::kFullHashSize],
               uint8_t target_cost);
    State poll(uint16_t expand_round_budget = 4,
               uint16_t search_round_budget = 256);
    bool takeStamp(uint8_t out_stamp[reticulum::kFullHashSize]);
    void reset();

    State state() const { return state_; }
    uint16_t expandedRounds() const { return expanded_rounds_; }
    uint32_t searchRounds() const { return search_rounds_; }
    uint8_t targetCost() const { return target_cost_; }

  private:
    static constexpr uint16_t kExpandRounds = 1000;
    static constexpr std::size_t kRoundBytes = 256;
    static constexpr std::size_t kWorkblockBytes =
        kExpandRounds * kRoundBytes;
    static constexpr std::size_t kMinInternalShaFreeBytes = 16 * 1024;

    void failAndRelease();
    bool expandOneRound();
    bool prepareSearch();
    bool searchOneRound();
    static bool meetsTarget(const uint8_t hash[reticulum::kFullHashSize],
                            uint8_t target_cost);

    uint8_t* workblock_ = nullptr;
    uint8_t transient_id_[reticulum::kFullHashSize] = {};
    uint8_t stamp_[reticulum::kFullHashSize] = {};
    mbedtls_sha256_context base_hash_{};
    State state_ = State::Idle;
    uint16_t expanded_rounds_ = 0;
    uint32_t search_rounds_ = 0;
    uint8_t target_cost_ = 0;
    bool base_hash_ready_ = false;
};

} // namespace chat::lxmf::runtime
