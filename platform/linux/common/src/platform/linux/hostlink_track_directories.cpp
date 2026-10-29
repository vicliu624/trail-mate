#include "platform/linux/hostlink_track_directories.h"
#include "geocaching/protocol/cmp_reader.h"
#include "platform/linux/track_directory_runtime.h"

namespace platform::linux_runtime
{
bool HostlinkTrackDirectories::supported() const { return track_directories_available(database_); }
::hostlink::management::Status HostlinkTrackDirectories::process(const ::hostlink::management::EnvelopeView& request, uint8_t* output, size_t capacity, size_t& written) const
{
    namespace management = ::hostlink::management;
    using Status = management::Status;
    written = 0;
    if (!request.session || !output || !capacity) return Status::InvalidField;
    geocaching::protocol::CmpReader reader({request.body, request.body_size});
    size_t fields = 0;
    if (!reader.array(fields, 1) || fields != 1) return Status::InvalidField;
    if (request.code == uint16_t(management::Operation::TrackDirectoryList))
    {
        uint64_t cursor = 0;
        if (!reader.unsignedInteger(cursor) || cursor > 8 || !reader.finished()) return Status::InvalidField;
        return read_track_directories(database_, request.session, size_t(cursor), output, capacity, written);
    }
    if (request.code == uint16_t(management::Operation::TrackDirectoryDiscover))
    {
        geocaching::ByteView target;
        if (!reader.binary(target, 16) || !track_sharing::networkRequestId(target) || !reader.finished()) return Status::InvalidField;
        const auto status = request_track_directory(database_, request.session, target.data);
        if (status != Status::Ok) return status;
        output[0] = 0x90;
        written = 1;
        return Status::Ok;
    }
    return Status::Unsupported;
}
void HostlinkTrackDirectories::close(const uint8_t* session) const { close_track_directories(database_, session); }
} // namespace platform::linux_runtime
