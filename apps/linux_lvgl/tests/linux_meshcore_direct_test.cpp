#include "chat/infra/meshcore/meshcore_payload_helpers.h"
#include "chat/linux_meshcore_direct_state.h"
#include "chat/linux_meshcore_direct_wire.h"
#include "chat/linux_meshcore_direct_routes.h"
#include "platform/linux/runtime_paths.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sqlite3.h>
#include <unistd.h>

namespace wire = trailmate::linux_app::meshcore_wire;
namespace mc = chat::meshcore;
void install(const std::filesystem::path& root, const char* seed, const char* pub)
{
    std::filesystem::create_directories(root);
    setenv("TRAIL_MATE_SETTINGS_ROOT", root.c_str(), 1);
    sqlite3* db = nullptr;
    assert(sqlite3_open(platform::linux_runtime::sqlite_database_path().c_str(), &db) == SQLITE_OK);
    const auto sql = std::string("CREATE TABLE linux_protocol_identity(protocol TEXT PRIMARY KEY,seed BLOB NOT NULL,public_key BLOB NOT NULL); INSERT INTO linux_protocol_identity VALUES('meshcore',X'") + seed + "',X'" + pub + "');";
    assert(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
}
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-mc-direct-" + std::to_string(getpid()));
    install(root / "a", "000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F",
            "03A107BFF3CE10BE1D70DD18E74BC09967E4D6309BA50D5F1DDC8664125531B8");
    wire::Identity a;
    assert(a.begin());
    install(root / "b", "202122232425262728292A2B2C2D2E2F303132333435363738393A3B3C3D3E3F",
            "29ACBAE141BCCAF0B22E1A94D34D0BC7361E526D0BFE12C89794BC9322966DD7");
    wire::Identity b;
    assert(b.begin());
    // Independent Python cryptography Ed25519->X25519/AES/HMAC/SHA256 vectors.
    const char* golden[] = {
        "09002903890EB60620C28CE41021C64AE95FDC6A49EE4A4D4DA9F4B4B3C09BA69CE5A4D6E3DE",
        "494029AC03A1890E1D70B60620C28CE41021C64AE95FDC6A49EE4A4D4DA9F4B4B3C09BA69CE5A4D6E3DE"};
    unsigned version = 0;
    for (auto profile : {mc::PayloadProfile::V1, mc::PayloadProfile::V2})
    {
        wire::Frame frame, reply;
        uint32_t ack = 0;
        assert(wire::encodeDirectText(a, b.publicKey(), profile, "private hello", 0x12345678, frame, ack));
        assert(mc::toHex(frame.bytes.data(), frame.size) == golden[version++]);
        assert(ack == 0x4CF85079);
        wire::DirectIncoming incoming;
        assert(wire::decodeDirect(b, a.publicKey(), frame.bytes.data(), frame.size, incoming));
        assert(incoming.kind == wire::DirectKind::Text && incoming.want_ack && incoming.ack == ack);
        assert(std::string(reinterpret_cast<const char*>(incoming.payload.data()), incoming.size) == "private hello");
        assert(!wire::decodeDirect(a, b.publicKey(), frame.bytes.data(), frame.size, incoming));
        auto corrupt = frame;
        corrupt.bytes[corrupt.size - 1] ^= 1;
        assert(!wire::decodeDirect(b, a.publicKey(), corrupt.bytes.data(), corrupt.size, incoming));
        assert(wire::decodeDirect(b, a.publicKey(), frame.bytes.data(), frame.size, incoming));
        incoming.ack = 0x1234; // Zero high bytes must survive PATH padding removal.
        incoming.return_path_size = profile == mc::PayloadProfile::V2 ? 4 : 2;
        incoming.return_path = {0x12, 0x34, 0x56, 0x78};
        assert(wire::encodeDirectAck(b, a.publicKey(), incoming, reply));
        assert(wire::decodeDirect(a, b.publicKey(), reply.bytes.data(), reply.size, incoming));
        assert(incoming.kind == wire::DirectKind::Ack && incoming.ack == 0x1234);
        assert(incoming.has_advertised_path);
        const auto path_size = profile == mc::PayloadProfile::V2 ? 4U : 2U;
        assert(incoming.advertised_path_size == path_size);
        const uint8_t path[] = {0x12, 0x34, 0x56, 0x78};
        assert(!std::memcmp(incoming.advertised_path.data(), path, path_size));
        assert(incoming.return_path_size == 0); // Outer flood path is separate.
        wire::DirectRoutes routes;
        assert(routes.remember(b.publicKey(), incoming, 0xFFFFFFF0));
        auto cached = frame;
        assert(routes.apply(b.publicKey(), profile, 0x20, cached));
        assert(!routes.apply(a.publicKey(), profile, 0x20, cached));
        assert(!routes.apply(b.publicKey(), profile, uint32_t(0xFFFFFFF0 + wire::DirectRoutes::lifetime_ms), cached));
        auto advertised = incoming;
        advertised.has_advertised_path = false;
        assert(!routes.remember(b.publicKey(), advertised, 100));
        advertised.has_advertised_path = true;
        advertised.advertised_path_size = 65;
        assert(!routes.remember(b.publicKey(), advertised, 100));
        advertised.advertised_path_size = 0;
        assert(routes.remember(b.publicKey(), advertised, 100));
        assert(routes.apply(b.publicKey(), profile, 101, cached));
        assert(!routes.apply(b.publicKey(), profile == mc::PayloadProfile::V1 ? mc::PayloadProfile::V2 : mc::PayloadProfile::V1, 101, cached));
        routes.forget(b.publicKey());
        assert(!routes.apply(b.publicKey(), profile, 102, cached));
        for (uint8_t i = 0; i < 33; ++i)
        {
            auto peer = b.publicKey();
            peer[0] = i;
            assert(routes.remember(peer, advertised, 200 + i));
        }
        auto oldest = b.publicKey();
        oldest[0] = 0;
        assert(!routes.apply(oldest, profile, 240, cached));
        oldest[0] = 1;
        assert(routes.apply(oldest, profile, 240, cached));
        assert(wire::applyDirectPath(frame, profile, path, path_size));
        mc::ParsedPacket routed;
        assert(mc::parsePacket(frame.bytes.data(), frame.size, &routed));
        assert(routed.route_type == 2 && routed.path_len == path_size);
        assert(!std::memcmp(routed.path, path, path_size));
        assert(wire::decodeDirect(b, a.publicKey(), frame.bytes.data(), frame.size, incoming));
        assert(!incoming.has_advertised_path && incoming.ack == ack);
        const auto before = frame;
        assert(!wire::applyDirectPath(frame, profile, nullptr, 1));
        assert(frame.size == before.size && frame.bytes == before.bytes);
        assert(wire::applyDirectPath(frame, profile, nullptr, 0)); // Direct peer, no relays.
        const uint8_t data[] = {1, 0, 2, 3};
        assert(wire::encodeDirectData(a, b.publicKey(), profile, 123, data, sizeof(data), true, frame, ack));
        assert(ack);
        assert(wire::decodeDirect(b, a.publicKey(), frame.bytes.data(), frame.size, incoming));
        assert(incoming.kind == wire::DirectKind::Data && incoming.port == 123 && incoming.ack == ack);
        assert(incoming.size == sizeof(data) && !std::memcmp(incoming.payload.data(), data, sizeof(data)));
        assert(!wire::encodeDirectText(a, b.publicKey(), profile, std::string(200, 'x'), 1, frame, ack));
        assert(!wire::encodeDirectText(a, b.publicKey(), profile, std::string("x\0y", 3), 1, frame, ack));
        assert(!wire::encodeDirectText(a, a.publicKey(), profile, "self", 1, frame, ack));
        const uint8_t ambiguous[] = {1, 0};
        assert(!wire::encodeDirectData(a, b.publicKey(), profile, 123, ambiguous, 2, true, frame, ack));
    }
    uint8_t padded[16] = {0, 3, 0x34, 0x12};
    assert(mc::trimPeerPlaintextPadding(8, padded, sizeof(padded)) == 6);
    padded[15] = 1;
    assert(mc::trimPeerPlaintextPadding(8, padded, sizeof(padded)) == 0);
    wire::Frame fuzz;
    uint32_t fuzz_ack;
    assert(wire::encodeDirectText(a, b.publicKey(), mc::PayloadProfile::V2, "bounds", 1, fuzz, fuzz_ack));
    for (std::size_t size = 0; size <= 256; ++size)
    {
        wire::DirectIncoming ignored;
        (void)wire::decodeDirect(b, a.publicKey(), fuzz.bytes.data(), size, ignored);
    }
    wire::DirectState state;
    wire::DirectOutcome result;
    assert(state.track(1, 10, 100, 0xFFFFFFF0));
    assert(!state.acknowledge(2, 10, 0x20, result));
    assert(state.acknowledge(1, 10, 0x20, result) && result.message == 100);
    assert(!state.acknowledge(1, 10, 0x21, result));
    assert(state.track(1, 11, 101, 0) && state.track(2, 11, 102, 0));
    assert(!state.acknowledgeLegacy(11, 1, result));
    assert(state.acknowledge(1, 11, 1, result));
    assert(state.acknowledgeLegacy(11, 1, result) && result.message == 102);
    assert(!state.acknowledgeLegacy(11, 1, result));
    assert(state.track(1, 20, 200, 0));
    assert(!state.canTrack(1, 20, 201));
    assert(state.track(1, 21, 200, 10)); // Resending the same message replaces its old ACK.
    assert(!state.acknowledge(1, 20, 100, result));
    assert(state.acknowledge(1, 21, 100, result) && result.message == 200);
    for (uint32_t i = 1; i <= 32; ++i) assert(state.track(1, i, i, 0));
    assert(!state.canTrack(1, 33, 33));
    unsigned expired = 0;
    while (state.expire(15000, result)) ++expired;
    assert(expired == 32 && !state.acknowledge(1, 1, 15001, result));
    wire::Frame frame;
    frame.size = 1;
    for (uint32_t i = 1; i <= 8; ++i) assert(state.enqueue(1, i, frame, 0));
    assert(!state.enqueue(1, 9, frame, 0));
    assert(!state.takeReply(119, true, frame) && !state.takeReply(120, false, frame));
    unsigned replies = 0;
    while (state.takeReply(120, true, frame)) ++replies;
    assert(replies == 8);
    frame.size = 1;
    assert(state.enqueue(1, 1, frame, 0));
    assert(!state.takeReply(15000, true, frame));
    assert(state.track(1, 1, 1, 0) && state.cancel(result) && result.message == 1);
    assert(!state.cancel(result));
    std::filesystem::remove_all(root);
}
