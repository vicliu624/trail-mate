#include "app/linux_tms_document.h"
#include <cstring>

namespace trailmate::linux_app
{
namespace tms = app::tms;

tms::RecordConsumeResult LinuxTmsDocument::consume(void* context, const tms::RecordReader& reader)
{
    auto& staged = *static_cast<LinuxTmsDocument*>(context);
    auto result = staged.settings.consume(reader);
    if (result != tms::RecordConsumeResult::Unhandled) return result;
    result = staged.network_records().consume_network_record(reader);
    if (result != tms::RecordConsumeResult::Unhandled) return result;
    result = staged.compatibility.consumeRecord(reader);
    if (result != tms::RecordConsumeResult::Unhandled) return result;
    return tms::c6::consume(staged.companion, reader);
}

bool LinuxTmsDocument::finish(void* context, bool, uint16_t version)
{
    auto& staged = *static_cast<LinuxTmsDocument*>(context);
    return version == tms::kSchemaVersion && staged.settings.valid() &&
           staged.network_records().valid_network_records(version) &&
           chat::reticulum::validateNetworkConfig(staged.network) && staged.compatibility.valid_state() &&
           (!staged.companion.seen || tms::c6::valid(staged.companion, staged.settings.wifi_enabled));
}

std::unique_ptr<LinuxTmsDocument> LinuxTmsDocument::parse(const char* bytes, size_t length, tms::DocumentKind kind)
{
    if (!bytes || !length || length > tms::kMaxDocumentBytes || std::memchr(bytes, 0, length)) return nullptr;
    auto staged = std::make_unique<LinuxTmsDocument>();
    tms::Decoder decoder(&staged->config, kind, consume, staged.get(), finish);
    size_t cursor = 0;
    while (cursor < length)
    {
        const auto* newline = static_cast<const char*>(std::memchr(bytes + cursor, '\n', length - cursor));
        const size_t end = newline ? size_t(newline - bytes) : length;
        if (end - cursor >= tms::kMaxLineBytes) return nullptr;
        tms::LineScratch scratch{};
        std::memcpy(scratch.bytes, bytes + cursor, end - cursor);
        if (!decoder.consumeLine(scratch.bytes)) return nullptr;
        cursor = end + 1;
    }
    return decoder.finish() ? std::move(staged) : nullptr;
}

bool LinuxTmsDocument::emit(void* context, tms::RecordWriter& writer)
{
    auto& staged = *static_cast<LinuxTmsDocument*>(context);
    return staged.settings.write(writer) && staged.network_records().write_network_records(writer) &&
           staged.compatibility.writeRecords(writer) &&
           (!staged.companion.seen || tms::c6::write(staged.companion, writer));
}

bool LinuxTmsDocument::write(tms::DocumentKind kind, std::string& output)
{
    std::string candidate;
    tms::LineScratch scratch{};
    const bool ok = tms::writeDocument(config, kind,
                                       {&candidate, [](void* context, const char* bytes, size_t length)
                                        {
                                            auto& text = *static_cast<std::string*>(context);
                                            if (length > tms::kMaxDocumentBytes - text.size()) return false;
                                            text.append(bytes, length);
                                            return true;
                                        }},
                                       scratch, nullptr, emit, this);
    // Validate the whole result, including required blocks and scalar ranges;
    // a writer success alone does not establish complete-document validity.
    if (!ok || !parse(candidate.data(), candidate.size(), kind)) return false;
    output = std::move(candidate);
    return true;
}
} // namespace trailmate::linux_app
