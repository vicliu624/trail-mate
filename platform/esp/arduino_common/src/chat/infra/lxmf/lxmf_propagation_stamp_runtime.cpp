/**
 * @file lxmf_propagation_stamp_runtime.cpp
 * @brief Incremental official LXMF propagation stamp generation for ESP
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_propagation_stamp_runtime.h"
#include "chat/infra/lxmf/lxmf_propagation_stamp.h"
#include "platform/esp/common/mbedtls_sha256_compat.h"

#include <cstring>

#include <esp_heap_caps.h>
#include <esp_random.h>

namespace chat::lxmf::runtime
{
PropagationStampRuntime::PropagationStampRuntime()
{
    mbedtls_sha256_init(&base_hash_);
}

PropagationStampRuntime::~PropagationStampRuntime()
{
    reset();
    mbedtls_sha256_free(&base_hash_);
}

bool PropagationStampRuntime::begin(
    const uint8_t transient_id[reticulum::kFullHashSize],
    uint8_t target_cost)
{
    if (!transient_id)
    {
        return false;
    }

    reset();
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) <
        kMinInternalShaFreeBytes)
    {
        state_ = State::Failed;
        return false;
    }

    workblock_ = static_cast<uint8_t*>(
        heap_caps_malloc(kWorkblockBytes,
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!workblock_)
    {
        state_ = State::Failed;
        return false;
    }

    std::memcpy(transient_id_, transient_id, sizeof(transient_id_));
    target_cost_ = target_cost;
    state_ = State::Expanding;
    return true;
}

PropagationStampRuntime::State PropagationStampRuntime::poll(
    uint16_t expand_round_budget,
    uint16_t search_round_budget)
{
    while (state_ == State::Expanding && expand_round_budget-- != 0)
    {
        if (!expandOneRound())
        {
            failAndRelease();
            break;
        }
        if (expanded_rounds_ == kExpandRounds && !prepareSearch())
        {
            failAndRelease();
            break;
        }
    }

    while (state_ == State::Searching && search_round_budget-- != 0)
    {
        if (!searchOneRound())
        {
            failAndRelease();
            break;
        }
    }
    return state_;
}

bool PropagationStampRuntime::takeStamp(
    uint8_t out_stamp[reticulum::kFullHashSize])
{
    if (!out_stamp || state_ != State::Complete)
    {
        return false;
    }
    std::memcpy(out_stamp, stamp_, sizeof(stamp_));
    reset();
    return true;
}

void PropagationStampRuntime::reset()
{
    if (workblock_)
    {
        heap_caps_free(workblock_);
        workblock_ = nullptr;
    }
    if (base_hash_ready_)
    {
        mbedtls_sha256_free(&base_hash_);
        mbedtls_sha256_init(&base_hash_);
    }
    std::memset(transient_id_, 0, sizeof(transient_id_));
    std::memset(stamp_, 0, sizeof(stamp_));
    state_ = State::Idle;
    expanded_rounds_ = 0;
    search_rounds_ = 0;
    target_cost_ = 0;
    base_hash_ready_ = false;
}

void PropagationStampRuntime::failAndRelease()
{
    if (workblock_)
    {
        heap_caps_free(workblock_);
        workblock_ = nullptr;
    }
    if (base_hash_ready_)
    {
        mbedtls_sha256_free(&base_hash_);
        mbedtls_sha256_init(&base_hash_);
    }
    std::memset(transient_id_, 0, sizeof(transient_id_));
    std::memset(stamp_, 0, sizeof(stamp_));
    state_ = State::Failed;
    expanded_rounds_ = 0;
    search_rounds_ = 0;
    target_cost_ = 0;
    base_hash_ready_ = false;
}

bool PropagationStampRuntime::expandOneRound()
{
    static_assert(kExpandRounds == kPropagationStampExpandRounds && kRoundBytes == kPropagationStampRoundBytes);
    if (!workblock_ || expanded_rounds_ >= kExpandRounds)
    {
        return false;
    }

    uint8_t* output =
        workblock_ + static_cast<std::size_t>(expanded_rounds_) * kRoundBytes;
    if (!expandPropagationStampRound(transient_id_, expanded_rounds_, output))
    {
        return false;
    }
    ++expanded_rounds_;
    return true;
}

bool PropagationStampRuntime::prepareSearch()
{
    if (!workblock_ || expanded_rounds_ != kExpandRounds)
    {
        return false;
    }
    if (::platform::esp::common::crypto::sha256_starts(&base_hash_, 0) != 0 ||
        ::platform::esp::common::crypto::sha256_update(
            &base_hash_, workblock_, kWorkblockBytes) != 0)
    {
        return false;
    }
    base_hash_ready_ = true;
    state_ = State::Searching;
    return true;
}

bool PropagationStampRuntime::searchOneRound()
{
    if (!base_hash_ready_)
    {
        return false;
    }
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) <
        kMinInternalShaFreeBytes)
    {
        return false;
    }

    esp_fill_random(stamp_, sizeof(stamp_));
    mbedtls_sha256_context attempt;
    mbedtls_sha256_init(&attempt);
    mbedtls_sha256_clone(&attempt, &base_hash_);
    uint8_t hash[reticulum::kFullHashSize] = {};
    const bool hashed = ::platform::esp::common::crypto::sha256_update(
                            &attempt, stamp_, sizeof(stamp_)) == 0 &&
                        ::platform::esp::common::crypto::sha256_finish(
                            &attempt, hash) == 0;
    mbedtls_sha256_free(&attempt);
    if (!hashed)
    {
        return false;
    }

    ++search_rounds_;
    if (meetsTarget(hash, target_cost_))
    {
        state_ = State::Complete;
    }
    return true;
}

bool PropagationStampRuntime::meetsTarget(
    const uint8_t hash[reticulum::kFullHashSize],
    uint8_t target_cost)
{
    return propagationStampMeetsTarget(hash, target_cost);
}

} // namespace chat::lxmf::runtime
