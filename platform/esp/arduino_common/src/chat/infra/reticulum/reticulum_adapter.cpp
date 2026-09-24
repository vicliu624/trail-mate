/**
 * @file reticulum_adapter.cpp
 * @brief Product-level Reticulum adapter boundary for ESP Arduino targets.
 */

#include "platform/esp/arduino_common/chat/infra/reticulum/reticulum_adapter.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_adapter.h"
#include "platform/ui/reticulum_page_runtime.h"
#include "platform/ui/reticulum_receive_runtime.h"

namespace chat::reticulum
{
namespace
{

namespace rtpage = ::platform::ui::reticulum_page;

rtpage::RequestStartCode pageRequestCodeFromFailure(MeshOperationFailure failure)
{
    switch (failure)
    {
    case MeshOperationFailure::InvalidInput:
        return rtpage::RequestStartCode::InvalidInput;
    case MeshOperationFailure::Unsupported:
        return rtpage::RequestStartCode::Unsupported;
    case MeshOperationFailure::NotReady:
    case MeshOperationFailure::LocalIdentityMissing:
    case MeshOperationFailure::RadioOffline:
        return rtpage::RequestStartCode::NotReady;
    case MeshOperationFailure::Busy:
    case MeshOperationFailure::DutyCycleLimited:
        return rtpage::RequestStartCode::Busy;
    case MeshOperationFailure::EncodeFailed:
    case MeshOperationFailure::CryptoFailed:
        return rtpage::RequestStartCode::EncodeFailed;
    case MeshOperationFailure::RadioTxFailed:
        return rtpage::RequestStartCode::RadioTxFailed;
    case MeshOperationFailure::None:
    case MeshOperationFailure::TxDisabled:
    case MeshOperationFailure::PeerKeyMissing:
    case MeshOperationFailure::ChannelKeyMissing:
    case MeshOperationFailure::Unknown:
    default:
        return rtpage::RequestStartCode::Unknown;
    }
}

rtpage::RequestStartResult startNomadPageRequest(
    const uint8_t destination_hash[rtpage::kReticulumPageDestinationTextSize / 2U],
    const char* path,
    const uint8_t* request_data,
    std::size_t request_data_len,
    void* context)
{
    rtpage::RequestStartResult out{};
    auto* service = static_cast<lxmf::LxmfAdapter*>(context);
    if (!service)
    {
        out.code = rtpage::RequestStartCode::Unsupported;
        return out;
    }

    const MeshActionResult result =
        service->requestNomadPage(destination_hash,
                                  path,
                                  request_data,
                                  request_data_len);
    if (result.ok)
    {
        out.code = result.detail == 1 ? rtpage::RequestStartCode::AlreadyPending
                                      : rtpage::RequestStartCode::Started;
        out.detail = result.detail;
        return out;
    }

    out.code = pageRequestCodeFromFailure(result.failure);
    out.detail = result.detail;
    return out;
}

bool cancelNomadPageRequest(
    const uint8_t destination_hash[rtpage::kReticulumPageDestinationTextSize / 2U],
    const char* path,
    void* context)
{
    auto* service = static_cast<lxmf::LxmfAdapter*>(context);
    return service && service->cancelNomadPage(destination_hash, path);
}

bool cancelIncomingResource(
    const uint8_t link_id[::platform::ui::reticulum_receive::kHashSize],
    const uint8_t resource_hash[32],
    void* context)
{
    auto* service = static_cast<lxmf::LxmfAdapter*>(context);
    return service && service->cancelIncomingResource(link_id, resource_hash);
}

} // namespace

ReticulumAdapter::ReticulumAdapter(LoraBoard& board,
                                   IMeshPeerDirectory* peer_directory,
                                   ReticulumUsage usage)
    : service_(new lxmf::LxmfAdapter(board, peer_directory, usage == ReticulumUsage::ActiveChat)),
      usage_(usage)
{
    if (usage_ != ReticulumUsage::ActiveChat)
    {
        return;
    }
    rtpage::bind_request_start_handler(startNomadPageRequest, service_.get());
    rtpage::bind_request_cancel_handler(cancelNomadPageRequest, service_.get());
    ::platform::ui::reticulum_receive::bind_cancel_handler(
        cancelIncomingResource, service_.get());
}

ReticulumAdapter::~ReticulumAdapter()
{
    if (usage_ != ReticulumUsage::ActiveChat)
    {
        return;
    }
    rtpage::bind_request_start_handler(nullptr, nullptr);
    rtpage::bind_request_cancel_handler(nullptr, nullptr);
    ::platform::ui::reticulum_receive::bind_cancel_handler(nullptr, nullptr);
}

MeshCapabilities ReticulumAdapter::getCapabilities() const
{
    return service_->getCapabilities();
}

void ReticulumAdapter::setGeocachingAnnouncementHandler(
    void (*handler)(const lxmf::GeocachingAnnouncementView&, void*), void* context)
{
    service_->setGeocachingAnnouncementHandler(handler, context);
}

bool ReticulumAdapter::getGeocachingAuthorKey(uint8_t out[64])
{
    return service_->getGeocachingAuthorKey(out);
}

bool ReticulumAdapter::signGeocachingRecord(lxmf::ByteSpan record, uint8_t* workspace, size_t workspace_capacity,
                                            uint8_t* output, size_t output_capacity, size_t& written)
{
    return service_->signGeocachingRecord(record, workspace, workspace_capacity, output, output_capacity, written);
}

MeshSendResult ReticulumAdapter::sendGeocachingData(const uint8_t destination_hash[16],
                                                    lxmf::ByteSpan data, bool response, std::array<uint8_t, 32>* accepted_lxmf_hash)
{
    return service_->sendCustomDataToDestination(destination_hash, "trailmate.geocache", data, response, accepted_lxmf_hash);
}

void ReticulumAdapter::setGeocachingDeliveryHandler(
    bool (*handler)(const lxmf::CustomDeliveryView&, void*), void* context)
{
    service_->setGeocachingDeliveryHandler(handler, context);
}

bool ReticulumAdapter::sendText(ChannelId channel, const std::string& text,
                                MessageId* out_msg_id, NodeId peer)
{
    return service_->sendText(channel, text, out_msg_id, peer);
}

MeshSendResult ReticulumAdapter::sendTextDetailed(ChannelId channel, const std::string& text,
                                                  MessageId forced_msg_id,
                                                  NodeId peer)
{
    return service_->sendTextDetailed(channel, text, forced_msg_id, peer);
}

MeshSendResult ReticulumAdapter::sendTextToReticulumDestination(
    ChannelId channel,
    const std::string& text,
    MessageId forced_msg_id,
    const ReticulumPeerIdentity& destination)
{
    return service_->sendTextToReticulumDestination(channel, text, forced_msg_id, destination);
}

bool ReticulumAdapter::pollIncomingText(MeshIncomingText* out)
{
    return service_->pollIncomingText(out);
}

IIncomingDeliveryCommitPort* ReticulumAdapter::incomingDeliveryCommitPort()
{
    return this;
}

void ReticulumAdapter::commitIncomingText(const MeshIncomingText& message,
                                          bool durably_accepted)
{
    service_->commitIncomingText(message, durably_accepted);
}

bool ReticulumAdapter::sendAppData(ChannelId channel, uint32_t portnum,
                                   const uint8_t* payload, size_t len,
                                   NodeId dest, bool want_ack,
                                   MessageId packet_id, bool want_response)
{
    return service_->sendAppData(channel,
                                 portnum,
                                 payload,
                                 len,
                                 dest,
                                 want_ack,
                                 packet_id,
                                 want_response);
}

bool ReticulumAdapter::pollIncomingData(MeshIncomingData* out)
{
    return service_->pollIncomingData(out);
}

bool ReticulumAdapter::requestNodeInfo(NodeId dest, bool want_response)
{
    return service_->requestNodeInfo(dest, want_response);
}

bool ReticulumAdapter::broadcastSelfIdentity()
{
    return service_->broadcastSelfIdentity();
}

NodeId ReticulumAdapter::getNodeId() const
{
    return service_->getNodeId();
}

bool ReticulumAdapter::deriveVmpContactSecret(NodeId peer_id,
                                              uint8_t out_secret[32])
{
    return service_ && service_->deriveVmpContactSecret(peer_id, out_secret);
}

bool ReticulumAdapter::getReticulumLocalIdentityInfo(ReticulumLocalIdentityInfo* out) const
{
    return service_->getReticulumLocalIdentityInfo(out);
}

MeshActionResult ReticulumAdapter::startReticulumAudioCall(
    const ReticulumPeerIdentity& destination)
{
    return service_->startReticulumAudioCall(destination);
}

MeshActionResult ReticulumAdapter::pingReticulumDestination(
    const ReticulumPeerIdentity& destination)
{
    return service_->pingReticulumDestination(destination);
}

void ReticulumAdapter::applyConfig(const MeshConfig& config)
{
    service_->applyConfig(config);
}

void ReticulumAdapter::setUserInfo(const char* long_name, const char* short_name)
{
    service_->setUserInfo(long_name, short_name);
}

bool ReticulumAdapter::setWifiTransportEnabled(bool enabled)
{
    return service_->setWifiTransportEnabled(enabled);
}

bool ReticulumAdapter::isReady() const
{
    return service_->isReady();
}

bool ReticulumAdapter::pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len)
{
    return service_->pollIncomingRawPacket(out_data, out_len, max_len);
}

void ReticulumAdapter::handleRawPacket(const uint8_t* data, size_t size)
{
    service_->handleRawPacket(data, size);
}

void ReticulumAdapter::setLastRxStats(float rssi, float snr)
{
    service_->setLastRxStats(rssi, snr);
}

void ReticulumAdapter::processSendQueue()
{
    service_->processSendQueue();
}

} // namespace chat::reticulum
