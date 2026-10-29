#include "chat/infra/lxmf/lxmf_propagation_service_runtime.h"
#include "chat/linux_reticulum_links.h"
#include "chat/linux_reticulum_tcp.h"
#include "geocaching/protocol/cmp_reader.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>
#include <unistd.h>

namespace r = chat::reticulum;
namespace state = chat::lxmf::runtime;
using Links = trailmate::linux_app::LinuxReticulumLinks;
using Tcp = trailmate::linux_app::LinuxReticulumTcp;
using Identity = platform::linux_runtime::LinuxReticulumIdentity;
static uint32_t now()
{
    return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
static std::vector<uint8_t> unhex(const char* text)
{
    std::vector<uint8_t> bytes;
    for (size_t i = 0; text[i]; i += 2) bytes.push_back(uint8_t(std::stoul(std::string(text + i, 2), nullptr, 16)));
    return bytes;
}
struct Context
{
    Tcp tcp;
    Links& links;
    unsigned outcomes = 0, refused_proofs = 0;
    bool refuse_proof = true;
    static bool send(void* opaque, const uint8_t* bytes, size_t size)
    {
        auto& self = *static_cast<Context*>(opaque);
        r::ParsedPacket packet;
        assert(r::parsePacket(bytes, size, &packet));
        if (self.refuse_proof && packet.packet_type == r::PacketType::Proof && packet.context == uint8_t(r::PacketContext::ResourcePrf))
        {
            self.refuse_proof = false;
            ++self.refused_proofs;
            return false;
        }
        const auto status = self.tcp.snapshot();
        return status.count && status.endpoints[0].state == Tcp::State::Connected && self.tcp.send(status.endpoints[0].id, status.endpoints[0].generation, bytes, size);
    }
    void pump()
    {
        const auto time = now();
        tcp.tick(time);
        Tcp::Frame frame;
        while (tcp.receive(frame)) links.receive(frame.data.data(), frame.size, time, send, this);
        links.tick(time);
        links.processOutbound(time, send, this);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
};
int main(int argc, char** argv)
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-rpc-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    {
        Identity identity(root / "state.sqlite3");
        assert(identity.begin());
        Links links(identity);
        Context context{{}, links};
        links.setOutcomeSink([](void* opaque, chat::MessageId, chat::MessageStatus, chat::delivery::SendFailureKind)
                             { ++static_cast<Context*>(opaque)->outcomes; },
                             &context);
        uint8_t zero[16]{}, output_id[16]{}, path[16]{};
        state::propagationServicePathHash(state::PropagationServicePath::Get, path);
        Links::ServiceResponse response;
        response.payload.push_back(17);
        assert(links.requestService(zero, path, nullptr, 0, true, 1.0, now(), Context::send, &context, output_id) == Links::ServiceRequestResult::NotReady);
        assert(!links.takeServiceResponse(zero, zero, response) && response.payload.size() == 1);
        if (argc == 6 && !std::strcmp(argv[1], "--reference"))
        {
            const auto destination = unhex(argv[4]), key = unhex(argv[5]);
            assert(destination.size() == 16 && key.size() == 64);
            r::ReticulumNetworkConfig config;
            config.interface_count = 1;
            auto& endpoint = config.interfaces[0];
            std::strcpy(endpoint.id, "reference");
            endpoint.type = r::NetworkInterfaceType::TcpClient;
            endpoint.enabled = true;
            std::snprintf(endpoint.target_host, sizeof(endpoint.target_host), "%s", argv[2]);
            endpoint.target_port = uint16_t(std::stoul(argv[3]));
            assert(context.tcp.configure(config));
            const auto began = now();
            bool active = false;
            while (uint32_t(now() - began) < 10000)
            {
                context.pump();
                const auto status = context.tcp.snapshot();
                if (status.count && status.endpoints[0].state == Tcp::State::Connected)
                {
                    assert(links.open(destination.data(), key.data(), now(), Context::send, &context) == Links::OpenResult::Invalid);
                    const auto opened = links.openPropagation(destination.data(), key.data(), now(), Context::send, &context);
                    if (opened == Links::OpenResult::Active)
                    {
                        active = true;
                        break;
                    }
                    assert(opened == Links::OpenResult::Opening || opened == Links::OpenResult::Pending);
                }
            }
            assert(active);
            for (unsigned scenario = 0; scenario < 6; ++scenario)
            {
                std::vector<uint8_t> packed;
                if (scenario == 5)
                {
                    packed = {0xc5, 0x9c, 0x40}; // Incompressible request crosses the first hashmap segment.
                    uint32_t random = 0x12345678;
                    for (unsigned i = 0; i < 40000; ++i)
                    {
                        random ^= random << 13;
                        random ^= random >> 17;
                        random ^= random << 5;
                        packed.push_back(uint8_t(random));
                    }
                }
                else packed.push_back(uint8_t(scenario));
                const auto timestamp = double(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) / 1000000.0;
                const auto queued = links.requestService(destination.data(), path, packed.data(), packed.size(), false, timestamp, now(), Context::send, &context, output_id);
                assert(queued == Links::ServiceRequestResult::Queued && links.serviceRequestPending(destination.data(), output_id));
                const auto sent = now();
                bool received = false;
                while (uint32_t(now() - sent) < 15000)
                {
                    context.pump();
                    if (links.takeServiceResponse(destination.data(), output_id, response))
                    {
                        received = true;
                        break;
                    }
                }
                if (!received) std::fprintf(stderr, "RPC scenario %u timed out; rejected=%llu active=%zu\n", scenario, static_cast<unsigned long long>(links.statistics().rejected), links.statistics().active);
                assert(received && !links.serviceRequestPending(destination.data(), output_id));
                if (scenario == 1) assert(response.data_is_nil && response.payload.empty());
                else
                {
                    assert(!response.data_is_nil);
                    geocaching::protocol::CmpReader reader({response.payload.data(), response.payload.size()});
                    if (scenario == 2 || scenario == 3)
                    {
                        geocaching::ByteView binary;
                        const size_t expected = scenario == 2 ? 6000 : 40000;
                        assert(reader.binary(binary, expected) && binary.size == expected && reader.finished());
                        if (scenario == 2) assert(std::all_of(binary.data, binary.data + binary.size, [](auto byte)
                                                              { return byte == 42; }));
                    }
                    else
                    {
                        uint64_t value = 0;
                        assert(reader.unsignedInteger(value) && value == (scenario == 5 ? 40000U : scenario == 4 ? 19U
                                                                                                                 : 17U) &&
                               reader.finished());
                    }
                }
                const auto consumed = now();
                while (uint32_t(now() - consumed) < (scenario == 2 ? 1500U : 50U)) context.pump();
            }
            std::array<std::array<uint8_t, 16>, 4> queued_ids{};
            const uint8_t request_data = 0;
            for (size_t i = 0; i < queued_ids.size(); ++i)
                assert(links.requestService(destination.data(), path, &request_data, 1, false, 1700000000.0 + double(i), now(), Context::send, &context, queued_ids[i].data()) == Links::ServiceRequestResult::Queued);
            assert(links.requestService(destination.data(), path, &request_data, 1, false, 1700000010.0, now(), Context::send, &context, output_id) == Links::ServiceRequestResult::Busy);
            const auto bounded = now();
            std::array<bool, 4> completed{};
            while (uint32_t(now() - bounded) < 10000 && !std::all_of(completed.begin(), completed.end(), [](bool value)
                                                                     { return value; }))
            {
                context.pump();
                for (size_t i = 0; i < queued_ids.size(); ++i)
                    if (!completed[i] && links.takeServiceResponse(destination.data(), queued_ids[i].data(), response))
                    {
                        assert(!response.data_is_nil && response.payload.size() == 1 && response.payload[0] == 17);
                        completed[i] = true;
                    }
            }
            assert(std::all_of(completed.begin(), completed.end(), [](bool value)
                               { return value; }));
            std::vector<uint8_t> upload{'U', 'P', 'L', 'O', 'A', 'D'};
            for (unsigned i = 0; i < 1250; ++i)
            {
                const auto material = std::to_string(i);
                uint8_t hash[32];
                r::fullHash(reinterpret_cast<const uint8_t*>(material.data()), material.size(), hash);
                upload.insert(upload.end(), hash, hash + sizeof(hash));
            }
            uint8_t upload_hash[32];
            assert(links.queuePropagationUpload(destination.data(), upload.data(), upload.size(), now(), Context::send, &context, upload_hash) == Links::ServiceRequestResult::Queued);
            assert(links.hasPropagationUpload(upload_hash));
            Links::UploadResult upload_result = Links::UploadResult::Failed;
            assert(!links.takePropagationUploadResult(upload_hash, upload_result));
            const auto uploading = now();
            bool uploaded = false;
            while (uint32_t(now() - uploading) < 15000)
            {
                context.pump();
                if (links.takePropagationUploadResult(upload_hash, upload_result))
                {
                    uploaded = true;
                    break;
                }
            }
            assert(uploaded && upload_result == Links::UploadResult::TransportAccepted && !links.hasPropagationUpload(upload_hash));
            assert(!links.takePropagationUploadResult(upload_hash, upload_result));
            assert(links.queuePropagationUpload(destination.data(), upload.data(), upload.size(), now(), Context::send, &context, upload_hash) == Links::ServiceRequestResult::Queued);
            links.closePropagation(destination.data(), now(), Context::send, &context);
            assert(links.takePropagationUploadResult(upload_hash, upload_result) && upload_result == Links::UploadResult::Failed);
            assert(context.refused_proofs == 1 && context.outcomes == 0 && links.statistics().rejected > 0);
            links.reset();
            assert(!links.serviceRequestPending(destination.data(), output_id));
            std::puts("Native propagation RPC and uploads: authenticated requests, large resources, proof retry, bounded concurrency, transport-only upload receipts and cancellation passed");
        }
        else
        {
            assert(argc == 1);
            assert(links.openPropagation(zero, identity.publicKey().data(), now(), Context::send, &context) == Links::OpenResult::Invalid);
            const uint8_t malformed = 0xc0;
            assert(links.requestService(zero, path, &malformed, 1, true, 1, now(), Context::send, &context, output_id) == Links::ServiceRequestResult::Invalid);
            assert(context.outcomes == 0);
            std::puts("Native propagation RPC rejects invalid/not-ready requests without changing response state");
        }
    }
    std::filesystem::remove_all(root);
}
