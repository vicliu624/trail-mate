#pragma once
#include "hostlink/management_envelope.h"
#include "track_sharing/publication_journal.h"
#include <array>
#include <filesystem>

namespace platform::linux_runtime
{
struct TrackPublicationRequest
{
    std::array<uint8_t, 16> session{};
    std::array<uint8_t, 160> body{};
    size_t bytes = 0;
    uint16_t code = 0;
    uint64_t serial = 0;
};
struct TrackPublicationReply
{
    ::hostlink::management::Status status = ::hostlink::management::Status::Busy;
    track_sharing::PublicationJournal job;
    bool valid = false, active = false;
};
// Single protocol-worker registration, scoped to the current author/database.
// Requests and replies own their bounded bytes. No callback or owner pointer is
// borrowed by the frontend, including when a USB/TCP request disappears.
class TrackPublicationPublisher
{
  public:
    TrackPublicationPublisher(std::filesystem::path database, uint32_t generation, const uint8_t actor[32]);
    ~TrackPublicationPublisher();
    TrackPublicationPublisher(const TrackPublicationPublisher&) = delete;
    TrackPublicationPublisher& operator=(const TrackPublicationPublisher&) = delete;
    bool valid() const { return token_ != 0; }
    bool take(TrackPublicationRequest&);
    bool current(uint64_t serial) const;
    void complete(uint64_t serial, ::hostlink::management::Status, const track_sharing::PublicationJournal*, bool active);
    void update(const track_sharing::PublicationJournal&, bool active);
    void clearActive();
    void invalidate();

  private:
    uint64_t token_ = 0;
};
bool track_publication_available(const std::filesystem::path&);
bool track_publication_busy(const std::filesystem::path&);
::hostlink::management::Status request_track_publication(const std::filesystem::path&, const ::hostlink::management::EnvelopeView&,
                                                         geocaching::ByteView operation, uint32_t generation, TrackPublicationReply&);
void close_track_publication(const std::filesystem::path&, const uint8_t* session);
} // namespace platform::linux_runtime
