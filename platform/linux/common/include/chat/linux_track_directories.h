#pragma once
#include "chat/ports/track_delivery_binding.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include <filesystem>
#include <functional>
#include <memory>

namespace trailmate::linux_app
{
// One protocol-worker owner across its selected IP/RF paths. All callbacks are
// synchronous; the HostLink side sees only the separate metadata registry.
class LinuxTrackDirectories
{
  public:
    struct Transport
    {
        std::function<bool(const uint8_t destination[16])> request_path;
        std::function<bool(const uint8_t destination[16], const uint8_t public_key[64],
                           chat::lxmf::ByteSpan data, chat::MessageId)>
            send;
        std::function<chat::MessageId()> next_message;
    };
    LinuxTrackDirectories(std::filesystem::path database, const platform::linux_runtime::LinuxReticulumIdentity&, Transport);
    ~LinuxTrackDirectories();
    bool observe(const chat::lxmf::TrackDirectoryAnnouncementView&, uint64_t now);
    bool receive(chat::lxmf::ByteSpan type, const chat::lxmf::CustomDeliveryView&, uint64_t now);
    void tick(uint64_t now, bool enabled, bool online, uint64_t context_generation);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace trailmate::linux_app
