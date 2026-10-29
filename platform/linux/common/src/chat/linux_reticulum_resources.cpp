#include "chat/infra/lxmf/lxmf_resource_wire.h"
#include "chat/linux_reticulum_links.h"
#include <algorithm>
#include <bzlib.h>
#include <climits>
#include <cstring>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/rand.h>
#endif

namespace trailmate::linux_app
{
namespace r = ::chat::reticulum;
namespace state = ::chat::lxmf::runtime;
namespace
{
bool compressResource(const uint8_t* data, size_t size, state::ResourcePayloadBuffer& output, size_t limit)
{
    if (size > UINT_MAX || limit > UINT_MAX || !limit) return false;
    output.resize(limit);
    unsigned int count = static_cast<unsigned int>(limit);
    const int result = BZ2_bzBuffToBuffCompress(reinterpret_cast<char*>(output.data()), &count,
                                                const_cast<char*>(reinterpret_cast<const char*>(data)), static_cast<unsigned int>(size), 9, 0, 30);
    if (result != BZ_OK)
    {
        output.clear();
        return false;
    }
    output.resize(count);
    return true;
}
bool expandResource(const uint8_t* data, size_t size, state::ResourcePayloadBuffer& output, size_t limit)
{
    if (size > UINT_MAX || !limit || limit > state::kMaxAdvertisedResourceBytes) return false;
    output.resize(limit);
    unsigned int count = static_cast<unsigned int>(limit);
    const int result = BZ2_bzBuffToBuffDecompress(reinterpret_cast<char*>(output.data()), &count,
                                                  const_cast<char*>(reinterpret_cast<const char*>(data)), static_cast<unsigned int>(size), 0, 0);
    if (result != BZ_OK || count != limit)
    {
        output.clear();
        return false;
    }
    return true;
}
} // namespace
bool LinuxReticulumLinks::advertiseResource(Session& session, state::LinkResourceTransfer& resource, uint32_t now, Sender sender, void* user)
{
    std::array<uint8_t, r::kReticulumMtu> data{};
    size_t size = data.size();
    const size_t hashes = std::min<size_t>(resource.part_count, state::resourceHashmapCapacity(session.mdu));
    if (!::chat::lxmf::encodeResourceAdvertisement(resource.transfer_size, resource.data_size, resource.part_count,
                                                   resource.resource_hash, resource.random_hash, resource.original_hash, 1, 1, resource.request_id.data(), resource.request_id.size(), resource.flags,
                                                   resource.hashmap.data(), hashes * 4, data.data(), &size)) return false;
    if (!send(session, r::PacketContext::ResourceAdv, data.data(), size, true, sender, user)) return false;
    links_.touchOutbound(session, now);
    return true;
}
bool LinuxReticulumLinks::startResource(Session& session, const uint8_t* data, size_t size, ::chat::MessageId id,
                                        uint32_t now, Sender sender, void* user, uint8_t flags, const uint8_t* request_id, uint8_t* output_hash)
{
    auto slot = std::find_if(resource_io_.begin(), resource_io_.end(), [](const auto& io)
                             { return !io.used; });
    const bool service = session.destination == state::LocalDestinationKind::Propagation;
    if (slot == resource_io_.end() || (!service && attempts_.size() >= 32)) return false;
    auto* resource = links_.createOutgoingResource(session);
    const auto random = [](void*, uint8_t* bytes, size_t count)
    {
#if defined(TRAIL_MATE_HAS_OPENSSL)
        return count <= INT_MAX && RAND_bytes(bytes, static_cast<int>(count)) == 1;
#else
        return false;
#endif
    };
    if (!state::prepareOutgoingResource(data, size, session.derived_key, state::resourcePartCapacity(session.mtu), flags, request_id, request_id ? 16 : 0, now, 4,
                                        random, nullptr, *resource, compressResource) ||
        !advertiseResource(session, *resource, now, sender, user))
    {
        links_.discardLastOutgoingResource(session);
        return false;
    }
    slot->used = slot->outgoing = true;
    slot->service = service;
    std::memcpy(slot->link, session.link_id, 16);
    std::memcpy(slot->hash, resource->resource_hash, 32);
    slot->last_advertisement = now;
    slot->submitted.assign(resource->part_count, 0);
    if (!service) attempts_.noteLinkResourceReceipt(resource->resource_hash, session.link_id, id, now, 32);
    if (output_hash) std::memcpy(output_hash, resource->resource_hash, 32);
    return true;
}
bool LinuxReticulumLinks::requestResource(Session& session, state::LinkResourceTransfer& resource, ResourceIo& io,
                                          uint32_t now, Sender sender, void* user)
{
    const auto request = links_.buildNextResourceWindowRequest(resource);
    if (!request.valid) return false;
    state::ResourceMetadataBuffer data;
    data.push_back(request.needs_more_hashmap ? 0xff : 0);
    if (request.needs_more_hashmap) data.insert(data.end(), request.last_known_hash.begin(), request.last_known_hash.end());
    data.insert(data.end(), resource.resource_hash, resource.resource_hash + 32);
    io.wanted.clear();
    for (const auto& hash : request.requested_hashes)
    {
        data.insert(data.end(), hash.begin(), hash.end());
        for (size_t i = 0; i < resource.map_hashes.size(); ++i)
            if (resource.map_hash_known[i] && resource.map_hashes[i] == hash)
            {
                io.wanted.push_back(i);
                break;
            }
    }
    io.last_request = now;
    links_.noteResourceWindowRequested(resource, request.needs_more_hashmap, now);
    return send(session, r::PacketContext::ResourceReq, data.data(), data.size(), true, sender, user);
}
bool LinuxReticulumLinks::resourceProof(Session& session, const uint8_t hash[32], const uint8_t proof[32], Sender sender, void* user)
{
    uint8_t payload[64];
    std::memcpy(payload, hash, 32);
    std::memcpy(payload + 32, proof, 32);
    size_t size = packet_.size();
    if (!r::buildHeader1Packet(r::PacketType::Proof, r::DestinationType::Link, r::PacketContext::ResourcePrf, false,
                               session.link_id, payload, sizeof(payload), packet_.data(), &size) ||
        !sender || !sender(user, packet_.data(), size)) return false;
    ++counters_.delivery_proofs;
    return true;
}
bool LinuxReticulumLinks::acknowledge(Session& session, PendingIncoming& pending, uint32_t now, Sender sender, void* user)
{
    if (!pending.resource) return packetProof(session, pending.packet_hash, sender, user);
    if (!resourceProof(session, pending.resource_hash, pending.packet_hash, sender, user)) return false;
    if (auto* resource = links_.findIncomingResource(session, pending.resource_hash))
    {
        links_.markResourceComplete(*resource, now);
        resource->parts.clear();
    }
    for (auto& io : resource_io_)
        if (io.used && !io.outgoing && !std::memcmp(io.link, session.link_id, 16) && !std::memcmp(io.hash, pending.resource_hash, 32)) io = {};
    return true;
}
bool LinuxReticulumLinks::resourceControl(Session& session, r::PacketContext context, const uint8_t* data, size_t size,
                                          uint32_t now, Sender sender, void* user)
{
    if (context == r::PacketContext::ResourceAdv)
    {
        ::chat::lxmf::DecodedResourceAdvertisement adv;
        if (!::chat::lxmf::decodeResourceAdvertisement(data, size, &adv)) return false;
        const bool delivery = session.destination == state::LocalDestinationKind::Delivery && (adv.flags == 1 || adv.flags == 3) && adv.request_id.empty();
        const bool response = session.destination == state::LocalDestinationKind::Propagation &&
                              (adv.flags == (1 | state::kResourceResponseFlag) || adv.flags == (3 | state::kResourceResponseFlag)) && adv.request_id.size() == 16;
        if ((!delivery && !response) || adv.segment_index != 1 || adv.total_segments != 1 || !adv.transfer_size ||
            adv.transfer_size > state::kMaxAdvertisedResourceBytes || !adv.data_size || adv.data_size > state::kMaxAdvertisedResourceBytes ||
            !state::resourcePartCapacity(session.mtu) ||
            adv.part_count != (adv.transfer_size + state::resourcePartCapacity(session.mtu) - 1) / state::resourcePartCapacity(session.mtu) || adv.hashmap.size() > state::resourceHashmapCapacity(session.mdu) * 4)
        {
            (void)send(session, r::PacketContext::ResourceRcl, adv.resource_hash, 32, true, sender, user);
            return false;
        }
        if (auto* existing = links_.findIncomingResource(session, adv.resource_hash))
        {
            if (existing->flags != adv.flags || existing->request_id.size() != adv.request_id.size() ||
                (!adv.request_id.empty() && std::memcmp(existing->request_id.data(), adv.request_id.data(), adv.request_id.size())) ||
                existing->data_size != adv.data_size || existing->transfer_size != adv.transfer_size || existing->part_count != adv.part_count ||
                std::memcmp(existing->random_hash, adv.random_hash, 4) || std::memcmp(existing->original_hash, adv.original_hash, 32)) return false;
            if (existing->complete) return resourceProof(session, existing->resource_hash, existing->expected_proof, sender, user);
            for (auto& io : resource_io_)
                if (io.used && !io.outgoing && !std::memcmp(io.hash, adv.resource_hash, 32) && !std::memcmp(io.link, session.link_id, 16))
                    return requestResource(session, *existing, io, now, sender, user);
            return false;
        }
        auto* pending_request = response ? links_.findPendingRequest(session, adv.request_id.data(), adv.request_id.size()) : nullptr;
        if (response && (!pending_request || pending_request->response_ready))
        {
            (void)send(session, r::PacketContext::ResourceRcl, adv.resource_hash, 32, true, sender, user);
            return false;
        }
        auto slot = std::find_if(resource_io_.begin(), resource_io_.end(), [](const auto& io)
                                 { return !io.used; });
        if (slot == resource_io_.end() || std::count_if(resource_io_.begin(), resource_io_.end(), [](const auto& io)
                                                        { return io.used && !io.outgoing; }) >= 2)
        {
            (void)send(session, r::PacketContext::ResourceRcl, adv.resource_hash, 32, true, sender, user);
            return false;
        }
        if (session.incoming_resources.size() >= 8)
        {
            auto cached = std::find_if(session.incoming_resources.begin(), session.incoming_resources.end(), [](const auto& item)
                                       { return item.complete; });
            if (cached == session.incoming_resources.end()) return false;
            uint8_t hash[32];
            std::memcpy(hash, cached->resource_hash, 32);
            links_.eraseIncomingResource(session, hash);
        }
        auto* resource = links_.startIncomingResource(session, adv.resource_hash, adv.random_hash, adv.original_hash, adv.request_id.data(), adv.request_id.size(),
                                                      adv.hashmap.data(), adv.hashmap.size(), adv.data_size, adv.transfer_size, adv.part_count, 1, 1, adv.flags,
                                                      true, (adv.flags & 2) != 0, false, false, now, 4);
        if (!resource) return false;
        if (pending_request) pending_request->awaiting_resource = true;
        slot->used = true;
        slot->service = response;
        std::memcpy(slot->link, session.link_id, 16);
        std::memcpy(slot->hash, resource->resource_hash, 32);
        return requestResource(session, *resource, *slot, now, sender, user);
    }
    if (context == r::PacketContext::ResourceReq)
    {
        if (size < 33 || (data[0] != 0 && data[0] != 0xff)) return false;
        const size_t prefix = data[0] == 0xff ? 5 : 1;
        if (size < prefix + 32 || (size - prefix - 32) % 4 || (size - prefix - 32) / 4 > state::kMaxIncomingResourceWindow) return false;
        auto* resource = links_.findOutgoingResource(session, data + prefix);
        if (!resource || resource->complete) return false;
        auto io = std::find_if(resource_io_.begin(), resource_io_.end(), [&](const auto& item)
                               { return item.used && item.outgoing &&
                                        !std::memcmp(item.link, session.link_id, 16) && !std::memcmp(item.hash, resource->resource_hash, 32); });
        if (io == resource_io_.end()) return false;
        for (size_t offset = prefix + 32; offset < size; offset += 4)
            for (size_t i = 0; i < resource->map_hashes.size(); ++i)
                if (!std::memcmp(resource->map_hashes[i].data(), data + offset, 4))
                {
                    if (io->wanted.size() < state::kMaxIncomingResourceWindow && std::find(io->wanted.begin(), io->wanted.end(), i) == io->wanted.end()) io->wanted.push_back(i);
                    break;
                }
        if (prefix == 5)
        {
            const size_t capacity = state::resourceHashmapCapacity(session.mdu);
            for (size_t i = 0; i + 1 < resource->part_count; ++i)
                if (!std::memcmp(resource->map_hashes[i].data(), data + 1, 4) && (i + 1) % capacity == 0)
                {
                    std::array<uint8_t, 500> update{};
                    size_t length = update.size() - 32;
                    std::memcpy(update.data(), resource->resource_hash, 32);
                    const size_t hashes = std::min(capacity, size_t(resource->part_count) - i - 1);
                    if (!::chat::lxmf::encodeResourceHashmapUpdate(static_cast<uint32_t>((i + 1) / capacity),
                                                                   resource->hashmap.data() + (i + 1) * 4, hashes * 4, update.data() + 32, &length)) return false;
                    return send(session, r::PacketContext::ResourceHmu, update.data(), length + 32, true, sender, user);
                }
        }
        resource->last_activity_ms = now;
        return true;
    }
    if (context == r::PacketContext::ResourceHmu)
    {
        if (size <= 32) return false;
        auto* resource = links_.findIncomingResource(session, data);
        ::chat::lxmf::DecodedResourceHashmapUpdate update;
        if (!resource || !::chat::lxmf::decodeResourceHashmapUpdate(data + 32, size - 32, &update) ||
            !links_.applyIncomingResourceHashmapUpdate(*resource, update.segment, update.hashmap.data(), update.hashmap.size(),
                                                       state::resourceHashmapCapacity(session.mdu), now)) return false;
        for (auto& io : resource_io_)
            if (io.used && !io.outgoing && !std::memcmp(io.link, session.link_id, 16) && !std::memcmp(io.hash, data, 32))
                return requestResource(session, *resource, io, now, sender, user);
        return false;
    }
    if ((context == r::PacketContext::ResourceRcl || context == r::PacketContext::ResourceIcl) && size == 32)
    {
        if (session.destination == state::LocalDestinationKind::Propagation)
        {
            for (const auto* resource : {links_.findOutgoingResource(session, data), links_.findIncomingResource(session, data)})
                if (resource && resource->request_id.size() == 16)
                    if (auto* pending = links_.findPendingRequest(session, resource->request_id.data(), 16)) links_.erasePendingRequest(session, *pending);
        }
        if (const auto* receipt = attempts_.findLinkResourceReceipt(session.link_id, data))
        {
            const auto id = receipt->message_id;
            attempts_.removeLinkResourceReceipt(session.link_id, data);
            outcome(id, ::chat::MessageStatus::Failed, ::chat::delivery::SendFailureKind::Rejected);
        }
        links_.eraseOutgoingResource(session, data);
        links_.eraseIncomingResource(session, data);
        for (auto& io : resource_io_)
            if (io.used && !std::memcmp(io.link, session.link_id, 16) && !std::memcmp(io.hash, data, 32)) io = {};
        for (auto& pending : incoming_)
            if (pending.used && pending.resource && !std::memcmp(pending.link, session.link_id, 16) && !std::memcmp(pending.resource_hash, data, 32)) pending = {};
        return true;
    }
    return false;
}
bool LinuxReticulumLinks::resourcePart(Session& session, const r::ParsedPacket& packet, uint32_t now, Sender sender, void* user, const ::chat::RxMeta* meta)
{
    for (auto& io : resource_io_)
    {
        if (!io.used || io.outgoing || std::memcmp(io.link, session.link_id, 16)) continue;
        auto* resource = links_.findIncomingResource(session, io.hash);
        if (!resource || resource->complete || packet.payload_len > state::resourcePartCapacity(session.mtu)) continue;
        uint8_t hash[32];
        bool complete = false;
        size_t index = 0;
        if (!state::resourceJoinedHash(packet.payload, packet.payload_len, resource->random_hash, 4, hash) ||
            !links_.recordIncomingResourcePart(*resource, packet.payload, packet.payload_len, hash, now, &index, &complete)) continue;
        io.wanted.erase(std::remove(io.wanted.begin(), io.wanted.end(), index), io.wanted.end());
        if (complete)
        {
            if (meta) io.meta = *meta;
            io.meta.hop_count = static_cast<uint8_t>(packet.hops + 1);
            if (!completedResource(session, *resource, io, now, sender, user, meta))
            {
                uint8_t failed[32];
                std::memcpy(failed, resource->resource_hash, 32);
                (void)send(session, r::PacketContext::ResourceRcl, failed, 32, true, sender, user);
                links_.eraseIncomingResource(session, failed);
                io = {};
                return false;
            }
            return true;
        }
        if (io.wanted.empty() && !resource->waiting_for_hashmap) (void)requestResource(session, *resource, io, now, sender, user);
        return true;
    }
    return false;
}
bool LinuxReticulumLinks::resourceReceipt(Session& session, const uint8_t* data, size_t size, uint32_t now)
{
    if (size != 64) return false;
    auto* resource = links_.findOutgoingResource(session, data);
    const auto* receipt = attempts_.findLinkResourceReceipt(session.link_id, data);
    const bool service = session.destination == state::LocalDestinationKind::Propagation;
    if (!resource || (!service && !receipt) || !resource->waiting_for_proof || std::memcmp(resource->expected_proof, data + 32, 32)) return false;
    const auto id = receipt ? receipt->message_id : 0;
    links_.markOutgoingResourceProofReceived(*resource, data + 32, now);
    if (service)
        for (auto& upload : upload_receipts_)
            if (upload.used && !std::memcmp(upload.link, session.link_id, 16) && !std::memcmp(upload.hash, data, 32)) upload.accepted = true;
    attempts_.removeLinkResourceReceipt(session.link_id, data);
    links_.eraseOutgoingResource(session, data);
    for (auto& io : resource_io_)
        if (io.used && io.outgoing && !std::memcmp(io.link, session.link_id, 16) && !std::memcmp(io.hash, data, 32)) io = {};
    links_.touchInbound(session, now);
    if (!service) outcome(id, ::chat::MessageStatus::Delivered);
    return true;
}
bool LinuxReticulumLinks::completedResource(Session& session, state::LinkResourceTransfer& resource, ResourceIo& io,
                                            uint32_t now, Sender sender, void* user, const ::chat::RxMeta* meta)
{
    if (io.service && resource.complete)
    {
        if (resourceProof(session, resource.resource_hash, resource.expected_proof, sender, user)) io = {};
        return true;
    }
    state::ResourcePayloadBuffer data;
    uint8_t proof[32]{};
    if (!state::decodeCompletedResource(resource, session.derived_key, data, proof, expandResource)) return false;
    if (!io.service)
    {
        if (!incomingText(session, data.data(), data.size(), nullptr, 0, now, meta, resource.resource_hash, proof)) return false;
        std::memcpy(resource.expected_proof, proof, 32);
        return true;
    }
    if (resource.request_id.size() != 16 || !serviceResponse(session, data.data(), data.size(), resource.request_id.data())) return false;
    std::memcpy(resource.expected_proof, proof, 32);
    links_.markResourceComplete(resource, now);
    resource.parts.clear();
    if (resourceProof(session, resource.resource_hash, resource.expected_proof, sender, user)) io = {};
    return true;
}
bool LinuxReticulumLinks::processResources(uint32_t now, Sender sender, void* user)
{
    const auto start = resource_cursor_;
    for (size_t step = 0; step < resource_io_.size(); ++step)
    {
        const auto index = (start + step) % resource_io_.size();
        auto& io = resource_io_[index];
        if (!io.used) continue;
        resource_cursor_ = (index + 1) % resource_io_.size();
        auto* session = links_.findSession(io.link);
        if (!session || session->state != state::LinkState::Active)
        {
            io = {};
            continue;
        }
        auto* resource = io.outgoing ? links_.findOutgoingResource(*session, io.hash) : links_.findIncomingResource(*session, io.hash);
        if (!resource)
        {
            io = {};
            continue;
        }
        if (uint32_t(now - resource->created_ms) > 120000)
        {
            uint8_t hash[32];
            std::memcpy(hash, io.hash, 32);
            (void)resourceControl(*session, r::PacketContext::ResourceIcl, hash, 32, now, sender, user);
            return true;
        }
        if (!io.outgoing)
        {
            if (io.service && resource->complete)
            {
                if (uint32_t(now - io.last_request) >= 1000)
                {
                    io.last_request = now;
                    (void)completedResource(*session, *resource, io, now, sender, user, &io.meta);
                    return true;
                }
                continue;
            }
            const bool awaiting_commit = std::any_of(incoming_.begin(), incoming_.end(), [&](const auto& pending)
                                                     { return pending.used && pending.resource &&
                                                              !std::memcmp(pending.link, io.link, 16) && !std::memcmp(pending.resource_hash, io.hash, 32); });
            if (!awaiting_commit && uint32_t(now - io.last_request) >= 2000)
            {
                if (resource->consecutive_complete_index + 1 == static_cast<int32_t>(resource->part_count))
                {
                    io.last_request = now;
                    (void)completedResource(*session, *resource, io, now, sender, user, &io.meta);
                }
                else (void)requestResource(*session, *resource, io, now, sender, user);
                return true;
            }
            continue;
        }
        if (!io.wanted.empty())
        {
            const size_t index = io.wanted.front();
            io.wanted.erase(io.wanted.begin());
            if (!send(*session, r::PacketContext::Resource, resource->parts[index].data(), resource->parts[index].size(), false, sender, user))
            {
                uint8_t failed[32];
                std::memcpy(failed, io.hash, 32);
                (void)resourceControl(*session, r::PacketContext::ResourceRcl, failed, 32, now, sender, user);
                return true;
            }
            io.submitted[index] = 1;
            if (!resource->waiting_for_proof && std::all_of(io.submitted.begin(), io.submitted.end(), [](uint8_t value)
                                                            { return value != 0; }))
            {
                resource->waiting_for_proof = true;
                if (const auto* receipt = attempts_.findLinkResourceReceipt(session->link_id, io.hash)) outcome(receipt->message_id, ::chat::MessageStatus::Sent);
            }
            links_.touchOutbound(*session, now);
            return true;
        }
        if (uint32_t(now - io.last_advertisement) >= 5000)
        {
            io.last_advertisement = now;
            (void)advertiseResource(*session, *resource, now, sender, user);
            return true;
        }
    }
    return false;
}
} // namespace trailmate::linux_app
