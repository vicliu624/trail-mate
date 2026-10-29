#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace trailmate::linux_app
{
// Serialized worker-owned transport. No UI calls, radio API or NIC selection.
class LinuxMqttClient
{
  public:
    struct Options
    {
        bool enabled = false, tls = false;
        std::string host, client_id, username, password, subscription;
        uint16_t port = 1883, keepalive_seconds = 60;
        uint32_t connect_ms = 5000, handshake_ms = 10000, pending_ms = 10000;
        uint32_t retry_initial_ms = 2000, retry_max_ms = 60000;
    };
    enum class State
    {
        Disabled,
        Waiting,
        Connecting,
        Connack,
        Suback,
        Online
    };
    struct Snapshot
    {
        State state = State::Disabled;
        uint64_t generation = 0, connections = 0, failures = 0, transmitted = 0, received = 0, discarded = 0;
        size_t pending_tx = 0;
        char error[96]{};
    };
    struct Message
    {
        uint64_t generation = 0;
        std::array<char, 193> topic{};
        std::array<uint8_t, 512> bytes{};
        size_t size = 0;
        bool retained = false, duplicate = false;
    };
    struct Outcome
    {
        uint64_t token = 0, generation = 0;
        bool written = false;
    };
    LinuxMqttClient();
    ~LinuxMqttClient();
    // Invalid updates preserve the active endpoint. Disabling always quiesces.
    bool configure(const Options&);
    void tick(uint32_t now_ms);
    bool publish(const char* topic, const uint8_t*, size_t, uint64_t token = 0);
    bool receive(Message&);
    bool outcome(Outcome&);
    void clear();
    Snapshot snapshot() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace trailmate::linux_app
