#pragma once
#include "chat/linux_reticulum_propagation.h"
#include "chat/linux_reticulum_stamp.h"

namespace trailmate::linux_app
{
// Single worker-owned outgoing task. The worker retains the original command
// for automatic direct fallback; this owner retains only the encrypted payload.
class LinuxReticulumUpload
{
  public:
    class Transport
    {
      public:
        virtual ~Transport() = default;
        virtual LinuxReticulumLinks::OpenResult open(const chat::lxmf::runtime::PropagationPeerState&) = 0;
        virtual void cancel(const uint8_t destination[16]) = 0;
        virtual void discover(const uint8_t destination[16]) = 0;
        virtual LinuxReticulumLinks::ServiceRequestResult upload(const uint8_t destination[16], const uint8_t*, size_t, uint8_t hash[32]) = 0;
        virtual bool uploadResult(const uint8_t hash[32], LinuxReticulumLinks::UploadResult&) = 0;
        virtual bool hasUpload(const uint8_t hash[32]) = 0;
    };
    struct Result
    {
        chat::MessageId message = 0;
        bool uploaded = false;
    };
    LinuxReticulumLinks::QueueResult start(const LinuxReticulumLinks&, const uint8_t destination[16], const uint8_t key[64],
                                           const std::string&, chat::MessageId, double timestamp, uint32_t now);
    void tick(const chat::reticulum::LxmfPropagationClientConfig&, bool allowed,
              const chat::lxmf::runtime::PropagationPeerState*, uint32_t now, uint32_t utc, Transport&);
    void cancel(Transport&);
    bool active() const { return message_ != 0; }
    chat::MessageId messageId() const { return message_; }
    std::optional<Result> takeResult();

  private:
    enum class Stage
    {
        WaitingNode,
        Stamping,
        Opening,
        Uploading
    } stage_ = Stage::WaitingNode;
    chat::MessageId message_ = 0;
    uint32_t started_ms_ = 0;
    std::array<uint8_t, 32> transient_{}, resource_hash_{};
    chat::lxmf::runtime::ResourcePayloadBuffer encrypted_, batch_;
    std::optional<chat::lxmf::runtime::PropagationPeerState> node_;
    std::optional<Result> result_;
    LinuxReticulumStamp stamp_;
    void finish(Transport&, bool uploaded);
};
} // namespace trailmate::linux_app
