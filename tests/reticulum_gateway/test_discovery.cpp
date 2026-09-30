#include "chat/infra/reticulum/interface_discovery.h"
#include "chat/infra/reticulum/public_gateway_host.h"

#include <cassert>
#include <cstring>
#include <vector>

using chat::reticulum::InterfaceDiscoveryView;
using chat::reticulum::parseInterfaceDiscovery;
using Bytes = std::vector<uint8_t>;

static void text(Bytes& out, const char* value)
{
    const size_t size = std::strlen(value);
    if (size < 32) out.push_back(static_cast<uint8_t>(0xa0 | size));
    else
    {
        out.push_back(0xd9);
        out.push_back(static_cast<uint8_t>(size));
    }
    out.insert(out.end(), value, value + size);
}

static Bytes announce(const char* host = "node.example.org", const char* type = "TCPServerInterface",
                      uint32_t port = 4242, uint8_t transport = 0xc3, const char* ifac = "")
{
    // Native Discovery.py integer-keyed map, including optional metadata and
    // nil locations. An all-zero stamp is intentional: this is a parser test,
    // and the result MUST NOT bypass the separate cryptographic admission gate.
    Bytes out{0, 0x8d, 0};
    text(out, type);
    out.insert(out.end(), {1, transport, 2});
    text(out, host);
    out.insert(out.end(), {3, 0xc0, 4, 0xc0, 5, 0xc0, 6, 0xce,
                           static_cast<uint8_t>(port >> 24), static_cast<uint8_t>(port >> 16),
                           static_cast<uint8_t>(port >> 8), static_cast<uint8_t>(port), 7});
    text(out, ifac);
    out.insert(out.end(), {8, 0xc0, 0xcc, 254, 0xc4, 16});
    out.insert(out.end(), 16, 0x42);
    out.insert(out.end(), {0xcc, 255});
    text(out, "Public gateway");
    out.insert(out.end(), {0xcc, 253});
    text(out, "Reticulum");
    out.insert(out.end(), {0xcc, 252});
    text(out, "1.0");
    out.insert(out.end(), 32, 0);
    return out;
}

static bool parse(const Bytes& bytes)
{
    InterfaceDiscoveryView result;
    return parseInterfaceDiscovery(bytes.data(), bytes.size(), result);
}

int main()
{
    for (const char* host : {"192.168.10.2", "10.1.2.3", "172.16.0.1", "172.31.255.254", "127.0.0.1", "169.254.1.2", "100.64.0.1", "224.0.0.1", "0.0.0.0", "192.0.2.1", "198.18.0.1", "198.51.100.1", "203.0.113.1", "999.1.2.3", "123", "gateway.local", "gateway.LOCAL", "node.home.arpa", "localhost"})
        assert(!chat::reticulum::publicGatewayHost(host, std::strlen(host)));
    for (const char* host : {"1.1.1.1", "172.32.0.1", "100.128.0.1", "192.169.0.1", "rns.arborisis.net", "node.example.org"})
        assert(chat::reticulum::publicGatewayHost(host, std::strlen(host)));
    const auto packet = announce();
    InterfaceDiscoveryView view;
    assert(parseInterfaceDiscovery(packet.data(), packet.size(), view));
    assert(view.port == 4242 && view.host_size == 16);
    assert(std::memcmp(view.host, "node.example.org", view.host_size) == 0);
    assert(view.packed == packet.data() + 1 && view.packed_size == packet.size() - 33);
    assert(view.stamp == packet.data() + packet.size() - 32 && view.transport_id[0] == 0x42);
    assert(parse(announce("192.0.2.1", "BackboneInterface", 65535)));
    assert(!parse(announce("node.example.org", "TCPClientInterface")));
    assert(!parse(announce("node.example.org", "TCPServerInterface", 0)));
    assert(!parse(announce("node.example.org", "TCPServerInterface", 65536)));
    assert(!parse(announce("node.example.org", "TCPServerInterface", 4242, 0xc2)));
    assert(!parse(announce("node.example.org", "TCPServerInterface", 4242, 1)));
    assert(!parse(announce("node.example.org", "TCPServerInterface", 4242, 0xc3, "private")));
    for (const char* host : {"", "-bad.org", "bad-.org", ".bad.org", "bad..org", "bad.org/route", "bad.org:80", "bad org", "::1"})
        assert(!parse(announce(host)));
    auto changed = packet;
    changed[0] = 2; // Encrypted discovery needs a network identity.
    assert(!parse(changed));
    changed[0] = 0x80;
    assert(!parse(changed));
    changed = packet;
    changed.insert(changed.end() - 32, 0xc0); // Trailing data before stamp.
    assert(!parse(changed));
    changed = packet;
    changed[1] = 0x8e;
    changed.insert(changed.end() - 32, {1, 0xc3}); // Duplicate required key.
    assert(!parse(changed));
    changed = packet;
    changed[1] = 0xde;
    changed.insert(changed.begin() + 2, {0, 13});
    assert(parse(changed)); // map16 encoding, also emitted by larger maps.
    for (size_t size = 0; size < packet.size(); ++size)
    {
        view.port = 123;
        assert(!parseInterfaceDiscovery(packet.data(), size, view));
        assert(view.port == 0 && view.host == nullptr);
    }
    assert(!parseInterfaceDiscovery(nullptr, 100, view));
    Bytes oversized(501, 0);
    assert(!parse(oversized));
    changed = packet;
    changed[1] = 0xde;
    changed.insert(changed.begin() + 2, {0xff, 0xff});
    assert(!parse(changed)); // Bounded work regardless of advertised map count.
    return 0;
}
