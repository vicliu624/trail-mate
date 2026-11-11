#include "platform/linux/team_espnow_pairing.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/random.h>
#include <thread>
#include <unistd.h>

// Real shared coordinator and native transport, with isolated test material.
// This tool never edits the application's identity, settings, or Team store.
class Runtime final : public team::ITeamRuntime
{
  public:
    uint32_t nowMillis() override
    {
        return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now().time_since_epoch())
                                         .count());
    }
    uint32_t nowUnixSeconds() override
    {
        return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count());
    }
    void fillRandomBytes(uint8_t* out, size_t len) override
    {
        while (len)
        {
            const auto size = ::getrandom(out, len, 0);
            if (size < 0 && errno == EINTR) continue;
            if (size <= 0) throw std::runtime_error("getrandom failed");
            out += size;
            len -= static_cast<size_t>(size);
        }
    }
};

class Events final : public team::ITeamPairingEventSink
{
  public:
    explicit Events(const char* path) : path_(path) {}
    void onTeamPairingStateChanged(const team::TeamPairingEvent& event) override
    {
        std::printf("STATE role=%u state=%u peer=%08x key_id=%u\n",
                    static_cast<unsigned>(event.role), static_cast<unsigned>(event.state), event.peer_id, event.key_id);
        std::fflush(stdout);
        if (event.peer_id) accepted_peer = true;
    }
    void onTeamPairingKeyDist(const team::TeamKeyDistEvent& event) override
    {
        if (event.msg.channel_psk_len != 16) throw std::runtime_error("Unexpected PSK length");
        save(event.msg.team_id, event.msg.key_id, event.msg.channel_psk.data());
        received_key = true;
        std::puts("KEY_RECEIVED_AND_SAVED");
        std::fflush(stdout);
    }
    void save(const team::TeamId& id, uint32_t key_id, const uint8_t* psk)
    {
        std::array<uint8_t, 28> material{};
        std::copy(id.begin(), id.end(), material.begin());
        for (unsigned i = 0; i < 4; ++i) material[8 + i] = static_cast<uint8_t>(key_id >> (8 * i));
        std::copy(psk, psk + 16, material.begin() + 12);
        const int file = ::open(path_, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (file < 0) throw std::runtime_error("Test output must be a new private file");
        const bool ok = ::write(file, material.data(), material.size()) == static_cast<ssize_t>(material.size()) && ::fsync(file) == 0;
        ::close(file);
        if (!ok) throw std::runtime_error("Test material save failed");
    }
    bool received_key = false;
    bool accepted_peer = false;

  private:
    const char* path_;
};

int main(int argc, char** argv)
{
    if (argc != 3 || (std::strcmp(argv[1], "leader") && std::strcmp(argv[1], "member")))
    {
        std::fprintf(stderr, "Usage: %s leader|member NEW_PRIVATE_OUTPUT_FILE\n", argv[0]);
        return 2;
    }
    try
    {
        Runtime runtime;
        Events events(argv[2]);
        platform::linux_runtime::LinuxEspNowPairing pairing(runtime, events);
        const bool leader = !std::strcmp(argv[1], "leader");
        bool ok = false;
        if (leader)
        {
            team::TeamId id{};
            std::array<uint8_t, 16> key{};
            runtime.fillRandomBytes(id.data(), id.size());
            runtime.fillRandomBytes(key.data(), key.size());
            events.save(id, 1, key.data());
            ok = pairing.startLeader(id, 1, key.data(), key.size(), 0x12345678, "Linux RF Test");
        }
        else
        {
            ok = pairing.startMember(0x87654321);
        }
        if (!ok) throw std::runtime_error("Native pairing start failed");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(leader ? 45 : 28);
        while (std::chrono::steady_clock::now() < deadline && !events.received_key)
        {
            pairing.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        pairing.stop();
        const bool success = leader ? events.accepted_peer : events.received_key;
        std::printf("PAIRING_TOOL_RESULT=%s\n", success ? "success" : "failure");
        return success ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
