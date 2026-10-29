#pragma once
#include "platform/linux/hostlink_upload_io.h"
#include "platform/linux/track_publication_transport.h"
#include "track_sharing/network_upload_response.h"
#include "track_sharing/publication_network_session.h"
#include <algorithm>
#include <functional>
#include <openssl/rand.h>

namespace platform::linux_runtime
{
// Replayable subexchange of an already durable publication intent. Restart
// repeats BEGIN by content hash; the directory supplies its durable offset.
// Local volatile offsets never authorize publication or a Confirmed result.
class TrackBlobUpload
{
  public:
    TrackBlobUpload(UploadIo& io, const track_sharing::PublicationDirectory& directory, const uint8_t local[16],
                    const char* path, const uint8_t hash[32], uint32_t bytes, TrackPublicationTransport transport,
                    TrackPublicationMessageSource messages, std::function<bool()> current)
        : io_(io), directory_(directory), path_(path), bytes_(bytes), transport_(std::move(transport)), messages_(std::move(messages)), current_(std::move(current))
    {
        std::copy_n(local, 16, local_.begin());
        std::copy_n(hash, 32, hash_.begin());
    }
    bool pending() const { return pending_; }
    void cancelPending() { pending_ = false; }
    bool done() const { return phase_ == Phase::Done; }
    void pause()
    {
        paused_ = true;
        window_.pause(*this);
    }
    bool resume(uint64_t now)
    {
        paused_ = false;
        return !window_.request().size || window_.resume(now);
    }
    track_sharing::NetworkSessionResult advance(uint64_t now)
    {
        using namespace track_sharing;
        if (!current_ || !current_() || !io_.current()) return NetworkSessionResult::Conflict;
        if (done()) return NetworkSessionResult::Progress;
        if (paused_) return NetworkSessionResult::Paused;
        if (!window_.request().size)
        {
            std::array<uint8_t, 16> nonce{};
            if (RAND_bytes(nonce.data(), nonce.size()) != 1 || !window_.begin({nonce.data(), 16}, now, 30000, 3000, 8)) return NetworkSessionResult::Invalid;
        }
        const auto poll = window_.poll(*this, now);
        if (poll == ExchangePoll::Completed) return NetworkSessionResult::Progress;
        if (poll == ExchangePoll::Waiting) return NetworkSessionResult::Waiting;
        if (poll == ExchangePoll::Paused || poll == ExchangePoll::Exhausted) return NetworkSessionResult::Paused;
        if (poll != ExchangePoll::Ready) return NetworkSessionResult::Invalid;
        size_t written = 0;
        if (phase_ == Phase::Begin)
        {
            operation_ = 4;
            if (!encodeTrackUploadBegin(window_.request(), {hash_.data(), 32}, bytes_, 8192, wire_.data(), wire_.size(), written)) return NetworkSessionResult::Invalid;
        }
        else if (phase_ == Phase::Chunk)
        {
            operation_ = 5;
            sent_ = uint16_t(std::min<uint32_t>(chunk_limit_, bytes_ - offset_));
            const auto opened = io_.open(path_, UploadIo::Mode::Read);
            uint64_t bytes = 0;
            if (!opened.handle) return NetworkSessionResult::StorageFailure;
            const bool read = io_.size(opened.handle, bytes) && bytes == bytes_ && io_.seek(opened.handle, offset_) && io_.read(opened.handle, file_.data(), sent_) == sent_;
            const bool closed = io_.close(opened.handle);
            if (!read || !closed) return NetworkSessionResult::StorageFailure;
            if (!encodeTrackUploadChunk(window_.request(), {token_.data(), 16}, offset_, {file_.data(), sent_}, 8192, wire_.data(), wire_.size(), written)) return NetworkSessionResult::Invalid;
        }
        else
        {
            operation_ = 6;
            if (!encodeTrackUploadFinish(window_.request(), {token_.data(), 16}, 8192, wire_.data(), wire_.size(), written)) return NetworkSessionResult::Invalid;
        }
        if (!transport_ || !messages_) return NetworkSessionResult::Invalid;
        const auto id = messages_();
        pending_ = true;
        const bool queued = id && transport_(directory_.delivery, directory_.public_key, {wire_.data(), written}, id);
        if (!queued) pending_ = false;
        if (!window_.sent(queued, now)) return NetworkSessionResult::Invalid;
        return queued ? NetworkSessionResult::Queued : NetworkSessionResult::Rejected;
    }
    bool receive(const chat::lxmf::CustomDeliveryView& view)
    {
        using namespace track_sharing;
        if (!pending_ || paused_ || !current_ || !current_() || !view.source.data || view.source.size != 16 ||
            !view.destination.data || view.destination.size != 16 || std::memcmp(view.source.data, directory_.delivery, 16) ||
            std::memcmp(view.destination.data, local_.data(), 16) || !view.data.data || !view.data.size || view.data.size > 8192) return false;
        const geocaching::ByteView bytes{view.data.data, view.data.size};
        if (operation_ == 4)
        {
            NetworkUploadBegin begin;
            if (!decodeTrackUploadBegin(bytes, window_.request(), bytes_, begin)) return false;
            offset_ = begin.offset;
            chunk_limit_ = begin.chunk_bytes;
            if (begin.sealed) phase_ = Phase::Done;
            else
            {
                std::copy_n(begin.token.data, 16, token_.begin());
                phase_ = offset_ == bytes_ ? Phase::Finish : Phase::Chunk;
            }
        }
        else if (operation_ == 5)
        {
            if (!decodeTrackUploadAck(bytes, window_.request(), {token_.data(), 16}, offset_, sent_, bytes_)) return false;
            offset_ += sent_;
            phase_ = offset_ == bytes_ ? Phase::Finish : Phase::Chunk;
        }
        else if (operation_ == 6)
        {
            if (!decodeTrackUploadSealed(bytes, window_.request(), {hash_.data(), 32}, bytes_)) return false;
            phase_ = Phase::Done;
        }
        else return false;
        pending_ = false;
        return true;
    }

  private:
    enum class Phase
    {
        Begin,
        Chunk,
        Finish,
        Done
    };
    UploadIo& io_;
    const track_sharing::PublicationDirectory& directory_;
    const char* path_;
    uint32_t bytes_, offset_ = 0;
    uint16_t chunk_limit_ = 0, sent_ = 0;
    uint8_t operation_ = 0;
    Phase phase_ = Phase::Begin;
    bool pending_ = false, paused_ = false;
    std::array<uint8_t, 16> local_{}, token_{};
    std::array<uint8_t, 32> hash_{};
    std::array<uint8_t, 1536> wire_{};
    std::array<uint8_t, 1024> file_{};
    track_sharing::RequestRetryWindow window_;
    TrackPublicationTransport transport_;
    TrackPublicationMessageSource messages_;
    std::function<bool()> current_;
};
} // namespace platform::linux_runtime
