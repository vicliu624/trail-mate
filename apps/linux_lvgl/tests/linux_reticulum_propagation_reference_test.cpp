#include "chat/infra/mesh_peer_directory_core.h"
#include "chat/linux_reticulum_network.h"
#include "chat/linux_reticulum_propagation.h"
#include "chat/linux_reticulum_upload.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <unistd.h>

namespace r = chat::reticulum;
using Identity = platform::linux_runtime::LinuxReticulumIdentity;
using Links = trailmate::linux_app::LinuxReticulumLinks;
using Network = trailmate::linux_app::LinuxReticulumNetwork;
using Owner = trailmate::linux_app::LinuxReticulumPropagation;
uint32_t milliseconds()
{
    return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
std::vector<uint8_t> unhex(const char* text)
{
    std::vector<uint8_t> bytes;
    while (*text)
    {
        unsigned value;
        assert(std::sscanf(text, "%2x", &value) == 1);
        bytes.push_back(uint8_t(value));
        text += 2;
    }
    return bytes;
}
struct Store final : chat::IMeshPeerDirectoryBlobStore
{
    chat::MeshPeerDirectoryBlobLoadResult loadBlob(std::vector<uint8_t>&) override { return chat::MeshPeerDirectoryBlobLoadResult::Missing; }
    bool saveBlob(const uint8_t*, size_t) override { return true; }
    void clearBlob() override {}
};
struct Harness final : Owner::Transport, trailmate::linux_app::LinuxReticulumUpload::Transport
{
    chat::IMeshPeerDirectory& directory;
    Links decoder;
    Owner owner;
    Network network;
    std::vector<chat::MeshIncomingText> deliveries;
    uint32_t delivered_ms = 0;
    bool committed = false;
    Harness(Identity& identity, chat::IMeshPeerDirectory& directory)
        : directory(directory), decoder(identity), owner(directory), network(identity, directory, hooks(this))
    {
        decoder.setIncomingHooks(
            nullptr, [](void* context, const uint8_t* destination, uint8_t* key, bool* ignored)
            {
                                     auto& self = *static_cast<Harness*>(context);
                                     chat::MeshPeerRecord record;
                                     const auto found = self.directory.find(chat::makeMeshPeerReticulumIdentity(chat::makeReticulumDestinationIdentity(destination)), record);
                                     *ignored = found.succeeded() ? record.flags.ignored : found.code != chat::MeshPeerDirectoryStatusCode::NotFound;
                                     if (!found.succeeded() || !record.reticulum.has_public_keys) return false;
                                     std::memcpy(key, record.reticulum.enc_pub, 32);
                                     std::memcpy(key + 32, record.reticulum.sig_pub, 32);
                                     return true; },
            this);
    }
    static Network::Hooks hooks(Harness* self)
    {
        Network::Hooks hooks;
        hooks.context = self;
        hooks.propagation = [](void* context, const trailmate::linux_app::LinuxReticulumIngress::PropagationAnnounce& event)
        { static_cast<Harness*>(context)->owner.observe(event); };
        return hooks;
    }
    Links::OpenResult open(const chat::lxmf::runtime::PropagationPeerState& peer) override
    {
        uint8_t key[64];
        std::memcpy(key, peer.enc_pub, 32);
        std::memcpy(key + 32, peer.sig_pub, 32);
        return network.openPropagation(peer.propagation_hash, key);
    }
    Links::ServiceRequestResult request(const uint8_t* destination, const uint8_t* path, const uint8_t* data, size_t size, double timestamp, uint8_t* id) override
    {
        return network.requestPropagation(destination, path, data, size, false, timestamp, id);
    }
    bool take(const uint8_t* destination, const uint8_t* id, Links::ServiceResponse& response) override
    {
        return network.takePropagationResponse(destination, id, response);
    }
    bool pending(const uint8_t* destination, const uint8_t* id) override { return network.propagationRequestPending(destination, id); }
    void cancel(const uint8_t* destination) override { network.closePropagation(destination); }
    void discover(const uint8_t* destination) override { (void)network.requestPath(destination); }
    bool decode(const uint8_t* bytes, size_t size, chat::MeshIncomingText& incoming) override
    {
        uint8_t hash[32], missing[16]{};
        const bool decoded = decoder.decodePropagatedText(bytes, size, nullptr, incoming, hash, missing);
        if (missing[0]) discover(missing);
        return decoded;
    }
    bool deliver(const chat::MeshIncomingText& incoming) override
    {
        deliveries.push_back(incoming);
        delivered_ms = milliseconds();
        return true;
    }
    Links::ServiceRequestResult upload(const uint8_t* destination, const uint8_t* data, size_t size, uint8_t* hash) override
    {
        return network.queuePropagationUpload(destination, data, size, hash);
    }
    bool uploadResult(const uint8_t* hash, Links::UploadResult& result) override { return network.takePropagationUploadResult(hash, result); }
    bool hasUpload(const uint8_t* hash) override { return network.hasPropagationUpload(hash); }
};
int main(int argc, char** argv)
{
    assert(argc == 6);
    const auto pn = unhex(argv[3]), author = unhex(argv[4]);
    assert(pn.size() == 16 && author.size() == 64);
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-lxmf-reference-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    {
        Identity identity(root / "state.sqlite3");
        assert(identity.begin());
        Store store;
        chat::MeshPeerDirectoryCore directory(store);
        assert(directory.begin().succeeded());
        Harness test(identity, directory);
        r::ReticulumNetworkConfig config;
        config.interface_count = 1;
        auto& endpoint = config.interfaces[0];
        endpoint.enabled = true;
        endpoint.type = r::NetworkInterfaceType::TcpClient;
        std::strcpy(endpoint.id, "reference");
        std::snprintf(endpoint.target_host, sizeof(endpoint.target_host), "%s", argv[1]);
        endpoint.target_port = uint16_t(std::stoul(argv[2]));
        config.propagation.automatic_node = false;
        config.propagation.sync_interval_s = 60;
        std::memcpy(config.propagation.node_hash, pn.data(), 16);
        assert(test.network.configure(config, false, "Native mailbox test"));
        std::printf("RECIPIENT ");
        for (auto byte : identity.publicKey()) std::printf("%02x", byte);
        std::printf("\n");
        std::fflush(stdout);
        const auto started = milliseconds();
        bool complete = false;
        while (uint32_t(milliseconds() - started) < 30000)
        {
            const auto now = milliseconds();
            test.network.tick(now, 1000);
            test.owner.tick(config.propagation, true, now, 1000, test);
            if (!test.deliveries.empty() && !test.committed && uint32_t(now - test.delivered_ms) >= 500)
            {
                assert(test.owner.syncStage() == chat::lxmf::runtime::PropagationSyncStage::AwaitingPersistence);
                assert(test.owner.commit(test.deliveries[0], true, 1000));
                test.committed = true;
            }
            if (test.committed && test.owner.syncStage() == chat::lxmf::runtime::PropagationSyncStage::Idle)
            {
                complete = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        assert(complete && test.deliveries.size() == 1);
        assert(test.deliveries[0].text.size() == 40000 && test.deliveries[0].text.substr(0, 18) == "Official LXMF mail");
        uint8_t author_identity[16], name[10], destination[16];
        r::computeIdentityHash(author.data(), author_identity);
        r::computeNameHash("lxmf", "delivery", name);
        r::computeDestinationHash(name, author_identity, destination);
        assert(!std::memcmp(test.deliveries[0].reticulum_identity.destination_hash, destination, 16));
        assert(!test.deliveries[0].source_unverified && test.deliveries[0].encrypted);
        const auto outbound_key = unhex(argv[5]);
        assert(outbound_key.size() == 64);
        uint8_t outbound_identity[16], outbound_destination[16];
        r::computeIdentityHash(outbound_key.data(), outbound_identity);
        r::computeDestinationHash(name, outbound_identity, outbound_destination);
        trailmate::linux_app::LinuxReticulumUpload upload;
        std::string outgoing = "Native propagation upload";
        outgoing.resize(40000, 'x');
        assert(upload.start(test.decoder, outbound_destination, outbound_key.data(), outgoing, 7, 1000, milliseconds()) == Links::QueueResult::Queued);
        const auto uploading = milliseconds();
        std::optional<trailmate::linux_app::LinuxReticulumUpload::Result> result;
        while (uint32_t(milliseconds() - uploading) < 30000 && !result)
        {
            const auto now = milliseconds();
            test.network.tick(now, 1000);
            const auto utc = uint32_t(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
            upload.tick(config.propagation, true, test.owner.select(config.propagation, true, now), now, utc, test);
            result = upload.takeResult();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        assert(result && result->message == 7 && result->uploaded);
        std::puts("Native LXMF mailbox: signed node discovery, authenticated RPC, encrypted resource download, original-author verification and durable acknowledgement passed");
        std::puts("Native LXMF sender: recipient encryption, incremental node-cost stamp and resource upload receipt passed");
    }
    std::filesystem::remove_all(root);
}
