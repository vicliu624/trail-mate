#pragma once
#include "hostlink/management_envelope.h"
#include "track_sharing/directory_pool.h"
#include "track_sharing/publication_directory.h"
#include <array>
#include <filesystem>

namespace platform::linux_runtime
{
// Mesh-worker registration. Only bounded metadata and discovery commands cross
// this seam; transport objects and callback views never leave their owner.
class TrackDirectoryPublisher
{
  public:
    explicit TrackDirectoryPublisher(std::filesystem::path database, bool enabled = true);
    ~TrackDirectoryPublisher();
    TrackDirectoryPublisher(const TrackDirectoryPublisher&) = delete;
    TrackDirectoryPublisher& operator=(const TrackDirectoryPublisher&) = delete;
    bool valid() const { return token_ != 0; }
    struct Consumer
    {
        uint64_t generation = 0;
        bool active = false;
    };
    Consumer consumer(uint64_t now);
    void publish(const track_sharing::TrackDirectoryPool&, bool online, uint64_t consumer_generation);
    bool takeDiscovery(std::array<uint8_t, 16>&);

  private:
    uint64_t token_ = 0;
};
bool track_directories_available(const std::filesystem::path& database);
::hostlink::management::Status read_track_directories(const std::filesystem::path&, const uint8_t session[16], size_t cursor, uint8_t*, size_t, size_t&);
::hostlink::management::Status request_track_directory(const std::filesystem::path&, const uint8_t session[16], const uint8_t destination[16]);
void close_track_directories(const std::filesystem::path&, const uint8_t* session);
::hostlink::management::Status pin_track_directory(const std::filesystem::path&,const uint8_t session[16],geocaching::ByteView delivery,
                                                   geocaching::ByteView epoch,track_sharing::PublicationDirectory&);
} // namespace platform::linux_runtime
