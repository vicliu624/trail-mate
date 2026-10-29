#include "chat/linux_mqtt_client.h"
#include "chat/infra/mqtt/mqtt311_codec.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <curl/curl.h>

namespace trailmate::linux_app
{
namespace
{
namespace mqtt = chat::mqtt311;
bool hostPart(std::string host, std::string& output)
{
    if (host.empty() || host.size() > 253) return false;
    if (host.front() == '[' || host.back() == ']')
    {
        if (host.front() != '[' || host.back() != ']') return false;
        host = host.substr(1, host.size() - 2);
        if (host.find(':') == std::string::npos) return false;
    }
    if (host.find(':') != std::string::npos)
    {
        const auto zone = host.find('%');
        const auto address = host.substr(0, zone);
        in6_addr parsed;
        if (inet_pton(AF_INET6, address.c_str(), &parsed) != 1) return false;
        if (zone != std::string::npos)
        {
            const auto scope = host.substr(zone + 1);
            if (scope.empty() || !std::all_of(scope.begin(), scope.end(), [](unsigned char c)
                                              { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'; })) return false;
            host = address + "%25" + scope;
        }
        output = "[" + host + "]";
        return true;
    }
    bool start = true;
    char previous = 0;
    for (const unsigned char c : host)
    {
        if (c == '.')
        {
            if (start || previous == '-') return false;
            start = true;
        }
        else
        {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c == '-' && !start))) return false;
            start = false;
        }
        previous = char(c);
    }
    if (previous == '-') return false;
    output = host;
    return true;
}
bool same(const LinuxMqttClient::Options& a, const LinuxMqttClient::Options& b)
{
    return a.enabled == b.enabled && a.tls == b.tls && a.host == b.host && a.port == b.port && a.client_id == b.client_id &&
           a.username == b.username && a.password == b.password && a.subscription == b.subscription && a.keepalive_seconds == b.keepalive_seconds &&
           a.connect_ms == b.connect_ms && a.handshake_ms == b.handshake_ms && a.pending_ms == b.pending_ms &&
           a.retry_initial_ms == b.retry_initial_ms && a.retry_max_ms == b.retry_max_ms;
}
bool matches(std::string_view filter, std::string_view topic)
{
    if (filter.empty() || (!topic.empty() && topic[0] == '$' && (filter[0] == '#' || filter[0] == '+'))) return false;
    size_t f = 0, t = 0;
    for (;;)
    {
        const auto fe = filter.find('/', f), te = topic.find('/', t);
        const auto fs = filter.substr(f, fe == filter.npos ? filter.size() - f : fe - f);
        const auto ts = topic.substr(t, te == topic.npos ? topic.size() - t : te - t);
        if (fs == "#") return true;
        if (fs != "+" && fs != ts) return false;
        if (fe == filter.npos) return te == topic.npos;
        if (te == topic.npos) return filter.substr(fe + 1) == "#";
        f = fe + 1;
        t = te + 1;
    }
}
} // namespace

struct LinuxMqttClient::Impl
{
    struct Tx
    {
        mqtt::Packet packet;
        size_t offset = 0;
        uint32_t started = 0;
        uint64_t token = 0;
        bool application = false;
    };
    Options options;
    Snapshot status;
    CURL* easy = nullptr;
    CURLM* multi = nullptr;
    bool attached = false, supported = false, immediate = true, connect_written = false, subscribe_written = false, ping_pending = false, ping_written = false;
    uint32_t now = 0, phase_started = 0, retry_started = 0, retry_wait = 0, retry_delay = 2000, last_written = 0, ping_started = 0, partial_started = 0;
    uint16_t subscription_id = 0;
    std::string url;
    std::array<Tx, 16> tx{};
    size_t tx_head = 0, tx_count = 0;
    std::array<Message, 16> rx{};
    size_t rx_head = 0, rx_count = 0;
    std::array<Outcome, 16> outcomes{};
    size_t outcome_head = 0, outcome_count = 0;
    mqtt::Packet encoding;
    mqtt::Decoder decoder;
    std::array<uint8_t, 4096> input{};

