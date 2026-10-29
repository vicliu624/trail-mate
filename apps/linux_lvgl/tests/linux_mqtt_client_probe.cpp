#include "chat/linux_mqtt_client.h"
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
} // namespace
int main(int argc, char** argv)
{
    if (argc != 4) return 2;
    using Client = trailmate::linux_app::LinuxMqttClient;
    const std::string mode = argv[3];
    Client client;
    Client::Options options;
    options.enabled = true;
    options.host = argv[1];
    options.port = static_cast<uint16_t>(std::stoul(argv[2]));
    options.client_id = "tm-fixture";
    options.username = "u";
    options.password = "p";
    options.subscription = "fixture/#";
    options.keepalive_seconds = 1;
    options.connect_ms = 500;
    options.handshake_ms = 300;
    options.pending_ms = 250;
    options.retry_initial_ms = 50;
    options.retry_max_ms = 100;
    for (const char* host : {"[]", "[broker]", "bad/host", "user@host", "host:wrong"})
    {
        auto invalid = options;
        invalid.host = host;
        require(!client.configure(invalid) && client.snapshot().state == Client::State::Disabled, "invalid host changed active transport");
    }
    require(client.configure(options), "MQTT options rejected");
    const auto started = std::chrono::steady_clock::now();
    uint64_t first_generation = 0;
    unsigned outcomes = 0;
    bool first_message = false;
    Client::Message message;
    while (std::chrono::steady_clock::now() - started < std::chrono::seconds(5))
    {
        const auto now = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
        client.tick(now);
        const auto state = client.snapshot();
        if (mode == "failure" && state.failures)
        {
            require(state.state != Client::State::Online && state.error[0], "broker failure falsely online");
            client.clear();
            return 0;
        }
        if (state.state == Client::State::Online && !first_generation)
        {
            first_generation = state.generation;
            auto invalid = options;
            invalid.subscription = "invalid/#/filter";
            require(!client.configure(invalid) && client.snapshot().state == Client::State::Online && client.snapshot().generation == first_generation,
                    "invalid update retired the authenticated connection");
            require(client.configure(options) && client.snapshot().generation == first_generation, "identical options reset connection");
            uint8_t bytes[]{1, 0, 2};
            if (mode == "budget")
            {
                for (uint64_t id = 1; id <= 8; ++id) require(client.publish("fixture/out", bytes, 3, id), "bounded publish slot missing");
                require(!client.publish("fixture/out", bytes, 3, 9), "publish queue overcommitted");
                client.clear();
                Client::Outcome outcome;
                unsigned count = 0;
                while (client.outcome(outcome))
                {
                    require(!outcome.written && outcome.generation == first_generation && outcome.token == ++count, "pending publish failed without correct outcome");
                }
                require(count == 8 && client.snapshot().state == Client::State::Disabled, "disable did not settle all accepted publishes");
                return 0;
            }
            if (mode == "baseline")
            {
                require(client.publish("fixture/out", bytes, 3, 7), "publish not admitted");
                std::memset(bytes, 9, sizeof(bytes));
            }
        }
        if (mode == "baseline")
        {
            Client::Outcome outcome;
            while (client.outcome(outcome))
            {
                require(outcome.token == 7 && outcome.written && outcome.generation == first_generation && ++outcomes == 1, "publish outcome was duplicated or misattributed");
            }
            if (state.connections == 1 && !first_message && client.receive(message))
            {
                require(std::string(message.topic.data()) == "fixture/in" && message.generation == first_generation && message.retained && message.duplicate &&
                            message.size == 3 && message.bytes[0] == 9 && message.bytes[1] == 0 && message.bytes[2] == 8,
                        "inbound QoS1/binary metadata changed");
                first_message = true;
            }
            if (state.connections >= 2 && client.receive(message))
            {
                require(first_message && outcomes == 1 && state.generation > first_generation && message.generation == state.generation &&
                            std::string(message.topic.data()) == "fixture/in" && message.bytes[0] == 4 && message.bytes[1] == 0 && message.bytes[2] == 5 && state.discarded >= 2,
                        "old-generation receive data survived reconnect");
                client.clear();
                return 0;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error("MQTT loopback condition timed out");
}
