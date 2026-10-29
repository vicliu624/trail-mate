#include "chat/linux_reticulum_links.h"
#include "chat/ports/track_delivery_binding.h"
#include <algorithm>
#include <cassert>
#include <deque>
#include <filesystem>
#include <stdexcept>
#include <unistd.h>

using Identity = platform::linux_runtime::LinuxReticulumIdentity;
using Links = trailmate::linux_app::LinuxReticulumLinks;
struct Pair
{
    struct Frame
    {
        bool to_a;
        std::vector<uint8_t> bytes;
    };
    struct Wire
    {
        Pair* pair;
        bool from_a;
        static bool send(void* context, const uint8_t* data, size_t bytes)
        {
            auto& wire = *static_cast<Wire*>(context);
            wire.pair->frames.push_back({!wire.from_a, {data, data + bytes}});
            return true;
        }
    } wire_a{this, true}, wire_b{this, false};
    struct Sink
    {
        std::array<uint8_t, 64> peer{};
        std::array<uint8_t, 16> source{}, destination{};
        std::array<uint8_t, 32> hash{};
        std::vector<uint8_t> bytes;
        unsigned calls = 0;
        bool accept = true, throws = false;
        static bool resolve(void* context, const uint8_t*, uint8_t* key, bool* ignored)
        {
            *ignored = false;
            std::copy_n(static_cast<Sink*>(context)->peer.data(), 64, key);
            return true;
        }
        static bool custom(void* context, chat::lxmf::ByteSpan type, const chat::lxmf::CustomDeliveryView& view)
        {
            auto& sink = *static_cast<Sink*>(context);
            ++sink.calls;
            assert(view.source.size == 16 && view.destination.size == 16 && view.message_hash.size == 32);
            assert(!std::memcmp(view.source.data, sink.source.data(), 16) && !std::memcmp(view.destination.data, sink.destination.data(), 16));
            if (sink.throws) throw std::runtime_error("injected business rejection");
            if (!sink.accept || !chat::lxmf::isTrackCustomType(type)) return false;
            sink.bytes.assign(view.data.data, view.data.data + view.data.size);
            std::copy_n(view.message_hash.data, 32, sink.hash.begin());
            return true; // Models a completed durable/control acceptance port.
        }
    } sink_a, sink_b;
    explicit Pair(const std::filesystem::path& root) : a_id(root / "a.sqlite"), b_id(root / "b.sqlite"), a(a_id), b(b_id)
    {
        assert(a_id.begin() && b_id.begin());
        sink_a.peer = b_id.publicKey();
        sink_b.peer = a_id.publicKey();
        sink_a.source = b_id.destinationHash();
        sink_a.destination = a_id.destinationHash();
        sink_b.source = a_id.destinationHash();
        sink_b.destination = b_id.destinationHash();
        a.setIncomingHooks(nullptr, Sink::resolve, &sink_a);
        b.setIncomingHooks(nullptr, Sink::resolve, &sink_b);
        a.setCustomReceiver(Sink::custom, &sink_a);
        b.setCustomReceiver(Sink::custom, &sink_b);
        a.setOutcomeSink([](void* context, chat::MessageId, chat::MessageStatus, chat::delivery::SendFailureKind)
                         { ++static_cast<Pair*>(context)->text_outcomes; },
                         this);
        a.setCustomOutcomeSink([](void* context, chat::MessageId, chat::MessageStatus status, chat::delivery::SendFailureKind)
                               { if(status==chat::MessageStatus::Delivered) ++static_cast<Pair*>(context)->delivered; },
                               this);
    }
    void pump(unsigned cycles)
    {
        for (unsigned cycle = 0; cycle < cycles; ++cycle)
        {
            a.processOutbound(now, Wire::send, &wire_a);
            b.processOutbound(now, Wire::send, &wire_b);
            unsigned count = 0;
            while (!frames.empty())
            {
                assert(++count < 1000);
                auto frame = std::move(frames.front());
                frames.pop_front();
                if (frame.to_a) a.receive(frame.bytes.data(), frame.bytes.size(), now, Wire::send, &wire_a);
                else b.receive(frame.bytes.data(), frame.bytes.size(), now, Wire::send, &wire_b);
            }
            a.retryAccepted(now, Wire::send, &wire_a);
            b.retryAccepted(now, Wire::send, &wire_b);
            a.tick(now);
            b.tick(now);
            now += 10;
        }
    }
    Identity a_id, b_id;
    Links a, b;
    std::deque<Frame> frames;
    unsigned delivered = 0;
    unsigned text_outcomes = 0;
    uint32_t now = 100;
};
int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::string scenario(argv[1]);
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-custom-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    {
        auto pair = std::make_unique<Pair>(root);
        const bool large = scenario == "resource" || scenario == "reject_resource";
        std::vector<uint8_t> data(large ? 8192 : 64);
        for (size_t index = 0; index < data.size(); ++index) data[index] = uint8_t(index * 37);
        const bool rejecting = scenario == "reject" || scenario == "reject_resource" || scenario == "unknown" || scenario == "throw" || scenario == "wrong_author";
        pair->sink_b.accept = !rejecting || scenario == "unknown";
        pair->sink_b.throws = scenario == "throw";
        if (scenario == "wrong_author") pair->sink_b.peer[0] ^= 1;
        auto key = pair->b_id.publicKey();
        if (scenario == "wrong_destination") key[0] ^= 1;
        if (scenario == "oversize") data.resize(8193);
        std::array<uint8_t, 32> accepted{};
        const auto result = pair->a.queueCustom(pair->b_id.destinationHash().data(), key.data(), scenario == "unknown" ? "unhandled.application" : "trailmate.track.v1",
                                                {data.data(), data.size()}, 123, 1.0, pair->now, Pair::Wire::send, &pair->wire_a, 1, nullptr, accepted.data());
        if (scenario == "oversize" || scenario == "wrong_destination")
        {
            assert(result == Links::QueueResult::Invalid && pair->frames.empty());
        }
        else
        {
            assert(result == Links::QueueResult::Queued);
            for (unsigned step = 0; step < 2000 && !pair->delivered; ++step) pair->pump(1);
            if (rejecting)
            {
                assert(!pair->delivered && pair->b.statistics().incoming_custom == 0 && pair->b.statistics().delivery_proofs == 0);
                assert(pair->b.statistics().incoming_texts == 0 && pair->b.statistics().pending_incoming == 0);
                if (scenario == "wrong_author") assert(pair->sink_b.calls == 0);
                else assert(pair->sink_b.calls > 0);
            }
            else
            {
                assert(pair->delivered == 1 && pair->sink_b.calls == 1 && pair->sink_b.bytes == data && pair->sink_b.hash == accepted);
                assert(pair->b.statistics().incoming_custom == 1 && pair->b.statistics().incoming_texts == 0);
            }
        }
        assert(pair->text_outcomes == 0); // Application control is never a chat result.
    }
    std::filesystem::remove_all(root);
}