    Impl()
    {
        static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
        const auto* version = curl_version_info(CURLVERSION_NOW);
        supported = initialized == CURLE_OK && version && (version->features & CURL_VERSION_ASYNCHDNS);
    }
    ~Impl() { close(); }
    void result(const Tx& entry, bool written)
    {
        if (!entry.application || !entry.token) return;
        // Admission reserves one result slot for every accepted tagged publish.
        outcomes[(outcome_head + outcome_count) % outcomes.size()] = {entry.token, status.generation, written};
        ++outcome_count;
    }
    void close()
    {
        if (attached) curl_multi_remove_handle(multi, easy);
        attached = false;
        if (easy) curl_easy_cleanup(easy);
        if (multi) curl_multi_cleanup(multi);
        easy = nullptr;
        multi = nullptr;
        for (size_t index = 0; index < tx_count; ++index) result(tx[(tx_head + index) % tx.size()], false);
        status.discarded += rx_count;
        tx_head = tx_count = rx_head = rx_count = 0;
        status.pending_tx = 0;
        for (auto& entry : tx)
        {
            entry.packet.bytes.fill(0);
            entry.packet.size = entry.offset = 0;
            entry.token = 0;
            entry.application = false;
        }
        for (auto& message : rx)
        {
            message.topic.fill(0);
            message.bytes.fill(0);
            message.size = 0;
        }
        encoding.bytes.fill(0);
        encoding.size = 0;
        decoder.reset();
        connect_written = subscribe_written = ping_pending = ping_written = false;
    }
    void fail(const char* error)
    {
        close();
        ++status.failures;
        std::snprintf(status.error, sizeof(status.error), "%s", error);
        status.state = options.enabled ? State::Waiting : State::Disabled;
        immediate = false;
        retry_started = now;
        retry_wait = retry_delay;
        retry_delay = std::min(options.retry_max_ms, retry_delay > options.retry_max_ms / 2 ? options.retry_max_ms : retry_delay * 2);
    }
    bool queue(bool application = false, uint64_t token = 0)
    {
        if (tx_count == tx.size()) return false;
        tx[(tx_head + tx_count) % tx.size()] = {encoding, 0, now, token, application};
        ++tx_count;
        status.pending_tx = tx_count;
        return true;
    }
    bool configure(const Options& value)
    {
        if (!value.enabled)
        {
            close();
            options = {};
            status.state = State::Disabled;
            status.error[0] = 0;
            return true;
        }
        std::string host;
        const auto duration = [](uint32_t milliseconds)
        { return milliseconds >= 50 && milliseconds <= 60000; };
        if (!supported || !value.port || value.client_id.empty() || value.client_id.size() > 64 || value.username.size() > 256 || value.password.size() > 256 ||
            !mqtt::validText(value.client_id) || !mqtt::validText(value.username) || value.subscription.size() > 192 ||
            (!value.subscription.empty() && !mqtt::validTopic(value.subscription, true)) || !hostPart(value.host, host) ||
            !duration(value.connect_ms) || !duration(value.handshake_ms) || !duration(value.pending_ms) ||
            !duration(value.retry_initial_ms) || !duration(value.retry_max_ms) || value.retry_initial_ms > value.retry_max_ms) return false;
        if (same(options, value)) return true;
        close();
        options = value;
        url = std::string(options.tls ? "https://" : "http://") + host + ":" + std::to_string(options.port) + "/";
        status.state = State::Waiting;
        status.error[0] = 0;
        immediate = true;
        retry_delay = value.retry_initial_ms;
        return true;
    }
    void start()
    {
        easy = curl_easy_init();
        multi = curl_multi_init();
        if (!easy || !multi)
        {
            fail("MQTT connection allocation failed");
            return;
        }
        if (curl_easy_setopt(easy, CURLOPT_URL, url.c_str()) != CURLE_OK || curl_easy_setopt(easy, CURLOPT_PROXY, "") != CURLE_OK ||
            curl_easy_setopt(easy, CURLOPT_CONNECT_ONLY, 1L) != CURLE_OK || curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L) != CURLE_OK ||
            curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, long(options.connect_ms)) != CURLE_OK ||
            curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, long(options.connect_ms)) != CURLE_OK ||
            curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L) != CURLE_OK || curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L) != CURLE_OK ||
            curl_easy_setopt(easy, CURLOPT_TCP_NODELAY, 1L) != CURLE_OK || curl_multi_add_handle(multi, easy) != CURLM_OK)
        {
            fail("MQTT connection configuration failed");
            return;
        }
        attached = true;
        if (++status.generation == 0) ++status.generation;
        status.state = State::Connecting;
        phase_started = now;
    }
    void online()
    {
        status.state = State::Online;
        status.error[0] = 0;
        ++status.connections;
        retry_delay = options.retry_initial_ms;
    }
    bool handle(const mqtt::View& view)
    {
        if (view.type == mqtt::Type::Connack)
        {
            if (status.state != State::Connack || !connect_written || view.session_present || view.return_code) return false;
            if (options.subscription.empty())
            {
                online();
                return true;
            }
            if (++subscription_id == 0) ++subscription_id;
            if (!mqtt::subscribe(encoding, subscription_id, options.subscription) || !queue()) return false;
            status.state = State::Suback;
            subscribe_written = false;
            phase_started = now;
            return true;
        }
        if (view.type == mqtt::Type::Suback)
        {
            if (status.state != State::Suback || !subscribe_written || view.packet_id != subscription_id || view.size != 1 || view.payload[0]) return false;
            online();
            return true;
        }
        if (view.type == mqtt::Type::Pingresp)
        {
            if (!ping_pending || !ping_written) return false;
            ping_pending = ping_written = false;
            return true;
        }
        if (view.type != mqtt::Type::Publish || (status.state != State::Online && !(status.state == State::Suback && subscribe_written))) return false;
        const bool accepted = matches(options.subscription, view.topic) && view.topic.size() < rx[0].topic.size() && view.size <= rx[0].bytes.size();
        if (accepted)
        {
            if (rx_count == rx.size())
            {
                ++status.discarded;
                return !view.qos;
            }
            auto& message = rx[(rx_head + rx_count) % rx.size()];
            message.generation = status.generation;
            std::memcpy(message.topic.data(), view.topic.data(), view.topic.size());
            message.topic[view.topic.size()] = 0;
            if (view.size) std::memcpy(message.bytes.data(), view.payload, view.size);
            message.size = view.size;
            message.retained = view.retained;
            message.duplicate = view.duplicate;
            ++rx_count;
            ++status.received;
        }
        else ++status.discarded;
        return !view.qos || (mqtt::puback(encoding, view.packet_id) && queue());
    }
    void tick(uint32_t value)
    {
        now = value;
        if (status.state == State::Disabled) return;
        if (status.state == State::Waiting)
        {
            if (immediate || uint32_t(now - retry_started) >= retry_wait) start();
            else return;
        }
        if (status.state == State::Connecting)
        {
            if (uint32_t(now - phase_started) >= options.connect_ms)
            {
                fail("MQTT connect timeout");
                return;
            }
            int running = 0;
            if (curl_multi_perform(multi, &running) != CURLM_OK)
            {
                fail("MQTT connect failed");
                return;
            }
            int left = 0;
            while (auto* message = curl_multi_info_read(multi, &left))
                if (message->msg == CURLMSG_DONE)
                {
                    if (message->data.result != CURLE_OK || !mqtt::connect(encoding, options.client_id, options.username, options.password, options.keepalive_seconds) || !queue())
                    {
                        fail("MQTT connect failed");
                        return;
                    }
                    status.state = State::Connack;
                    phase_started = now;
                }
            if (status.state == State::Connecting) return;
        }
        if (!easy) return;
        if ((status.state == State::Connack || status.state == State::Suback) && uint32_t(now - phase_started) >= options.handshake_ms)
        {
            fail("MQTT handshake timeout");
            return;
        }
        if (decoder.partial() && uint32_t(now - partial_started) >= options.pending_ms)
        {
            fail("MQTT incomplete frame timeout");
            return;
        }
        if (ping_pending && uint32_t(now - ping_started) >= options.handshake_ms)
        {
            fail("MQTT ping timeout");
            return;
        }
        if (status.state == State::Online && options.keepalive_seconds && !ping_pending &&
            uint32_t(now - last_written) >= uint32_t(options.keepalive_seconds) * 500)
        {
            if (!mqtt::ping(encoding) || !queue())
            {
                fail("MQTT control queue full");
                return;
            }
            ping_pending = true;
            ping_written = false;
            ping_started = now;
        }
        size_t budget = 4096;
        while (tx_count && budget)
        {
            auto& entry = tx[tx_head];
            if (uint32_t(now - entry.started) >= options.pending_ms)
            {
                fail("MQTT write timeout");
                return;
            }
            size_t written = 0;
            const auto result = curl_easy_send(easy, entry.packet.bytes.data() + entry.offset, std::min(budget, entry.packet.size - entry.offset), &written);
            if (result == CURLE_AGAIN) break;
            if (result != CURLE_OK || !written)
            {
                fail("MQTT write failed");
                return;
            }
            entry.offset += written;
            budget -= written;
            if (entry.offset != entry.packet.size) continue;
            const auto kind = entry.packet.bytes[0] >> 4;
            if (kind == 1)
            {
                connect_written = true;
                phase_started = now;
            }
            if (kind == 8)
            {
                subscribe_written = true;
                phase_started = now;
            }
            if (kind == 12)
            {
                ping_written = true;
                ping_started = now;
            }
            if (entry.application)
            {
                ++status.transmitted;
                resultForHead();
            }
            last_written = now;
            entry.packet.bytes.fill(0);
            entry.packet.size = 0;
            entry.token = 0;
            entry.application = false;
            tx_head = (tx_head + 1) % tx.size();
            --tx_count;
            status.pending_tx = tx_count;
        }
        size_t received = 0;
        const auto read = curl_easy_recv(easy, input.data(), input.size(), &received);
        if (read == CURLE_AGAIN) return;
        if (read != CURLE_OK || !received)
        {
            fail("MQTT connection closed");
            return;
        }
        for (size_t index = 0; index < received; ++index)
        {
            if (!decoder.partial()) partial_started = now;
            mqtt::View view;
            const auto decoded = decoder.push(input[index], view);
            if (decoded == mqtt::Result::Error || (decoded == mqtt::Result::Packet && !handle(view)))
            {
                fail("MQTT invalid or refused broker packet");
                return;
            }
        }
    }
    void resultForHead() { result(tx[tx_head], true); }
    bool publish(const char* topic, const uint8_t* bytes, size_t size, uint64_t token)
    {
        if (status.state != State::Online || !topic || strnlen(topic, 193) > 192 || size > 512) return false;
        size_t applications = 0, tagged = 0;
        for (size_t index = 0; index < tx_count; ++index)
        {
            const auto& entry = tx[(tx_head + index) % tx.size()];
            if (entry.application)
            {
                ++applications;
                if (entry.token) ++tagged;
            }
        }
        if (applications >= 8 || (token && outcome_count + tagged >= outcomes.size())) return false;
        return mqtt::publish(encoding, topic, bytes, size) && queue(true, token);
    }
};
LinuxMqttClient::LinuxMqttClient() : impl_(std::make_unique<Impl>()) {}
LinuxMqttClient::~LinuxMqttClient() = default;
bool LinuxMqttClient::configure(const Options& options) { return impl_->configure(options); }
void LinuxMqttClient::tick(uint32_t now) { impl_->tick(now); }
bool LinuxMqttClient::publish(const char* topic, const uint8_t* bytes, size_t size, uint64_t token) { return impl_->publish(topic, bytes, size, token); }
bool LinuxMqttClient::receive(Message& message)
{
    if (!impl_->rx_count || impl_->status.state != State::Online) return false;
    message = impl_->rx[impl_->rx_head];
    impl_->rx_head = (impl_->rx_head + 1) % impl_->rx.size();
    --impl_->rx_count;
    return true;
}
bool LinuxMqttClient::outcome(Outcome& outcome)
{
    if (!impl_->outcome_count) return false;
    outcome = impl_->outcomes[impl_->outcome_head];
    impl_->outcome_head = (impl_->outcome_head + 1) % impl_->outcomes.size();
    --impl_->outcome_count;
    return true;
}
void LinuxMqttClient::clear() { impl_->configure({}); }
LinuxMqttClient::Snapshot LinuxMqttClient::snapshot() const { return impl_->status; }
} // namespace trailmate::linux_app
