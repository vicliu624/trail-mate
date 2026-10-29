#include "chat/linux_reticulum_network_store.h"
#include "app/reticulum_network_records.h"
#include "platform/linux/settings_reset_storage.h"

#include <array>
#include <string>

namespace platform::linux_runtime
{
namespace
{
namespace tms = ::app::tms;
constexpr const char* kNamespace = ::platform::linux_runtime::settings_storage::kNetworkNamespace;
constexpr const char* kKey = "network_tms_v1";
struct State
{
    chat::reticulum::ReticulumNetworkConfig network;
    uint64_t seen = 0;
    uint32_t access_seen = 0;
    tms::ReticulumNetworkRecords records() { return {network, seen, access_seen}; }
};
} // namespace

LinuxReticulumNetworkStore::Result LinuxReticulumNetworkStore::load(chat::reticulum::ReticulumNetworkConfig& output)
{
    std::array<char, tms::kMaxDocumentBytes> document{};
    size_t length = 0;
    const auto read = ::platform::ui::settings_store::read_blob_into(kNamespace, kKey, document.data(), document.size(), &length);
    if (read != Result::Ok) return read;
    return decode(document.data(), length, output) ? Result::Ok : Result::Invalid;
}

bool LinuxReticulumNetworkStore::decode(const char* document, size_t length, chat::reticulum::ReticulumNetworkConfig& output)
{
    if (!document || !length || length > tms::kMaxDocumentBytes || std::memchr(document, 0, length)) return false;
    State candidate;
    tms::Decoder decoder(
        nullptr, tms::DocumentKind::Working,
        [](void* context, const tms::RecordReader& reader)
        { return static_cast<State*>(context)->records().consume_network_record(reader); },
        &candidate,
        [](void* context, bool, uint16_t version)
        { return static_cast<State*>(context)->records().valid_network_records(version); });
    size_t cursor = 0;
    while (cursor < length)
    {
        const auto* newline = static_cast<const char*>(std::memchr(document + cursor, '\n', length - cursor));
        const size_t end = newline ? size_t(newline - document) : length;
        if (end - cursor >= tms::kMaxLineBytes) return false;
        tms::LineScratch scratch{};
        std::memcpy(scratch.bytes, document + cursor, end - cursor);
        if (!decoder.consumeLine(scratch.bytes)) return false;
        cursor = end + 1;
    }
    if (!decoder.finish() || !chat::reticulum::validateNetworkConfig(candidate.network)) return false;
    output = candidate.network;
    return true;
}

bool LinuxReticulumNetworkStore::save(const chat::reticulum::ReticulumNetworkConfig& network, const app::AppConfig& config)
{
    std::string document;
    return encode(network, config, document) &&
           ::platform::ui::settings_store::put_blob(kNamespace, kKey, document.data(), document.size());
}

bool LinuxReticulumNetworkStore::encode(const chat::reticulum::ReticulumNetworkConfig& network, const app::AppConfig& config,
                                        std::string& output)
{
    if (!chat::reticulum::validateNetworkConfig(network)) return false;
    State state{network};
    // Inactive slots are not durable configuration and must not retain stale
    // credentials or unchecked unused text after the interface list shrinks.
    for (size_t index = network.interface_count; index < chat::reticulum::kMaxNetworkInterfaces; ++index)
        state.network.interfaces[index] = {};
    for (const auto& entry : state.network.interfaces)
    {
        if (!std::memchr(entry.id, 0, sizeof(entry.id)) || !std::memchr(entry.target_host, 0, sizeof(entry.target_host)) ||
            !std::memchr(entry.group_id, 0, sizeof(entry.group_id)) ||
            !std::memchr(entry.access.network_name, 0, sizeof(entry.access.network_name)) ||
            !std::memchr(entry.access.passphrase, 0, sizeof(entry.access.passphrase))) return false;
    }
    std::string document;
    tms::LineScratch scratch;
    const bool encoded = tms::writeDocument(
        config, tms::DocumentKind::Working,
        {&document, [](void* context, const char* bytes, size_t length)
         {
             auto& text = *static_cast<std::string*>(context);
             if (length > tms::kMaxDocumentBytes - text.size()) return false;
             text.append(bytes, length);
             return true;
         }},
        scratch, nullptr, [](void* context, tms::RecordWriter& writer)
        { return static_cast<State*>(context)->records().write_network_records(writer); },
        &state);
    if (!encoded) return false;
    output = std::move(document);
    return true;
}
} // namespace platform::linux_runtime
