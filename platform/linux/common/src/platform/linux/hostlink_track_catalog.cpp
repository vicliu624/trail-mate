#include "platform/linux/hostlink_track_catalog.h"
#include "geocaching/protocol/cmp_reader.h"
#include "geocaching/protocol/cmp_writer.h"
#include "platform/linux/hostlink_upload_io.h"
#include <algorithm>
#include <array>

namespace platform::linux_runtime
{
namespace
{
bool valid_name(const std::string& name)
{
    if (name.empty() || name.size() > 127 || name.front() == '.') return false;
    for (const auto byte : name)
        if (uint8_t(byte) < 32 || byte == '/' || byte == '\\' || byte == ':') return false;
    for (size_t offset = 0; offset < name.size();)
    {
        const uint8_t first = uint8_t(name[offset++]);
        if (first < 128) continue;
        unsigned continuation = 0;
        uint32_t value = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf)
        {
            continuation = 1;
            value = first & 31;
            minimum = 0x80;
        }
        else if (first >= 0xe0 && first <= 0xef)
        {
            continuation = 2;
            value = first & 15;
            minimum = 0x800;
        }
        else if (first >= 0xf0 && first <= 0xf4)
        {
            continuation = 3;
            value = first & 7;
            minimum = 0x10000;
        }
        else return false;
        if (continuation > name.size() - offset) return false;
        while (continuation--)
        {
            const uint8_t next = uint8_t(name[offset++]);
            if ((next & 0xc0) != 0x80) return false;
            value = value << 6 | (next & 63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}
uint8_t format(const std::string& name)
{
    auto lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char byte)
                   { return char(byte >= 'A' && byte <= 'Z' ? byte + 32 : byte); });
    if (lower == "active.bin" || lower.size() < 4) return 255;
    const auto suffix = lower.substr(lower.size() - 4);
    if (suffix == ".kml") return 0;
    if (suffix == ".gpx") return 1;
    if (suffix == ".csv") return 2;
    if (suffix == ".bin") return 3;
    return 255;
}
} // namespace

::hostlink::management::Status HostlinkTrackCatalog::read(const ::hostlink::management::EnvelopeView& request,
                                                          uint8_t* output, size_t capacity, size_t& written) const
{
    using Status = ::hostlink::management::Status;
    written = 0;
    if (!output || !request.session) return Status::Internal;
    geocaching::protocol::CmpReader reader({request.body, request.body_size});
    size_t fields = 0;
    uint64_t wanted = 0, position = 0;
    if (!reader.array(fields, 1) || fields != 1) return Status::InvalidField;
    const bool first = reader.position() < request.body_size && request.body[reader.position()] == 0xc0;
    if (first)
    {
        if (!reader.nil()) return Status::InvalidField;
    }
    else if (!reader.array(fields, 2) || fields != 2 || !reader.unsignedInteger(wanted) || wanted > UINT32_MAX ||
             !reader.unsignedInteger(position) || position > 128U * 1024U * 1024U || position % 32) return Status::InvalidField;
    if (!reader.finished()) return Status::InvalidField;

    UploadIo io(root_);
    const std::array<uint8_t, 32> unused{};
    const uint32_t media = io.media_token(request.session, 0, unused.data(), "/tracks");
    if (!io.current() || !media) return Status::StorageUnavailable;
    if (!first && wanted != media) return Status::Conflict;
    std::array<UploadIo::ScanEntry, 4> entries;
    size_t count = 0;
    bool ended = false;
    const auto scan = io.scan("/tracks", uint32_t(position / 32), entries, count, ended, 128U * 1024U * 1024U / 32);
    if (scan == UploadIo::ScanStatus::Invalid) return Status::LimitExceeded;
    if (scan == UploadIo::ScanStatus::Missing) ended = true;
    else if (scan != UploadIo::ScanStatus::Ready) return Status::StorageUnavailable;

    std::string name;
    uint8_t kind = 255;
    uint64_t bytes = 0;
    bool active = false;
    for (size_t index = 0; index < count; ++index)
    {
        position += 32;
        const auto& entry = entries[index];
        if (!entry.regular || entry.name.empty() || entry.name.front() == '.') continue;
        kind = format(entry.name);
        if (kind == 255) continue;
        if (!valid_name(entry.name)) return Status::StorageUnavailable;
        const auto logical = "/tracks/" + entry.name;
        auto opened = io.open(logical.c_str(), UploadIo::Mode::Read);
        if (!opened.handle || !io.size(opened.handle, bytes)) return Status::StorageUnavailable;
        // The recorder holds LOCK_EX for its full lifetime. Observe via an
        // independent descriptor; never unlock or change that recorder's lock.
        active = !io.lock_open(opened.handle);
        if (!io.still_named(logical.c_str()) || !io.close(opened.handle)) return Status::StorageUnavailable;
        name = entry.name;
        // A row always has a continuation, including the final file. A later
        // request emits the explicit terminal page required by the web client.
        ended = false;
        break;
    }
    if (!io.current() || io.media_token(request.session, 0, unused.data(), "/tracks") != media) return Status::Conflict;
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!writer.array(3) || !writer.unsignedInteger(media) || !(ended ? writer.nil() : writer.unsignedInteger(position))) return Status::Internal;
    if (name.empty())
    {
        if (!writer.nil()) return Status::Internal;
    }
    else if (!writer.array(4) || !writer.text(name.c_str()) || !writer.unsignedInteger(kind) ||
             !writer.unsignedInteger(bytes) || !writer.unsignedInteger(active ? 1 : 0)) return Status::Internal;
    written = writer.size();
    return Status::Ok;
}
} // namespace platform::linux_runtime
