#include "platform/linux/hostlink_track_publication.h"
#include "platform/linux/track_publication_runtime.h"
#include "track_sharing/commit_request.h"
#include "track_sharing/network_request.h"
namespace platform::linux_runtime
{
bool HostlinkTrackPublication::supported() const { return track_publication_available(database_); }
bool HostlinkTrackPublication::identityBusy() const { return track_publication_busy(database_); }
void HostlinkTrackPublication::close(const uint8_t* session) const { close_track_publication(database_, session); }
::hostlink::management::Status HostlinkTrackPublication::process(const ::hostlink::management::EnvelopeView& request, uint8_t* output, size_t capacity, size_t& written) const
{
    namespace management = ::hostlink::management;
    using Status = management::Status;
    written = 0;
    if (!request.session || !track_sharing::networkRequestId({request.session, 16}) || !output || capacity < 384) return Status::InvalidField;
    const bool commit = request.code == uint16_t(management::Operation::TrackCommit);
    geocaching::ByteView operation;
    uint32_t generation = 0;
    if (commit)
    {
        track_sharing::CommitRequest decoded;
        if (!track_sharing::decodeCommitRequest({request.body, request.body_size}, decoded)) return Status::InvalidField;
        operation = decoded.operation;
        generation = decoded.generation;
    }
    else
    {
        if (request.code != uint16_t(management::Operation::JobGet) && request.code != uint16_t(management::Operation::JobCancel)) return Status::Unsupported;
        geocaching::protocol::CmpReader reader({request.body, request.body_size});
        size_t fields = 0;
        uint64_t epoch = 0, type = 0;
        if (!reader.array(fields, 3) || fields != 3 || !reader.binary(operation, 16) || !track_sharing::networkRequestId(operation) || !reader.unsignedInteger(epoch) ||
            !epoch || epoch > UINT32_MAX || !reader.unsignedInteger(type) || type != 1 || !reader.finished()) return Status::InvalidField;
        generation = uint32_t(epoch);
    }
    TrackPublicationReply reply;
    const auto status = request_track_publication(database_, request, operation, generation, reply);
    if (status != Status::Ok) return status;
    if (!reply.valid) return Status::Internal;
    const auto& job = reply.job;
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!writer.array(commit ? 8 : 14) || !writer.binary({job.operation, 16}) || !writer.binary({job.object, 32}) || !writer.binary({job.revision_hash, 32}) ||
        !writer.unsignedInteger(job.generation) || !writer.unsignedInteger(job.revision) || !writer.binary({job.directory, 16}) || !writer.binary({job.directory_epoch, 16})) return Status::Internal;
    if (commit)
    {
        if (!writer.unsignedInteger(1)) return Status::Internal;
    }
    else if (!writer.binary({job.file_hash, 32}) || !writer.unsignedInteger(job.file_bytes) || !writer.unsignedInteger(job.offset) || !writer.unsignedInteger(uint8_t(job.phase)) ||
             !writer.unsignedInteger(job.sequence) || !writer.binary({job.author_hash, 32}) || !writer.unsignedInteger(reply.active)) return Status::Internal;
    written = writer.size();
    return Status::Ok;
}
} // namespace platform::linux_runtime
