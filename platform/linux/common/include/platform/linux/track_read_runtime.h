#pragma once
#include "hostlink/management_envelope.h"
#include <array>
#include <filesystem>

namespace platform::linux_runtime
{
struct TrackReadCommand
{
    std::array<uint8_t, 16> session{};
    std::array<uint8_t, 471> body{};
    size_t bytes = 0;
    uint16_t code = 0;
    uint64_t serial = 0;
};
class TrackReadPublisher
{
  public:
    TrackReadPublisher(std::filesystem::path database, uint32_t generation);
    ~TrackReadPublisher();
    TrackReadPublisher(const TrackReadPublisher&) = delete;
    TrackReadPublisher& operator=(const TrackReadPublisher&) = delete;
    bool valid() const { return token_ != 0; }
    bool take(TrackReadCommand&);
    bool current(uint64_t serial) const;
    void complete(uint64_t serial, ::hostlink::management::Status, const uint8_t* output, size_t bytes);
    void active(const uint8_t* session);
    bool retiring();

  private:
    uint64_t token_ = 0;
};
// Bounded, copied requests only. The mesh worker owns all file/network objects.
class HostlinkTrackRead
{
  public:
    explicit HostlinkTrackRead(std::filesystem::path database) : database_(std::move(database)) {}
    bool supported() const;
    bool identityBusy() const;
    void close(const uint8_t* session = nullptr) const;
    ::hostlink::management::Status process(const ::hostlink::management::EnvelopeView&, uint8_t* output, size_t capacity, size_t& written) const;

  private:
    std::filesystem::path database_;
};
} // namespace platform::linux_runtime
