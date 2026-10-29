#include "chat/infra/mesh_peer_directory_core.h"
#include "chat/linux_reticulum_propagation.h"
#include <cassert>

namespace wire = chat::lxmf;
using Owner = trailmate::linux_app::LinuxReticulumPropagation;
using Links = trailmate::linux_app::LinuxReticulumLinks;
using Stage = chat::lxmf::runtime::PropagationSyncStage;
struct Store final : chat::IMeshPeerDirectoryBlobStore
{
    chat::MeshPeerDirectoryBlobLoadResult loadBlob(std::vector<uint8_t>&) override { return chat::MeshPeerDirectoryBlobLoadResult::Missing; }
    bool saveBlob(const uint8_t*, size_t) override { return true; }
    void clearBlob() override {}
};
struct Transport final : Owner::Transport
{
    std::vector<wire::DecodedPropagationGetRequest> requests;
    std::vector<chat::MeshIncomingText> delivered;
    Links::ServiceResponse response;
    bool has_response = false, live_request = false, accept = true;
    unsigned cancelled = 0;
    Links::OpenResult open(const chat::lxmf::runtime::PropagationPeerState&) override { return Links::OpenResult::Active; }
    Links::ServiceRequestResult request(const uint8_t*, const uint8_t*, const uint8_t* data, size_t size, double, uint8_t* id) override
    {
        wire::DecodedPropagationGetRequest decoded;
        assert(wire::decodePropagationGetRequestPayload(data, size, &decoded));
        requests.push_back(decoded);
        std::memset(id, static_cast<int>(requests.size()), 16);
        live_request = true;
        return Links::ServiceRequestResult::Queued;
    }
    bool take(const uint8_t*, const uint8_t*, Links::ServiceResponse& out) override
    {
        if (!has_response) return false;
        out = std::move(response);
        has_response = live_request = false;
        return true;
    }
    bool pending(const uint8_t*, const uint8_t*) override { return live_request; }
    void cancel(const uint8_t*) override
    {
        ++cancelled;
        live_request = has_response = false;
    }
    bool decode(const uint8_t* data, size_t size, chat::MeshIncomingText& incoming) override
    {
        if (size != 64 || data[0] != 1) return false;
        incoming.has_reticulum_lxmf_hash = true;
        std::memset(incoming.reticulum_lxmf_hash, 77, 32);
        incoming.text = "accepted author";
        return true;
    }
    bool deliver(const chat::MeshIncomingText& incoming) override
    {
        delivered.push_back(incoming);
        return accept;
    }
    void reply(const std::vector<std::vector<uint8_t>>& items)
    {
        response = {};
        response.payload.resize(4096);
        size_t size = response.payload.size();
        assert(wire::encodePropagationMessageListPayload(items, response.payload.data(), &size));
        response.payload.resize(size);
        has_response = true;
    }
};
struct Fixture
{
    Store store;
    chat::MeshPeerDirectoryCore directory{store};
    Owner owner{directory};
    Transport transport;
    chat::reticulum::LxmfPropagationClientConfig config;
    std::vector<uint8_t> message = std::vector<uint8_t>(64, 1), invalid = std::vector<uint8_t>(64, 2);
    std::vector<uint8_t> transient = std::vector<uint8_t>(32), invalid_transient = std::vector<uint8_t>(32);
    Fixture()
    {
        assert(directory.begin().succeeded());
        trailmate::linux_app::LinuxReticulumIngress::PropagationAnnounce event;
        event.destination[0] = 9;
        event.delivery[0] = 8;
        event.identity[0] = 7;
        event.data.valid = event.data.node_active = true;
        event.received_ms = event.received_s = 100;
        owner.observe(event);
        chat::reticulum::fullHash(message.data(), message.size(), transient.data());
        chat::reticulum::fullHash(invalid.data(), invalid.size(), invalid_transient.data());
        config.sync_interval_s = 60;
    }
    void tick(uint32_t now = 100, bool allowed = true) { owner.tick(config, allowed, now, 100 + now / 1000, transport); }
    void download()
    {
        tick();
        assert(owner.syncStage() == Stage::Listing && transport.requests.size() == 1);
        assert(transport.requests[0].wants_is_nil && transport.requests[0].haves_is_nil);
        transport.reply({transient, invalid_transient});
        tick(101);
        assert(owner.syncStage() == Stage::Downloading && transport.requests.size() == 2);
        assert(transport.requests[1].wants.size() == 2 && transport.requests[1].haves_is_nil && transport.requests[1].has_transfer_limit);
        transport.reply({message, message, invalid});
        tick(102);
        assert(owner.syncStage() == Stage::AwaitingPersistence && transport.delivered.size() == 1 && transport.requests.size() == 2);
    }
};
int main()
{
    {
        Fixture f;
        f.download();
        f.tick(103);
        assert(f.transport.requests.size() == 2); // Resource transport is not durable acceptance.
        assert(f.owner.commit(f.transport.delivered[0], true, 100));
        f.tick(104);
        assert(f.owner.syncStage() == Stage::Acknowledging && f.transport.requests.size() == 3);
        const auto& ack = f.transport.requests.back();
        assert(ack.wants_is_nil && !ack.haves_is_nil && ack.haves.size() == 1 && ack.haves[0] == f.transient);
        f.transport.reply({});
        f.tick(105);
        assert(f.owner.syncStage() == Stage::Idle && f.transport.cancelled == 1);
        f.tick(106);
        assert(f.transport.requests.size() == 3);
    }
    {
        Fixture f;
        f.download();
        assert(f.owner.commit(f.transport.delivered[0], false, 100));
        f.tick(103);
        assert(f.owner.syncStage() == Stage::Idle && f.transport.requests.size() == 2);
    }
    {
        Fixture f;
        f.download();
        f.tick(120103);
        assert(f.owner.syncStage() == Stage::Idle && f.transport.requests.size() == 2);
        assert(!f.owner.commit(f.transport.delivered[0], true, 221));
    }
    {
        Fixture f;
        f.download();
        f.config.enabled = false;
        f.tick(103);
        assert(f.owner.syncStage() == Stage::Idle && f.transport.cancelled == 1);
        assert(!f.owner.commit(f.transport.delivered[0], true, 100));
    }
    {
        Fixture f;
        f.tick();
        f.transport.reply({f.invalid_transient});
        f.tick(101);
        f.transport.reply({f.message}); // Correct recipient is insufficient: it was not requested.
        f.tick(102);
        assert(f.owner.syncStage() == Stage::Idle && f.transport.delivered.empty() && f.transport.requests.size() == 2);
    }
}
