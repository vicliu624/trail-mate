#include "chat/linux_reticulum_upload.h"

namespace trailmate::linux_app
{
LinuxReticulumLinks::QueueResult LinuxReticulumUpload::start(const LinuxReticulumLinks& encoder, const uint8_t destination[16], const uint8_t key[64],
                                                             const std::string& text, chat::MessageId id, double timestamp, uint32_t now)
{
    using Queue = LinuxReticulumLinks::QueueResult;
    if (active() || result_) return Queue::Busy;
    uint8_t message_hash[32];
    if (!id || !encoder.encodePropagatedText(destination, key, text, timestamp, encrypted_, message_hash, transient_.data())) return Queue::Invalid;
    message_ = id;
    started_ms_ = now;
    stage_ = Stage::WaitingNode;
    return Queue::Queued;
}
void LinuxReticulumUpload::finish(Transport& transport, bool uploaded)
{
    if (node_) transport.cancel(node_->propagation_hash);
    result_ = Result{message_, uploaded};
    message_ = 0;
    node_.reset();
    stamp_.reset();
    chat::lxmf::runtime::ResourcePayloadBuffer().swap(encrypted_);
    chat::lxmf::runtime::ResourcePayloadBuffer().swap(batch_);
    transient_.fill(0);
    resource_hash_.fill(0);
}
void LinuxReticulumUpload::cancel(Transport& transport)
{
    if (active()) finish(transport, false);
}
std::optional<LinuxReticulumUpload::Result> LinuxReticulumUpload::takeResult()
{
    auto result = result_;
    result_.reset();
    return result;
}
void LinuxReticulumUpload::tick(const chat::reticulum::LxmfPropagationClientConfig& config, bool allowed,
                                const chat::lxmf::runtime::PropagationPeerState* selected, uint32_t now, uint32_t utc, Transport& transport)
{
    if (!active()) return;
    if (!allowed || !config.enabled || uint32_t(now - started_ms_) > 15U * 60U * 1000U ||
        (node_ && (!selected || std::memcmp(node_->propagation_hash, selected->propagation_hash, 16) || node_->stamp_cost != selected->stamp_cost)))
    {
        finish(transport, false);
        return;
    }
    if (stage_ == Stage::WaitingNode)
    {
        if (!selected)
        {
            if (!config.automatic_node) transport.discover(config.node_hash);
            return;
        }
        node_ = *selected;
        if ((node_->transfer_limit_kb && encrypted_.size() + 96 > uint64_t(node_->transfer_limit_kb) * 1000U) ||
            !stamp_.begin(transient_.data(), node_->stamp_cost))
        {
            finish(transport, false);
            return;
        }
        stage_ = Stage::Stamping;
    }
    if (stage_ == Stage::Stamping)
    {
        const auto state = stamp_.poll();
        if (state == LinuxReticulumStamp::State::Failed)
        {
            finish(transport, false);
            return;
        }
        if (state != LinuxReticulumStamp::State::Complete) return;
        uint8_t stamp[32];
        if (!stamp_.take(stamp))
        {
            finish(transport, false);
            return;
        }
        encrypted_.insert(encrypted_.end(), stamp, stamp + sizeof(stamp));
        const std::vector<chat::lxmf::ByteSpan> messages{{encrypted_.data(), encrypted_.size()}};
        batch_.resize(encrypted_.size() + 64);
        size_t size = batch_.size();
        if (!chat::lxmf::encodePropagationBatch(utc, messages, batch_.data(), &size))
        {
            finish(transport, false);
            return;
        }
        batch_.resize(size);
        stage_ = Stage::Opening;
    }
    if (stage_ == Stage::Opening)
    {
        const auto opened = transport.open(*node_);
        if (opened == LinuxReticulumLinks::OpenResult::Opening || opened == LinuxReticulumLinks::OpenResult::Pending) return;
        if (opened != LinuxReticulumLinks::OpenResult::Active)
        {
            finish(transport, false);
            return;
        }
        const auto queued = transport.upload(node_->propagation_hash, batch_.data(), batch_.size(), resource_hash_.data());
        if (queued == LinuxReticulumLinks::ServiceRequestResult::Busy) return;
        if (queued != LinuxReticulumLinks::ServiceRequestResult::Queued)
        {
            finish(transport, false);
            return;
        }
        stage_ = Stage::Uploading;
    }
    if (stage_ == Stage::Uploading)
    {
        LinuxReticulumLinks::UploadResult result;
        if (transport.uploadResult(resource_hash_.data(), result)) finish(transport, result == LinuxReticulumLinks::UploadResult::TransportAccepted);
        else if (!transport.hasUpload(resource_hash_.data())) finish(transport, false);
    }
}
} // namespace trailmate::linux_app
