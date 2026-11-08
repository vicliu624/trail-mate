#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_announce_ingestor.h"
#include <array>
#include <cassert>
#include <cstring>
#include <vector>

// Compile the actual firmware crypto port; only Serial/carrier declarations
// are host substitutes. No signature or announcement decision is mocked.
namespace chat::lxmf
{
#include "announce_verify_actual.inc"
}

using namespace chat;
using namespace chat::lxmf;
using namespace chat::lxmf::runtime;

struct SignedAnnounce
{
    std::array<uint8_t, 64> public_key{};
    std::array<uint8_t, 16> destination{};
    std::vector<uint8_t> payload;

    explicit SignedAnnounce(uint8_t nonce)
    {
        uint8_t seed[32] = {0x42}; // Synthetic test identity, never a device key.
        uint8_t private_key[64]{};
        std::memset(public_key.data(), 0x31, 32);
        ed25519_create_keypair(public_key.data() + 32, private_key, seed);
        uint8_t identity[16]{}, name[10]{}, random[10]{};
        reticulum::computeIdentityHash(public_key.data(), identity);
        reticulum::computeNameHash("lxmf", "delivery", name);
        reticulum::computeDestinationHash(name, identity, destination.data());
        random[9] = nonce;
        payload.insert(payload.end(), public_key.begin(), public_key.end());
        payload.insert(payload.end(), name, name + sizeof(name));
        payload.insert(payload.end(), random, random + sizeof(random));
        auto signed_bytes = std::vector<uint8_t>(destination.begin(), destination.end());
        signed_bytes.insert(signed_bytes.end(), payload.begin(), payload.end());
        uint8_t signature[64]{};
        ed25519_sign(signature, signed_bytes.data(), signed_bytes.size(), public_key.data() + 32, private_key);
        payload.insert(payload.end(), signature, signature + sizeof(signature));
    }

    std::vector<uint8_t> packet(reticulum::PacketContext context) const
    {
        std::vector<uint8_t> bytes(reticulum::kReticulumMtu);
        size_t length = bytes.size();
        assert(reticulum::buildHeader1Packet(reticulum::PacketType::Announce,
                                             reticulum::DestinationType::Single, context, false,
                                             destination.data(), payload.data(), payload.size(), bytes.data(), &length, 1));
        bytes.resize(length);
        return bytes;
    }
};

int main()
{
    AnnounceIngestor ingestor;
    DestinationRegistry peers;
    PathManager paths;
    LxmfIdentity local;
    AnnounceIngestOptions options;
    options.now_ms = 1000;
    options.now_s = 1;
    options.path_ttl_ms = 100000;
    options.pending_path_request_ttl_ms = 45000;
    options.max_transport_hops = 8;
    options.max_paths = 4;
    options.ingress_interface_id = 1;
    AnnounceIngestResult result;
    const auto receive = [&](const std::vector<uint8_t>& bytes)
    {
        reticulum::ParsedPacket packet;
        assert(reticulum::parsePacket(bytes.data(), bytes.size(), &packet));
        return ingestor.ingest(bytes.data(), bytes.size(), packet, local, peers, paths, options, &result);
    };
    SignedAnnounce original(10);
    auto announcement = original.packet(reticulum::PacketContext::None);
    assert(receive(announcement));
    assert(result.status == AnnounceIngestResult::Status::Accepted && peers.size() == 1);
    const auto* route = paths.findAnyPath(original.destination.data());
    assert(route);
    const auto route_age = route->updated_ms;
    const auto history_count = route->announce_random_blob_count;
    const auto cached = std::vector<uint8_t>(route->cached_announce, route->cached_announce + route->cached_announce_len);

    peers.clear(); // A directory recovery can outlive the valid route cache.
    paths.notePendingPathRequest(original.destination.data(), 1100, 4);
    options.now_ms = 1200;
    auto response = original.packet(reticulum::PacketContext::PathResponse);
    assert(receive(response));
    assert(result.status == AnnounceIngestResult::Status::Accepted && result.learned_peer);
    assert(std::memcmp(result.learned_peer->sig_pub, original.public_key.data() + 32, 32) == 0);
    assert(result.path->updated_ms == route_age && result.path->announce_random_blob_count == history_count);
    assert(cached.size() == result.path->cached_announce_len);
    assert(std::memcmp(cached.data(), result.path->cached_announce, cached.size()) == 0);
    assert(!paths.findPendingPathRequest(original.destination.data()));

    peers.clear();
    assert(receive(response)); // Unsolicited duplicate cannot repopulate peers.
    assert(result.status == AnnounceIngestResult::Status::Ignored && peers.size() == 0);
    paths.notePendingPathRequest(original.destination.data(), 1300, 4);
    options.now_ms = 50000;
    assert(receive(response));
    assert(result.status == AnnounceIngestResult::Status::Ignored && peers.size() == 0);

    // A still-valid historic signature must not roll current metadata back.
    SignedAnnounce current(11);
    options.now_ms = 51000;
    announcement = current.packet(reticulum::PacketContext::None);
    assert(receive(announcement));
    assert(result.status == AnnounceIngestResult::Status::Accepted);
    const auto current_age = result.path->updated_ms;
    peers.clear();
    paths.notePendingPathRequest(current.destination.data(), 51100, 4);
    options.now_ms = 51200;
    assert(receive(response));
    assert(result.status == AnnounceIngestResult::Status::Ignored && peers.size() == 0);
    assert(paths.findPendingPathRequest(current.destination.data()) &&
           !paths.findPendingPathRequest(current.destination.data())->resolved);
    response = current.packet(reticulum::PacketContext::PathResponse);
    assert(receive(response));
    assert(result.status == AnnounceIngestResult::Status::Accepted && peers.size() == 1);
    assert(result.path->updated_ms == current_age);

    peers.clear();
    paths.notePendingPathRequest(current.destination.data(), 51300, 4);
    options.now_ms = 51400;
    response.back() ^= 1; // Invalid real Ed25519 signature, despite a matching route.
    assert(!receive(response));
    assert(result.status == AnnounceIngestResult::Status::Rejected && peers.size() == 0);
    assert(paths.findPendingPathRequest(current.destination.data()) &&
           !paths.findPendingPathRequest(current.destination.data())->resolved);
    assert(paths.findAnyPath(current.destination.data())->updated_ms == current_age);
    response = current.packet(reticulum::PacketContext::PathResponse);
    options.now_ms = current_age + options.path_ttl_ms + 1;
    paths.notePendingPathRequest(current.destination.data(), options.now_ms, 4);
    assert(receive(response)); // A requested replay cannot revive an expired route.
    assert(result.status == AnnounceIngestResult::Status::Ignored && peers.size() == 0);
}
