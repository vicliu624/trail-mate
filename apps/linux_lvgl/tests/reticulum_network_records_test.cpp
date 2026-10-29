#include "app/reticulum_network_records.h"

#include <cassert>
#include <cstdio>
#include <string>

namespace tms = app::tms;
namespace r = chat::reticulum;

struct NetworkDocument
{
    r::ReticulumNetworkConfig network;
    uint64_t seen = 0;
    uint32_t access_seen = 0;
};

std::string encode(r::ReticulumNetworkConfig network)
{
    NetworkDocument state{network};
    app::AppConfig config;
    tms::LineScratch scratch;
    std::string output;
    assert(tms::writeDocument(
        config, tms::DocumentKind::Working,
        {&output, [](void* context, const char* bytes, size_t length)
         { static_cast<std::string*>(context)->append(bytes, length); return true; }},
        scratch, nullptr,
        [](void* context, tms::RecordWriter& writer)
        {
            auto& state = *static_cast<NetworkDocument*>(context);
            return tms::ReticulumNetworkRecords(state.network, state.seen, state.access_seen).write_network_records(writer);
        },
        &state));
    return output;
}

bool decode(const std::string& document, r::ReticulumNetworkConfig& output)
{
    NetworkDocument candidate;
    tms::Decoder decoder(
        nullptr, tms::DocumentKind::Working,
        [](void* context, const tms::RecordReader& reader)
        {
            auto& state = *static_cast<NetworkDocument*>(context);
            return tms::ReticulumNetworkRecords(state.network, state.seen, state.access_seen).consume_network_record(reader);
        },
        &candidate,
        [](void* context, bool, uint16_t version)
        {
            auto& state = *static_cast<NetworkDocument*>(context);
            return tms::ReticulumNetworkRecords(state.network, state.seen, state.access_seen).valid_network_records(version);
        });
    size_t cursor = 0;
    while (cursor < document.size())
    {
        const auto end = document.find('\n', cursor);
        const auto length = (end == std::string::npos ? document.size() : end) - cursor;
        if (length >= tms::kMaxLineBytes) return false;
        tms::LineScratch scratch{};
        std::memcpy(scratch.bytes, document.data() + cursor, length);
        if (!decoder.consumeLine(scratch.bytes)) return false;
        if (end == std::string::npos) break;
        cursor = end + 1;
    }
    if (!decoder.finish()) return false;
    output = candidate.network;
    return true;
}

int main()
{
    r::ReticulumNetworkConfig source, output;
    assert(decode(encode(source), output) && output.interface_count == 0);
    source.interface_count = 5;
    for (size_t i = 0; i < source.interface_count; ++i)
    {
        auto& entry = source.interfaces[i];
        std::snprintf(entry.id, sizeof(entry.id), "config-%zu", i);
        entry.enabled = i != 4;
        entry.type = i == 0 ? r::NetworkInterfaceType::IntegratedLoRa : i == 1 ? r::NetworkInterfaceType::Auto
                                                                               : r::NetworkInterfaceType::TcpClient;
        std::snprintf(entry.target_host, sizeof(entry.target_host), "gateway-%zu.example.test", i);
        entry.target_port = uint16_t(5000 + i);
    }
    auto& secured = source.interfaces[2].access;
    std::strcpy(secured.network_name, "network %:=\n");
    std::strcpy(secured.passphrase, "secret %:=\n\xe4\xb8\xad\xe6\x96\x87");
    secured.ifac_size_bits = 512;
    source.propagation.automatic_node = false;
    source.propagation.node_hash[15] = 7;
    source.propagation.delivery = r::LxmfDeliveryPreference::Propagated;
    source.propagation.sync_interval_s = 86400;
    source.propagation.max_messages_per_sync = 64;
    const auto document = encode(source);
    assert(decode(document, output));
    assert(encode(output) == document);
    assert(output.interface_count == 5 && !output.interfaces[4].enabled);
    assert(output.interfaces[2].access == secured && output.propagation.node_hash[15] == 7);

    // Every network record is required exactly once, except each optional IFAC
    // triple, which must be absent or complete. Admission must remain atomic.
    size_t tested = 0, cursor = 0;
    while ((cursor = document.find("rt.net.", cursor)) != std::string::npos)
    {
        const auto end = document.find('\n', cursor) + 1;
        const auto line = document.substr(cursor, end - cursor);
        auto missing = document;
        missing.erase(cursor, end - cursor);
        assert(!decode(missing, output));
        assert(encode(output) == document);
        auto duplicate = document;
        duplicate.insert(end, line);
        assert(!decode(duplicate, output));
        assert(encode(output) == document);
        ++tested;
        cursor = end;
    }
    assert(tested == 61);
    source = r::ReticulumNetworkConfig{};
    assert(decode(encode(source), output) && output.interface_count == 0);
    assert(!decode(document.substr(0, document.size() / 2), output));
    assert(output.interface_count == 0);
    std::puts("Shared TMS network records: three endpoints, IFAC, propagation, empty list and strict atomic admission passed");
}
