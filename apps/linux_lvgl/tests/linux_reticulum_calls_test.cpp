#include "call_pcm_fixture.h"
#include "chat/infra/mesh_peer_directory_core.h"
#include "chat/linux_reticulum_calls.h"
#include "chat/linux_reticulum_tcp.h"
#include "local_tcp_relay_fixture.h"
#include "platform/audio/audio_focus.h"
#include "sys/clock.h"
#include <arpa/inet.h>
#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace call = platform::ui::reticulum_call;
namespace native = platform::linux_runtime;
using Identity = native::LinuxReticulumIdentity;
using Links = trailmate::linux_app::LinuxReticulumLinks;
using Calls = trailmate::linux_app::LinuxReticulumCalls;
using Tcp = trailmate::linux_app::LinuxReticulumTcp;
namespace fs = std::filesystem;
enum class Scenario
{
    Conversation,
    Rejected,
    Ignored,
    MediaFailure,
    TransportLoss,
    ActiveReset,
    Busy,
    QueueOverflow,
    RuntimeReplacement
};
struct MemoryStore : chat::IMeshPeerDirectoryBlobStore
{
    chat::MeshPeerDirectoryBlobLoadResult loadBlob(std::vector<uint8_t>&) override { return chat::MeshPeerDirectoryBlobLoadResult::Missing; }
    bool saveBlob(const uint8_t*, size_t) override { return true; }
    void clearBlob() override {}
};
struct Wire
{
    int fd;
    Tcp* tcp = nullptr;
    uint64_t epoch = 0;
    static bool send(void* context, const uint8_t* data, size_t size)
    {
        auto& self = *static_cast<Wire*>(context);
        return self.tcp ? self.tcp->send("fixture", self.epoch, data, size) : ::send(self.fd, data, size, MSG_DONTWAIT | MSG_NOSIGNAL) == ssize_t(size);
    }
};
int endpoint(const fs::path& root, bool caller, int fd, Scenario scenario, uint16_t tcp_port = 0)
{
    const auto own = root / (caller ? "alice" : "bob");
    const auto other = root / (caller ? "bob" : "alice");
    setenv("TRAIL_MATE_SETTINGS_ROOT", own.c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", own.c_str(), 1);
    Identity identity(own / "identity.sqlite"), peer(other / "identity.sqlite");
    assert(identity.begin() && peer.begin());
    MemoryStore store;
    chat::MeshPeerDirectoryCore directory(store);
    assert(directory.beginEmpty().succeeded());
    chat::MeshPeerRecord record;
    record.valid = true;
    record.identity = chat::makeMeshPeerReticulumIdentity(chat::makeReticulumPeerIdentity(peer.destinationHash().data(), peer.identityHash().data()));
    record.reticulum.identity = record.identity.reticulum;
    record.reticulum.has_public_keys = true;
    record.reticulum.delivery = true;
    std::memcpy(record.reticulum.enc_pub, peer.publicKey().data(), 32);
    std::memcpy(record.reticulum.sig_pub, peer.publicKey().data() + 32, 32);
    std::snprintf(record.display_name, sizeof(record.display_name), "%s", caller ? "Bob fixture" : "Alice fixture");
    assert(directory.record(record).succeeded());
    if (!caller && scenario == Scenario::Ignored)
    {
        chat::MeshPeerUserFlags flags;
        flags.ignored = true;
        assert(directory.setUserFlags(record.identity, flags).succeeded());
    }
    auto pcm = std::make_shared<call_audio_test::State>();
    if (!caller && scenario == Scenario::MediaFailure) pcm->fail_open = true;
    native::NativeCallAudioBinding media(call_audio_test::options(pcm));
    assert(media.snapshot().installed);
    std::unique_ptr<Tcp> tcp;
    Links links(identity);
    Calls calls(links, directory);
    Wire wire{fd};
    if (tcp_port)
    {
        tcp = std::make_unique<Tcp>();
        chat::reticulum::ReticulumNetworkConfig config;
        config.interface_count = 1;
        auto& network = config.interfaces[0];
        network.type = chat::reticulum::NetworkInterfaceType::TcpClient;
        network.enabled = true;
        network.target_port = tcp_port;
        std::snprintf(network.id, sizeof(network.id), "fixture");
        std::snprintf(network.target_host, sizeof(network.target_host), "127.0.0.1");
        assert(tcp->configure(config));
        const auto connected = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do
        {
            tcp->tick(sys::millis_now());
            const auto snapshot = tcp->snapshot();
            if (snapshot.endpoints[0].state == Tcp::State::Connected) wire.epoch = snapshot.endpoints[0].generation;
            assert(std::chrono::steady_clock::now() < connected);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (!wire.epoch);
        wire.tcp = tcp.get();
    }
    assert(!calls.dial(record.identity.reticulum, {}, sys::millis_now(), Wire::send, &wire).ok);
    calls.tick(sys::millis_now(), true, Wire::send, &wire);
    call::Peer foreign;
    foreign.link_id[0] = 201;
    foreign.destination_hash[0] = 202;
    if (!caller && scenario == Scenario::Busy) assert(call::begin_outgoing(foreign));
    bool started = false;
    if (caller)
    {
        assert(!calls.dial({}, {}, sys::millis_now(), Wire::send, &wire).ok);
        auto bad_identity = record.identity.reticulum;
        bad_identity.identity_hash[0] ^= 1;
        assert(!calls.dial(bad_identity, {}, sys::millis_now(), Wire::send, &wire).ok);
        assert(calls.dial(record.identity.reticulum, {}, sys::millis_now(), Wire::send, &wire).ok);
        assert(calls.dial(record.identity.reticulum, {}, sys::millis_now(), Wire::send, &wire).failure == chat::MeshOperationFailure::Busy);
        started = true;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    bool active = false, half_seen = false, reset = false, transport_ready = true, overflow_injected = false;
    unsigned control_stage = 0;
    uint32_t control_at = 0;
    uint64_t muted_sent = 0;
    for (;;)
    {
        if (tcp)
        {
            tcp->tick(sys::millis_now());
            Tcp::Frame packet;
            while (tcp->receive(packet))
            {
                assert(packet.connection_generation == wire.epoch);
                assert(links.receive(packet.data.data(), packet.size, sys::millis_now(), Wire::send, &wire));
            }
        }
        uint8_t bytes[chat::reticulum::kReticulumMtu];
        for (unsigned drained = 0; !tcp && drained < 32; ++drained)
        {
            const auto size = recv(fd, bytes, sizeof(bytes), MSG_DONTWAIT);
            if (size < 0)
            {
                assert(errno == EAGAIN || errno == EWOULDBLOCK);
                break;
            }
            assert(size > 0);
            assert(links.receive(bytes, size_t(size), sys::millis_now(), Wire::send, &wire));
            chat::reticulum::ParsedPacket parsed;
            const bool application_data = chat::reticulum::parsePacket(bytes, size_t(size), &parsed) &&
                                          parsed.packet_type == chat::reticulum::PacketType::Data && parsed.context == 0;
            if (!caller && scenario == Scenario::QueueOverflow && active && !overflow_injected && application_data)
            {
                // Simulate a burst delivered in one worker poll, before the
                // protocol owner gets another opportunity to drain its queue.
                for (unsigned index = 0; index < 20; ++index)
                    assert(links.receive(bytes, size_t(size), sys::millis_now(), Wire::send, &wire));
                overflow_injected = true;
            }
        }
        const auto now = sys::millis_now();
        links.tick(now);
        calls.tick(now, transport_ready && (!tcp || tcp->online("fixture", wire.epoch)), Wire::send, &wire);
        call::service_ui_runtime();
        const auto snapshot = call::snapshot();
        const auto stats = calls.statistics();
        if (snapshot.state == call::State::Incoming)
        {
            started = true;
            assert(!std::strcmp(snapshot.peer_name, "Alice fixture"));
            if (!snapshot.accepted)
            {
                if (scenario == Scenario::Rejected) call::reject();
                else assert(call::accept());
            }
        }
        if (snapshot.state == call::State::Active)
        {
            active = true;
            if (snapshot.duplex_mode == call::DuplexMode::Half) half_seen = true;
            if (!caller && scenario == Scenario::RuntimeReplacement && !reset && stats.sent_audio > 1 && stats.received_audio > 1)
            {
                call::notify_link_closed(snapshot.link_id);
                const auto stopped = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                while (pcm->live || platform::audio::music_blocked())
                {
                    assert(std::chrono::steady_clock::now() < stopped);
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                assert(call::begin_outgoing(foreign));
                calls.tick(sys::millis_now(), true, Wire::send, &wire);
                assert(calls.statistics().sessions == 0 && call::state() == call::State::Outgoing &&
                       !std::memcmp(call::snapshot().link_id, foreign.link_id, 16) && "Replacing the runtime must close only the old transport session");
                reset = true;
            }
            if (caller && stats.sent_audio > 2 && stats.received_audio > 2 && control_stage == 0)
            {
                if (scenario == Scenario::TransportLoss) transport_ready = false;
                else if (scenario == Scenario::ActiveReset)
                {
                    calls.reset(now, Wire::send, &wire);
                    reset = true;
                }
                else
                {
                    call::set_microphone_muted(true);
                    call::set_speaker_muted(true);
                    muted_sent = stats.sent_audio;
                    control_at = now;
                    control_stage = 1;
                }
            }
            if (caller && control_stage == 1)
            {
                assert(stats.sent_audio == muted_sent);
                if (uint32_t(now - control_at) > 300)
                {
                    call::set_microphone_muted(false);
                    call::set_speaker_muted(false);
                    assert(call::set_duplex_mode(call::DuplexMode::Half));
                    call::set_ptt_pressed(true);
                    control_at = now;
                    control_stage = 2;
                }
            }
            else if (caller && control_stage == 2 && uint32_t(now - control_at) > 300)
            {
                call::set_ptt_pressed(false);
                assert(call::set_duplex_mode(call::DuplexMode::Full));
                control_at = now;
                control_stage = 3;
            }
            else if (caller && control_stage == 3 && uint32_t(now - control_at) > 300) call::hangup();
        }
        if (stats.closed > 0 && stats.sessions == 0) break;
        assert(std::chrono::steady_clock::now() < deadline && "Call orchestration did not reach a terminal state");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    calls.reset(sys::millis_now(), Wire::send, &wire);
    if (!caller && (scenario == Scenario::Busy || scenario == Scenario::RuntimeReplacement))
    {
        assert(call::state() == call::State::Outgoing && !std::memcmp(call::snapshot().link_id, foreign.link_id, 16));
        call::hangup();
        calls.tick(sys::millis_now(), true, Wire::send, &wire);
        uint8_t pending[16];
        assert(call::consume_hangup_request(pending) && !std::memcmp(pending, foreign.link_id, 16));
        call::notify_link_closed(pending);
    }
    media.shutdown();
    call::service_ui_runtime();
    std::fprintf(stderr, "scenario=%u caller=%d active=%d started=%d tx=%llu rx=%llu written=%u closed=%llu\n",
                 unsigned(scenario), caller, active, started,
                 static_cast<unsigned long long>(calls.statistics().sent_audio),
                 static_cast<unsigned long long>(calls.statistics().received_audio), pcm->written.load(),
                 static_cast<unsigned long long>(calls.statistics().closed));
    assert(!pcm->live && !platform::audio::music_blocked() && !call::modal_active());
    if (scenario == Scenario::Conversation || scenario == Scenario::TransportLoss || scenario == Scenario::ActiveReset || scenario == Scenario::RuntimeReplacement)
    {
        assert(active && started && calls.statistics().received_audio > 0 && calls.statistics().sent_audio > 0 && pcm->written > 0);
        if (scenario == Scenario::Conversation) assert(half_seen && calls.statistics().received_audio > 2 && calls.statistics().sent_audio > 2);
        if (caller && scenario == Scenario::ActiveReset) assert(reset);
        if (!caller && scenario == Scenario::RuntimeReplacement) assert(reset);
    }
    else
    {
        if (scenario == Scenario::QueueOverflow)
        {
            assert(active);
            if (!caller) assert(overflow_injected && calls.statistics().dropped > 0);
        }
        else assert(!active);
        if (scenario == Scenario::Ignored) assert(caller || (!started && calls.statistics().rejected > 0));
        if (scenario == Scenario::Rejected || scenario == Scenario::Ignored || scenario == Scenario::Busy) assert(pcm->opened == 0);
    }
    if (fd >= 0) ::close(fd);
    return 0;
}
bool tcpConversation(const fs::path& root)
{
    return tcp_test::pair([&](bool caller, uint16_t port)
                          { return endpoint(root, caller, -1, Scenario::Conversation, port); });
}
int main()
{
    const auto root = fs::temp_directory_path() / ("trailmate-call-orchestration-" + std::to_string(getpid()));
    fs::create_directories(root / "alice");
    fs::create_directories(root / "bob");
    {
        Identity alice(root / "alice/identity.sqlite"), bob(root / "bob/identity.sqlite");
        assert(alice.begin() && bob.begin());
    }
    bool passed = true;
    for (const auto scenario : {Scenario::Conversation, Scenario::Rejected, Scenario::Ignored, Scenario::MediaFailure, Scenario::TransportLoss, Scenario::ActiveReset, Scenario::Busy, Scenario::QueueOverflow, Scenario::RuntimeReplacement})
    {
        int sockets[2];
        assert(socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets) == 0);
        pid_t children[2];
        for (unsigned side = 0; side < 2; ++side)
        {
            children[side] = fork();
            assert(children[side] >= 0);
            if (children[side] == 0)
            {
                ::close(sockets[1 - side]);
                std::exit(endpoint(root, side == 0, sockets[side], scenario));
            }
        }
        ::close(sockets[0]);
        ::close(sockets[1]);
        for (const auto child : children)
        {
            int status;
            assert(waitpid(child, &status, 0) == child);
            passed = passed && WIFEXITED(status) && WEXITSTATUS(status) == 0;
        }
        if (!passed) break;
    }
    if (passed) passed = tcpConversation(root);
    fs::remove_all(root);
    if (!passed) return 1;
    std::puts("Production LXST orchestration: bidirectional Codec2, mute/PTT, rejection, ignored peers, media failure, transport loss and active reset passed");
    std::puts("Native TCP + HDLC + encrypted LXST + Codec2 conversation passed through a local byte relay");
}
