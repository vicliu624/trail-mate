#include "hostlink/hostlink_codec.h"
#include "hostlink/management_envelope.h"
#include "platform/linux/hostlink_transmit_queue.h"
#include "platform/ui/hostlink_runtime.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace runtime = platform::ui::hostlink;
namespace hl = hostlink;
using Bytes = std::vector<uint8_t>;
static Bytes frame(hl::FrameType type, uint16_t sequence, size_t size = 0)
{
    Bytes payload(size, 0x5a), encoded;
    assert(hl::encode_frame(static_cast<uint8_t>(type), sequence, payload.data(), payload.size(), encoded));
    return encoded;
}
static void send_all(int socket, const uint8_t* bytes, size_t size)
{
    while (size)
    {
        const auto sent = send(socket, bytes, size, MSG_NOSIGNAL);
        assert(sent > 0);
        bytes += sent;
        size -= sent;
    }
}
static hl::Frame receive(int socket, hl::Decoder& decoder)
{
    hl::Frame result;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (!decoder.next(result))
    {
        assert(std::chrono::steady_clock::now() < deadline);
        pollfd ready{socket, POLLIN, 0};
        assert(poll(&ready, 1, 3000) == 1);
        uint8_t bytes[2048];
        const auto count = recv(socket, bytes, sizeof(bytes), 0);
        assert(count > 0);
        decoder.push(bytes, count);
    }
    return result;
}
static uint32_t u32(const uint8_t* bytes)
{
    return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
}
static void check_partial_writes()
{
    using Queue = platform::linux_runtime::HostlinkTransmitQueue;
    Queue queue;
    const auto expected = frame(hl::FrameType::Hello, 9);
    assert(queue.push(uint8_t(hl::FrameType::Hello), 9, nullptr, 0));
    Bytes written;
    unsigned calls = 0;
    auto writer = [&](const uint8_t* bytes, size_t size) -> Queue::WriteResult
    {
        ++calls;
        if (calls == 2) return {Queue::WriteState::Retry};
        if (calls == 3) return {Queue::WriteState::WouldBlock};
        const auto count = std::min<size_t>(size, 3);
        written.insert(written.end(), bytes, bytes + count);
        return {Queue::WriteState::Progress, count};
    };
    auto result = queue.flush(writer);
    assert(result.connected && result.completed == 0 && !queue.empty() && written.size() == 3);
    result = queue.flush(writer);
    assert(result.connected && result.completed == 1 && queue.empty() && written == expected);
    for (unsigned slot = 0; slot < 8; ++slot) assert(queue.push(uint8_t(hl::FrameType::Ack), slot, nullptr, 0));
    assert(!queue.push(uint8_t(hl::FrameType::Ack), 8, nullptr, 0));
    result = queue.flush([](const uint8_t*, size_t)
                         { return Queue::WriteResult{Queue::WriteState::Failed}; });
    assert(!result.connected && result.completed == 0 && !queue.empty());
    queue.reset();
    assert(queue.empty());
    assert(queue.push(uint8_t(hl::FrameType::Ack), 1, nullptr, 0));
    result = queue.flush([](const uint8_t*, size_t size)
                         { return Queue::WriteResult{Queue::WriteState::Progress, size + 1}; });
    assert(!result.connected);
    queue.reset();
    const uint8_t sensitive[]{0x51, 0x72, 0x93, 0xb4};
    const uint8_t* retained = nullptr;
    size_t retained_size = 0;
    assert(queue.push(uint8_t(hl::FrameType::ManagementResponse), 2, sensitive, sizeof(sensitive)));
    result = queue.flush([&](const uint8_t* bytes, size_t size)
                         {
                             retained = bytes;
                             retained_size = size;
                             return Queue::WriteResult{Queue::WriteState::Progress, size}; });
    assert(result.completed == 1 && retained && retained_size);
    assert(std::all_of(retained, retained + retained_size, [](uint8_t value)
                       { return value == 0; }));
    assert(queue.push(uint8_t(hl::FrameType::ManagementResponse), 3, sensitive, sizeof(sensitive)));
    result = queue.flush([&](const uint8_t* bytes, size_t size)
                         {
                             retained = bytes;
                             retained_size = size;
                             return Queue::WriteResult{Queue::WriteState::WouldBlock}; });
    queue.reset();
    assert(std::all_of(retained, retained + retained_size, [](uint8_t value)
                       { return value == 0; }));
}
int main()
{
    check_partial_writes();
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-hostlink-protocol-" + std::to_string(getpid()));
    setenv("TRAIL_MATE_SD_ROOT", root.c_str(), 1);
    setenv("TRAIL_MATE_HOSTLINK_BIND", "127.0.0.1", 1);
    runtime::start();
    const auto endpoint_path = root / "hostlink/endpoint.txt";
    std::string endpoint;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (endpoint.empty() && std::chrono::steady_clock::now() < deadline)
    {
        std::ifstream input(endpoint_path);
        std::getline(input, endpoint);
        if (endpoint.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(endpoint.rfind("tcp://127.0.0.1:", 0) == 0);
    const int port = std::stoi(endpoint.substr(endpoint.rfind(':') + 1));
    const auto connect_client = [&]
    {
        const int socket = ::socket(AF_INET, SOCK_STREAM, 0);
        assert(socket >= 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        assert(inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1);
        assert(connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        return socket;
    };
    int socket = connect_client();
    hl::Decoder decoder;
    const Bytes junk{'n', 'o', 't', '-', 'a', '-', 'h', 'e', 'l', 'l', 'o'};
    send_all(socket, junk.data(), junk.size());
    pollfd ready{socket, POLLIN, 0};
    assert(poll(&ready, 1, 40) == 0); // No text greeting, echo, or false readiness.
    assert(runtime::get_status().state != runtime::LinkState::Ready);
    auto command = frame(hl::FrameType::CmdGetConfig, 10);
    send_all(socket, command.data(), command.size());
    auto reply = receive(socket, decoder);
    assert(reply.type == uint8_t(hl::FrameType::Ack) && reply.seq == 10 && reply.payload == Bytes{uint8_t(hl::ErrorCode::NotInMode)});
    auto hello = frame(hl::FrameType::Hello, 20);
    auto corrupt = hello;
    corrupt.back() ^= 1;
    send_all(socket, corrupt.data(), corrupt.size());
    // Fragment a valid Hello at every byte boundary.
    for (size_t offset = 0; offset < hello.size(); ++offset)
        send_all(socket, hello.data() + offset, 1);
    reply = receive(socket, decoder);
    assert(reply.type == uint8_t(hl::FrameType::HelloAck) && reply.seq == 20 && reply.payload.size() > 10);
    assert(reply.payload[0] == hl::kProtocolVersion && reply.payload[1] == 0);
    assert((unsigned(reply.payload[2]) | unsigned(reply.payload[3]) << 8) == hl::kMaxFrameLen);
    assert(u32(reply.payload.data() + 4) == (hl::CapStatus | hl::CapManagement | hl::CapAutoInterfaceIfac));
    auto status = receive(socket, decoder);
    assert(status.type == uint8_t(hl::FrameType::EvStatus));
    bool found_link = false;
    for (size_t offset = 0; offset < status.payload.size();)
    {
        assert(offset + 2 <= status.payload.size());
        const auto key = static_cast<hl::StatusKey>(status.payload[offset]);
        const size_t length = status.payload[offset + 1];
        assert(offset + 2 + length <= status.payload.size());
        assert(key == hl::StatusKey::Battery || key == hl::StatusKey::Charging || key == hl::StatusKey::LinkState || key == hl::StatusKey::LastError);
        if (key == hl::StatusKey::LinkState)
        {
            assert(length == 1 && status.payload[offset + 2] == uint8_t(runtime::LinkState::Ready));
            found_link = true;
        }
        offset += 2 + length;
    }
    assert(found_link && runtime::get_status().state == runtime::LinkState::Ready);
    // Coalesced commands and maximum-size frames retain sequence correlation.
    Bytes batch;
    for (uint16_t sequence = 30; sequence < 38; ++sequence)
    {
        const auto bytes = frame(hl::FrameType::CmdSetConfig, sequence, sequence == 30 ? hl::kMaxFrameLen : 0);
        batch.insert(batch.end(), bytes.begin(), bytes.end());
    }
    send_all(socket, batch.data(), batch.size());
    for (uint16_t sequence = 30; sequence < 38; ++sequence)
    {
        reply = receive(socket, decoder);
        assert(reply.type == uint8_t(hl::FrameType::Ack) && reply.seq == sequence && reply.payload == Bytes{uint8_t(hl::ErrorCode::Unsupported)});
    }
    uint8_t session[16]{1}, request_id[16]{2}, encoded[512]{}, body[1]{0x90};
    size_t encoded_size = 0;
    assert(hl::management::encodeEnvelope(session, request_id, 0, body, 1, encoded, sizeof(encoded), encoded_size));
    Bytes management_frame;
    assert(hl::encode_frame(uint8_t(hl::FrameType::ManagementRequest), 60, encoded, encoded_size, management_frame));
    send_all(socket, management_frame.data(), management_frame.size());
    reply = receive(socket, decoder);
    hl::management::EnvelopeView management_reply;
    assert(reply.type == uint8_t(hl::FrameType::ManagementResponse) && reply.seq == 60 &&
           hl::management::decodeEnvelope(reply.payload.data(), reply.payload.size(), management_reply));
    assert(management_reply.code == uint16_t(hl::management::Status::Ok) && !std::memcmp(management_reply.session, session, 16) &&
           !std::memcmp(management_reply.request, request_id, 16));
    reply = receive(socket, decoder);
    assert(reply.type == uint8_t(hl::FrameType::Ack) && reply.seq == 60 && reply.payload == Bytes{uint8_t(hl::ErrorCode::Ok)});
    status = receive(socket, decoder); // Periodic status is framed too.
    assert(status.type == uint8_t(hl::FrameType::EvStatus));
    // A partial frame cannot leak into the next connection's decoder.
    send_all(socket, hello.data(), 3);
    close(socket);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    socket = connect_client();
    decoder.reset();
    send_all(socket, hello.data() + 3, hello.size() - 3);
    send_all(socket, command.data(), command.size());
    reply = receive(socket, decoder);
    assert(reply.type == uint8_t(hl::FrameType::Ack) && reply.seq == 10 && reply.payload == Bytes{uint8_t(hl::ErrorCode::NotInMode)});
    send_all(socket, hello.data(), hello.size());
    assert(receive(socket, decoder).type == uint8_t(hl::FrameType::HelloAck));
    close(socket);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    socket = connect_client();
    send_all(socket, junk.data(), junk.size());
    pollfd expired{socket, POLLIN, 0};
    assert(poll(&expired, 1, 6500) == 1);
    uint8_t byte = 0;
    assert(recv(socket, &byte, 1, 0) == 0); // Garbage cannot complete/renew Hello.
    close(socket);
    runtime::stop();
    assert(!runtime::is_active() && !std::filesystem::exists(endpoint_path));
    std::filesystem::remove_all(root);
}
