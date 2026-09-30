#include "app/tms_config_codec.h"
#include "chat/domain/reticulum_network_config.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using NetworkConfig = chat::reticulum::ReticulumNetworkConfig;
using InterfaceConfig = chat::reticulum::NetworkInterfaceConfig;
using app::AppConfig;
namespace tms = app::tms;
#include "network_validation.inc"
NetworkConfig source;
namespace platform::ui::reticulum_network_config
{
bool validateForTms(const NetworkConfig& config) { return validate_config(config); }
bool snapshotForTms(const chat::MeshConfig&, NetworkConfig* out)
{
    *out = source;
    return true;
}
} // namespace platform::ui::reticulum_network_config
struct ParseState
{
    uint64_t network_seen = 0;
    uint32_t access_seen = 0;
    NetworkConfig network;
} s_state;
bool key_equals(const tms::RecordReader& reader, const char* key) { return !std::strcmp(reader.key(), key); }
#include "network_tms.inc"

std::string emit()
{
    std::string document;
    AppConfig app;
    tms::LineScratch scratch;
    assert(tms::writeDocument(
        app, tms::DocumentKind::Working,
        {&document, [](void* out, const char* text, size_t length)
         { static_cast<std::string*>(out)->append(text, length); return true; }},
        scratch, nullptr,
        [](void* config, tms::RecordWriter& writer)
        { return write_network_records(writer, *static_cast<AppConfig*>(config)); },
        &app));
    return document;
}
bool decode(const std::string& document)
{
    s_state = {};
    tms::Decoder reader(
        nullptr, tms::DocumentKind::Working,
        [](void*, const tms::RecordReader& record)
        { return consume_network_record(record); },
        nullptr,
        [](void*, bool, uint16_t version)
        { return valid_network_records(version); });
    size_t position = 0;
    while (position < document.size())
    {
        const auto end = document.find('\n', position);
        assert(end != std::string::npos && end - position < tms::kMaxLineBytes);
        char line[tms::kMaxLineBytes]{};
        std::memcpy(line, document.data() + position, end - position);
        if (!reader.consumeLine(line)) return false;
        position = end + 1;
    }
    return reader.finish();
}
int main()
{
    source.interface_count = 1;
    auto& entry = source.interfaces[0];
    std::strcpy(entry.id, "tcp");
    std::strcpy(entry.target_host, "example.test");
    entry.type = chat::reticulum::NetworkInterfaceType::TcpClient;
    entry.enabled = true;
    assert(decode(emit())); // Existing public documents have no access fields.
    assert(!s_state.network.interfaces[0].access.enabled());
    std::strcpy(entry.access.network_name, "name %: =\n");
    std::strcpy(entry.access.passphrase, "password %: =\n");
    entry.access.ifac_size_bits = 128;
    const auto document = emit();
    assert(decode(document));
    assert(s_state.network.interfaces[0].access == entry.access);
    assert(validate_config(s_state.network));
    const auto start = document.find("rt.net.interface.0.passphrase=");
    assert(start != std::string::npos);
    const auto end = document.find('\n', start) + 1;
    auto broken = document;
    broken.erase(start, end - start);
    assert(!decode(broken));
    broken = document;
    broken.insert(end, document.substr(start, end - start));
    assert(!decode(broken));
    for (auto bits : {0, 1, 7, 9, 513})
    {
        entry.access.ifac_size_bits = bits;
        assert(!validate_config(source));
        assert(!decode(emit()));
    }
    entry.access.ifac_size_bits = 128;
    entry.type = chat::reticulum::NetworkInterfaceType::IntegratedLoRa;
    assert(!validate_config(source)); // Until LoRa access framing is implemented.
    entry.type = chat::reticulum::NetworkInterfaceType::TcpClient;
    std::memset(entry.access.network_name, 'x', sizeof(entry.access.network_name));
    assert(!validate_config(source));
    entry.access = {};
    assert(validate_config(source) && decode(emit()));
}
