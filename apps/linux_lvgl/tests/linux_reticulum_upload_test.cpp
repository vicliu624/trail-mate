#include "chat/linux_reticulum_upload.h"
#include <cassert>
#include <filesystem>
#include <unistd.h>

using Identity = platform::linux_runtime::LinuxReticulumIdentity;
using Links = trailmate::linux_app::LinuxReticulumLinks;
using Upload = trailmate::linux_app::LinuxReticulumUpload;
struct Transport final : Upload::Transport
{
    unsigned opens = 0, uploads = 0, closes = 0, discoveries = 0;
    bool completed = false, accepted = true, busy = false, pending = false;
    chat::lxmf::DecodedPropagationBatch batch;
    Links::OpenResult open(const chat::lxmf::runtime::PropagationPeerState&) override
    {
        ++opens;
        return Links::OpenResult::Active;
    }
    void cancel(const uint8_t*) override
    {
        ++closes;
        pending = false;
    }
    void discover(const uint8_t*) override { ++discoveries; }
    Links::ServiceRequestResult upload(const uint8_t*, const uint8_t* data, size_t size, uint8_t* hash) override
    {
        if (busy) return Links::ServiceRequestResult::Busy;
        assert(chat::lxmf::decodePropagationBatch(data, size, &batch));
        assert(batch.messages.size() == 1 && batch.messages[0].size() > 32);
        std::memset(hash, 9, 32);
        ++uploads;
        pending = true;
        return Links::ServiceRequestResult::Queued;
    }
    bool uploadResult(const uint8_t*, Links::UploadResult& result) override
    {
        if (!completed) return false;
        result = accepted ? Links::UploadResult::TransportAccepted : Links::UploadResult::Failed;
        pending = false;
        return true;
    }
    bool hasUpload(const uint8_t*) override { return pending; }
};
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-upload-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    {
        Identity author(root / "author.db"), recipient(root / "recipient.db");
        assert(author.begin() && recipient.begin());
        Links encoder(author), decoder(recipient);
        decoder.setIncomingHooks(
            nullptr, [](void* context, const uint8_t*, uint8_t* key, bool* ignored)
            { const auto& identity = *static_cast<Identity*>(context); *ignored = false; std::memcpy(key, identity.publicKey().data(), 64); return true; },
            &author);
        const auto start = [&](Upload& owner)
        { return owner.start(encoder, recipient.destinationHash().data(), recipient.publicKey().data(), "outgoing task", 7, 123, 100); };
        chat::reticulum::LxmfPropagationClientConfig config;
        chat::lxmf::runtime::PropagationPeerState node;
        node.propagation_hash[0] = 8;
        node.node_active = true;
        node.transfer_limit_kb = 1024;
        {
            Upload owner;
            Transport transport;
            assert(start(owner) == Links::QueueResult::Queued && start(owner) == Links::QueueResult::Busy);
            transport.busy = true;
            for (unsigned i = 0; i < 300 && !transport.opens; ++i) owner.tick(config, true, &node, 101 + i, 123, transport);
            assert(owner.active() && transport.opens && !transport.uploads && !owner.takeResult());
            transport.busy = false;
            owner.tick(config, true, &node, 500, 123, transport);
            assert(owner.active() && transport.uploads == 1 && !owner.takeResult());
            const auto& stamped = transport.batch.messages[0];
            chat::MeshIncomingText message;
            uint8_t hash[32];
            assert(decoder.decodePropagatedText(stamped.data(), stamped.size() - 32, nullptr, message, hash) && message.text == "outgoing task");
            transport.completed = true;
            owner.tick(config, true, &node, 501, 123, transport);
            const auto result = owner.takeResult();
            assert(result && result->message == 7 && result->uploaded && !owner.active() && transport.closes == 1);
            assert(!owner.takeResult());
        }
        {
            Upload owner;
            Transport transport;
            assert(start(owner) == Links::QueueResult::Queued);
            config.automatic_node = false;
            config.node_hash[0] = 8;
            owner.tick(config, true, nullptr, 101, 123, transport);
            assert(transport.discoveries == 1 && !transport.uploads);
            owner.cancel(transport);
            assert(owner.takeResult()->uploaded == false && !owner.active());
        }
        {
            Upload owner;
            Transport transport;
            assert(start(owner) == Links::QueueResult::Queued);
            owner.tick(config, false, &node, 101, 123, transport);
            assert(!owner.takeResult()->uploaded && !transport.uploads);
        }
        {
            Upload owner;
            Transport transport;
            assert(start(owner) == Links::QueueResult::Queued);
            owner.tick(config, true, &node, 101, 123, transport);
            node.stamp_cost = 1;
            owner.tick(config, true, &node, 102, 123, transport);
            assert(!owner.takeResult()->uploaded && !transport.uploads && transport.closes == 1);
        }
    }
    std::filesystem::remove_all(root);
}
