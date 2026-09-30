#pragma once
#include "geocaching/protocol/cmp_writer.h"
#include "geocaching/protocol/get_response.h"
#include "geocaching/protocol/verify_record.h"
#include "platform/esp/arduino_common/chat/infra/mesh_adapter_router.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_saved_cache.h"
#include "platform/esp/common/memory_budget.h"
#include "platform/memory/psram_ptr.h"
#include "ui_presentation/geocaching/geocaching_source.h"
#include <esp_heap_caps.h>
#include <memory>

namespace platform::esp::arduino_common::geocaching
{
// One open detail, allocated off the ESP stack. Text is copied only after
// author verification against the selected cache ID and revision hash.
struct CacheDetail
{
    enum class State
    {
        Network,
        Saved,
        Ready,
        Failed
    };
    ::geocaching::GeocacheId id;
    ::geocaching::RevisionHash hash;
    ::geocaching::Destination remote;
    ::geocaching::RequestId request;
    std::array<uint8_t, 128> request_bytes{};
    size_t request_size = 0;
    std::array<char, 2049> description{};
    std::array<char, 513> hint{};
    std::array<uint8_t, 32> receipt{};
    bool has_receipt = false;
    uint8_t* response = nullptr;
    size_t response_size = 0;
    ::geocaching::protocol::VerifiedRecordView verified_record;
    ::platform::memory::PsramPtr<SdIndexedSavedCache> saved_read;
    uint64_t started = 0, next_send = 0;
    State state = State::Network;
    const char* error = "Details unavailable. Go back and reopen to retry.";
    ~CacheDetail()
    {
        if (response) heap_caps_free(response);
    }
    bool matches(const std::array<uint8_t, 32>& cache, const std::array<uint8_t, 32>& revision) const
    {
        return id.bytes == cache && hash.bytes == revision;
    }
    bool matches(const ::geocaching::Destination& source, const ::geocaching::RequestId& incoming) const
    {
        return request_size && source.bytes == remote.bytes && incoming.bytes == request.bytes;
    }
    bool verify(::geocaching::ByteView bytes, ::geocaching::protocol::RecordCrypto& crypto)
    {
        if (state == State::Saved && !response)
        {
            response = static_cast<uint8_t*>(::platform::esp::common::memory::allocatePreferred("geocaching.detail.saved", bytes.size, false));
            if (!response) return false;
            std::memcpy(response, bytes.data, bytes.size);
            response_size = bytes.size;
            bytes = {response, response_size};
        }
        constexpr size_t capacity = ::geocaching::kMaxRecordBytes + 64;
        auto* scratch = static_cast<uint8_t*>(::platform::esp::common::memory::allocatePreferred("geocaching.detail.verify", capacity, false));
        if (!scratch) return false;
        ::geocaching::protocol::VerifiedRecordView verified;
        const bool valid = ::geocaching::protocol::verifyGeocache(bytes, crypto, scratch, capacity, verified, &id, &hash) ==
                           ::geocaching::protocol::VerificationResult::Valid;
        if (valid)
        {
            verified_record = verified;
            description.fill(0);
            hint.fill(0);
            std::memcpy(description.data(), verified.record.description.data(), verified.record.description.size());
            std::memcpy(hint.data(), verified.record.hint.data(), verified.record.hint.size());
            state = State::Ready;
        }
        heap_caps_free(scratch);
        return valid;
    }
    bool receive(const ::geocaching::Destination& source, const ::geocaching::RequestId& incoming,
                 ::geocaching::ByteView bytes, ::geocaching::protocol::RecordCrypto& crypto, uint8_t** owned)
    {
        if (!matches(source, incoming)) return false;
        if (state == State::Ready && has_receipt)
        {
            std::array<uint8_t, 32> digest{};
            return crypto.sha256(bytes, digest.data()) && digest == receipt;
        }
        if (state != State::Network || response) return false;
        response = owned ? *owned : static_cast<uint8_t*>(::platform::esp::common::memory::allocatePreferred("geocaching.detail.rx", bytes.size, false));
        if (!response) return false;
        if (owned) *owned = nullptr;
        else std::memcpy(response, bytes.data, bytes.size);
        response_size = bytes.size;
        return false;
    }
    void advanceNetwork(chat::MeshAdapterRouter& router, const ::geocaching::Destination& local,
                        ::geocaching::protocol::RecordCrypto& crypto, uint64_t now)
    {
        if (state != State::Network) return;
        if (response)
        {
            ::geocaching::protocol::GetResponseView parsed;
            if (::geocaching::protocol::decodeGetResponse({response, response_size}, request, 8192, parsed) && !parsed.has_conflict &&
                verify(parsed.signed_cache, crypto))
                has_receipt = crypto.sha256({response, response_size}, receipt.data());
            // Keep only the open, verified response so Save can transfer it to
            // the durable installation without another network request.
            if (state == State::Ready) return;
            heap_caps_free(response);
            response = nullptr;
            response_size = 0;
        }
        if (now - started >= 120000)
        {
            state = State::Failed;
            error = "Directory did not return verified details. Go back and reopen to retry.";
            return;
        }
        if (now < next_send) return;
        std::array<uint8_t, 32> accepted{};
        const auto sent = router.sendGeocachingData(remote.bytes.data(), {request_bytes.data(), request_size}, false, &accepted, local.bytes.data());
        next_send = now + (sent.ok ? 15000 : 1000);
    }
};
static_assert(sizeof(CacheDetail) < 4096, "Only the open detail owns full text");
} // namespace platform::esp::arduino_common::geocaching
