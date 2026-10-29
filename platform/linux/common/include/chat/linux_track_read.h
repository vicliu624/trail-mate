#pragma once
#include "chat/ports/track_delivery_binding.h"
#include "hostlink/management_envelope.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include "track_sharing/publication_directory.h"
#include <filesystem>
#include <functional>
#include <memory>

namespace trailmate::linux_app
{
class LinuxTrackRead
{
  public:
    using Directory = std::function<::hostlink::management::Status(const uint8_t*, geocaching::ByteView, geocaching::ByteView, track_sharing::PublicationDirectory&)>;
    using Transport = std::function<bool(const uint8_t*, const uint8_t*, chat::lxmf::ByteSpan, chat::MessageId)>;
    using MessageSource = std::function<chat::MessageId()>;
    LinuxTrackRead(std::filesystem::path storage, std::filesystem::path database, const platform::linux_runtime::LinuxReticulumIdentity&, Directory, Transport, MessageSource);
    ~LinuxTrackRead();
    void tick(uint64_t now, bool context_current, uint64_t context_generation);
    bool receive(chat::lxmf::ByteSpan, const chat::lxmf::CustomDeliveryView&, uint64_t now);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace trailmate::linux_app
