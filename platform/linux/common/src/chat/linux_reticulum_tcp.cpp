#include "chat/linux_reticulum_tcp.h"
#include "chat/infra/reticulum/interface_access_codec.h"
#include "chat/infra/reticulum/tcp_retry.h"
#include <Crypto.h>
#include <algorithm>
#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <curl/curl.h>
#include <string>

namespace trailmate::linux_app
{
namespace r = ::chat::reticulum;
namespace
{
constexpr size_t kWireCapacity = r::kReticulumMtu + r::InterfaceAccessCodec::kMaxTagSize;
constexpr uint8_t kFlag = 0x7e, kEscape = 0x7d, kMask = 0x20;
bool text(const char* value, size_t capacity, bool empty = false)
{
    return std::memchr(value, 0, capacity) && (empty || value[0]);
}
bool hostUrl(const char* input, std::string& out)
{
    if (!text(input, r::kInterfaceHostMaxLen + 1)) return false;
    std::string host(input);
    if (host.front() == '[' || host.back() == ']')
    {
        if (host.front() != '[' || host.back() != ']' || host.find(':') == std::string::npos) return false;
        host = host.substr(1, host.size() - 2);
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
        out = "[" + host + "]";
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
            const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            if (!letter && !(c >= '0' && c <= '9') && (c != '-' || start)) return false;
            start = false;
        }
        previous = char(c);
    }
    if (previous == '-') return false;
    out = host;
    return true;
}
bool equivalent(const r::NetworkInterfaceConfig& lhs, const r::NetworkInterfaceConfig& rhs)
{
    return !std::strcmp(lhs.id, rhs.id) && lhs.enabled == rhs.enabled && lhs.target_port == rhs.target_port &&
           !std::strcmp(lhs.target_host, rhs.target_host) && lhs.access == rhs.access;
}
} // namespace
struct LinuxReticulumTcp::Impl
{
    struct Tx
    {
        std::array<uint8_t, 2 * kWireCapacity + 2> bytes{};
        size_t size = 0, offset = 0;
        uint32_t created = 0;
    };
    struct Channel
    {
        r::NetworkInterfaceConfig config;
        r::InterfaceAccessCodec access;
        r::TcpRetry retry;
        Endpoint status;
        CURL* easy = nullptr;
        CURLM* multi = nullptr;
        bool attached = false, in_frame = false, escaped = false;
        std::array<uint8_t, kWireCapacity> frame{}, encoding{};
        size_t frame_size = 0;
        std::array<Tx, 8> tx{};
        size_t head = 0, count = 0;
        std::string url;
        explicit Channel(const r::NetworkInterfaceConfig& config, std::string host) : config(config)
        {
            std::snprintf(status.id, sizeof(status.id), "%s", config.id);
            status.state = config.enabled ? State::Waiting : State::Disabled;
            url = "http://" + host + ":" + std::to_string(config.target_port) + "/";
        }
        ~Channel()
        {
            disconnect();
            clean(&config.access, sizeof(config.access));
        }
        void disconnect()
        {
            if (attached) curl_multi_remove_handle(multi, easy);
            attached = false;
            if (easy) curl_easy_cleanup(easy);
            if (multi) curl_multi_cleanup(multi);
            easy = nullptr;
            multi = nullptr;
            status.discarded_tx += count;
            head = count = frame_size = 0;
            in_frame = escaped = false;
            frame.fill(0);
            encoding.fill(0);
            tx = {};
            status.state = config.enabled ? State::Waiting : State::Disabled;
        }
        void fail(const char* reason, uint32_t now)
        {
            std::snprintf(status.error, sizeof(status.error), "%s", reason);
            ++status.failures;
            disconnect();
            retry.failed(now);
        }
    };
    Options options;
    std::array<std::unique_ptr<Channel>, r::kMaxTcpClientInterfaces> channels;
    std::array<Frame, 32> rx{};
    size_t head = 0, count = 0;
    uint32_t now = 0;
    uint64_t generation = 0, discarded = 0;
    bool supported = false;
    char error[96]{};
    explicit Impl(Options value) : options(value)
    {
        options.connect_timeout_ms = std::clamp(options.connect_timeout_ms, 100U, 30000U);
        options.max_pending_ms = std::clamp(options.max_pending_ms, 100U, 30000U);
        options.read_byte_budget = std::clamp<size_t>(options.read_byte_budget, 1, 4096);
        options.write_byte_budget = std::clamp<size_t>(options.write_byte_budget, 1, 4096);
        static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
        const auto* version = curl_version_info(CURLVERSION_NOW);
        supported = initialized == CURLE_OK && version && (version->features & CURL_VERSION_ASYNCHDNS);
        if (!supported) std::snprintf(error, sizeof(error), "Asynchronous curl resolver unavailable");
    }
    Channel* find(const char* id) const
    {
        if (!id) return nullptr;
        for (const auto& channel : channels)
            if (channel && !std::strcmp(channel->config.id, id)) return channel.get();
        return nullptr;
    }
    bool configure(const r::ReticulumNetworkConfig& config)
    {
        if (!supported) return false;
        const auto invalid = [&]
        { std::snprintf(error, sizeof(error), "Invalid Reticulum TCP configuration"); return false; };
        if (config.version != r::ReticulumNetworkConfig::kSchemaVersion || config.interface_count > r::kMaxNetworkInterfaces) return invalid();
        std::array<std::unique_ptr<Channel>, r::kMaxTcpClientInterfaces> prepared;
        std::array<int, r::kMaxTcpClientInterfaces> retained;
        retained.fill(-1);
        size_t used = 0;
        try
        {
            for (size_t index = 0; index < config.interface_count; ++index)
            {
                const auto& descriptor = config.interfaces[index];
                if (!text(descriptor.id, sizeof(descriptor.id))) return invalid();
                if (descriptor.type != r::NetworkInterfaceType::IntegratedLoRa && descriptor.type != r::NetworkInterfaceType::Auto && descriptor.type != r::NetworkInterfaceType::TcpClient) return invalid();
                for (size_t before = 0; before < index; ++before)
                    if (!std::strcmp(descriptor.id, config.interfaces[before].id)) return invalid();
                if (descriptor.type != r::NetworkInterfaceType::TcpClient) continue;
                std::string host;
                if (used == prepared.size() || !descriptor.access.valid() || !descriptor.target_port ||
                    !text(descriptor.target_host, sizeof(descriptor.target_host), !descriptor.enabled) ||
                    (descriptor.target_host[0] && !hostUrl(descriptor.target_host, host))) return invalid();
                for (size_t old = 0; old < channels.size(); ++old)
                    if (channels[old] && equivalent(descriptor, channels[old]->config)) retained[used] = int(old);
                if (retained[used] < 0)
                {
                    prepared[used] = std::make_unique<Channel>(descriptor, host);
                    if (!prepared[used]->access.configure(descriptor.access.network_name, descriptor.access.passphrase, descriptor.access.ifac_size_bits / 8)) return invalid();
                }
                ++used;
            }
        }
        catch (...)
        {
            std::snprintf(error, sizeof(error), "Reticulum TCP configuration allocation failed");
            return false;
        }
        for (size_t index = 0; index < used; ++index)
            if (retained[index] >= 0) prepared[index] = std::move(channels[size_t(retained[index])]);
        channels = std::move(prepared);
        error[0] = 0;
        return true;
    }
    void connect(Channel& channel)
    {
        channel.easy = curl_easy_init();
        channel.multi = curl_multi_init();
        if (!channel.easy || !channel.multi)
        {
            channel.fail("TCP connection allocation failed", now);
            return;
        }
        auto* easy = channel.easy;
        const bool configured = curl_easy_setopt(easy, CURLOPT_URL, channel.url.c_str()) == CURLE_OK &&
                                curl_easy_setopt(easy, CURLOPT_PROXY, "") == CURLE_OK &&
                                curl_easy_setopt(easy, CURLOPT_CONNECT_ONLY, 1L) == CURLE_OK &&
                                curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L) == CURLE_OK &&
                                curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, long(options.connect_timeout_ms)) == CURLE_OK &&
                                curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, long(options.connect_timeout_ms)) == CURLE_OK &&
                                curl_easy_setopt(easy, CURLOPT_TCP_NODELAY, 1L) == CURLE_OK &&
                                curl_easy_setopt(easy, CURLOPT_TCP_KEEPALIVE, 1L) == CURLE_OK;
        if (!configured || curl_multi_add_handle(channel.multi, easy) != CURLM_OK)
        {
            channel.fail("TCP connection configuration failed", now);
            return;
        }
        channel.attached = true;
        if (++generation == 0) ++generation;
        channel.status.generation = generation;
        channel.status.state = State::Connecting;
    }
    void purgeRetired()
    {
        size_t kept = 0;
        for (size_t index = 0; index < count; ++index)
        {
            const auto& frame = rx[(head + index) % rx.size()];
            const auto* channel = find(frame.interface_id);
            if (channel && channel->status.state == State::Connected && channel->status.generation == frame.connection_generation)
            {
                if (kept != index) rx[(head + kept) % rx.size()] = frame;
                ++kept;
            }
            else ++discarded;
        }
        for (size_t index = kept; index < count; ++index) rx[(head + index) % rx.size()] = {};
        count = kept;
    }
    bool enqueue(Channel& channel)
    {
        size_t size = channel.frame_size;
        if (!channel.access.decode(channel.frame.data(), size, channel.frame.size()))
        {
            ++channel.status.rejected;
            return true;
        }
        // A retired endpoint must not occupy the live endpoints' receive
        // space until the consumer gets its next polling opportunity.
        if (count == rx.size()) purgeRetired();
        if (count == rx.size())
        {
            ++channel.status.rejected;
            return false;
        }
        auto& frame = rx[(head + count) % rx.size()];
        std::snprintf(frame.interface_id, sizeof(frame.interface_id), "%s", channel.config.id);
        frame.connection_generation = channel.status.generation;
        std::memcpy(frame.data.data(), channel.frame.data(), size);
        frame.size = size;
        ++count;
        ++channel.status.received;
        return true;
    }
    bool byte(Channel& channel, uint8_t value)
    {
        if (value == kFlag)
        {
            bool accepted = true;
            if (channel.in_frame && channel.frame_size)
            {
                if (!channel.escaped) accepted = enqueue(channel);
                else ++channel.status.rejected;
            }
            channel.in_frame = true;
            channel.escaped = false;
            channel.frame_size = 0;
            return accepted;
        }
        if (!channel.in_frame) return true;
        if (channel.escaped)
        {
            if (value != (kFlag ^ kMask) && value != (kEscape ^ kMask))
            {
                channel.in_frame = false;
                channel.frame_size = 0;
                channel.escaped = false;
                ++channel.status.rejected;
                return true;
            }
            value ^= kMask;
            channel.escaped = false;
        }
        else if (value == kEscape)
        {
            channel.escaped = true;
            return true;
        }
        if (channel.frame_size == channel.frame.size())
        {
            channel.in_frame = false;
            channel.frame_size = 0;
            ++channel.status.rejected;
            return true;
        }
        channel.frame[channel.frame_size++] = value;
        return true;
    }
    void tick(uint32_t value)
    {
        now = value;
        for (auto& entry : channels)
        {
            if (!entry || !entry->config.enabled) continue;
            auto& channel = *entry;
            if (!channel.easy)
            {
                if (!channel.retry.ready(now)) continue;
                connect(channel);
                if (!channel.easy) continue;
            }
            if (channel.status.state == State::Connecting)
            {
                int running = 0;
                if (curl_multi_perform(channel.multi, &running) != CURLM_OK)
                {
                    channel.fail("TCP connection progress failed", now);
                    continue;
                }
                int left = 0;
                while (auto* message = curl_multi_info_read(channel.multi, &left))
                    if (message->msg == CURLMSG_DONE)
                    {
                        if (message->data.result != CURLE_OK)
                        {
                            channel.fail(curl_easy_strerror(message->data.result), now);
                            break;
                        }
                        channel.status.state = State::Connected;
                        channel.status.error[0] = 0;
                        ++channel.status.connections;
                        channel.retry.connected(now);
                    }
            }
            if (channel.status.state != State::Connected) continue;
            channel.retry.connected(now);
            if (channel.count && uint32_t(now - channel.tx[channel.head].created) >= options.max_pending_ms)
            {
                channel.fail("TCP pending frame expired", now);
                continue;
            }
            size_t budget = options.write_byte_budget;
            while (channel.count && budget)
            {
                auto& frame = channel.tx[channel.head];
                size_t written = 0;
                const auto result = curl_easy_send(channel.easy, frame.bytes.data() + frame.offset, std::min(frame.size - frame.offset, budget), &written);
                if (result == CURLE_AGAIN) break;
                if (result != CURLE_OK || !written)
                {
                    channel.fail("TCP write failed", now);
                    break;
                }
                frame.offset += written;
                budget -= written;
                if (frame.offset == frame.size)
                {
                    frame = {};
                    channel.head = (channel.head + 1) % channel.tx.size();
                    --channel.count;
                    ++channel.status.transmitted;
                }
            }
            if (channel.status.state != State::Connected) continue;
            std::array<uint8_t, 4096> bytes;
            size_t received = 0;
            const auto result = curl_easy_recv(channel.easy, bytes.data(), options.read_byte_budget, &received);
            if (result == CURLE_AGAIN) continue;
            if (result != CURLE_OK || !received)
            {
                channel.fail("TCP peer disconnected", now);
                continue;
            }
            for (size_t index = 0; index < received; ++index)
                if (!byte(channel, bytes[index]))
                {
                    channel.fail("TCP receive queue full", now);
                    break;
                }
        }
    }
    bool send(const char* id, uint64_t epoch, const uint8_t* bytes, size_t size)
    {
        auto* channel = find(id);
        if (!channel || channel->status.state != State::Connected || !epoch || epoch != channel->status.generation ||
            !bytes || size < 3 || size > r::kReticulumMtu || channel->count == channel->tx.size()) return false;
        std::memcpy(channel->encoding.data(), bytes, size);
        if (!channel->access.encode(channel->encoding.data(), size, channel->encoding.size())) return false;
        auto& frame = channel->tx[(channel->head + channel->count) % channel->tx.size()];
        frame.size = 0;
        frame.offset = 0;
        frame.created = now;
        frame.bytes[frame.size++] = kFlag;
        for (size_t index = 0; index < size; ++index)
        {
            const auto value = channel->encoding[index];
            if (value == kFlag || value == kEscape)
            {
                frame.bytes[frame.size++] = kEscape;
                frame.bytes[frame.size++] = value ^ kMask;
            }
            else frame.bytes[frame.size++] = value;
        }
        frame.bytes[frame.size++] = kFlag;
        channel->encoding.fill(0);
        ++channel->count;
        return true;
    }
};
LinuxReticulumTcp::LinuxReticulumTcp() : LinuxReticulumTcp(Options{}) {}
LinuxReticulumTcp::LinuxReticulumTcp(Options options) : impl_(std::make_unique<Impl>(options)) {}
LinuxReticulumTcp::~LinuxReticulumTcp() = default;
bool LinuxReticulumTcp::configure(const r::ReticulumNetworkConfig& config) { return impl_->configure(config); }
void LinuxReticulumTcp::tick(uint32_t now) { impl_->tick(now); }
bool LinuxReticulumTcp::send(const char* id, uint64_t generation, const uint8_t* data, size_t size) { return impl_->send(id, generation, data, size); }
bool LinuxReticulumTcp::online(const char* id, uint64_t generation) const
{
    const auto* channel = impl_->find(id);
    return channel && channel->status.state == State::Connected && generation && channel->status.generation == generation;
}
bool LinuxReticulumTcp::receive(Frame& out)
{
    while (impl_->count)
    {
        auto& frame = impl_->rx[impl_->head];
        const bool current = online(frame.interface_id, frame.connection_generation);
        if (current) out = frame;
        frame = {};
        impl_->head = (impl_->head + 1) % impl_->rx.size();
        --impl_->count;
        if (current) return true;
        ++impl_->discarded;
    }
    return false;
}
void LinuxReticulumTcp::clear()
{
    impl_->channels = {};
    impl_->rx = {};
    impl_->head = impl_->count = 0;
}
LinuxReticulumTcp::Snapshot LinuxReticulumTcp::snapshot() const
{
    Snapshot result;
    result.supported = impl_->supported;
    result.discarded_rx = impl_->discarded;
    std::snprintf(result.error, sizeof(result.error), "%s", impl_->error);
    for (const auto& channel : impl_->channels)
        if (channel)
        {
            auto& status = result.endpoints[result.count++];
            status = channel->status;
            status.pending_tx = channel->count;
        }
    return result;
}
} // namespace trailmate::linux_app
