#include "platform/linux/hostlink_config_snapshot.h"
#include "geocaching/protocol/cmp_reader.h"
#include "geocaching/protocol/cmp_writer.h"
#include "platform/linux/geocaching_crypto.h"
#include "platform/linux/tms_snapshot_bridge.h"
#include <algorithm>
#include <cstring>

namespace platform::linux_runtime
{
using Status = hostlink::management::Status;
bool HostlinkConfigSnapshot::supported()
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
    return tms_snapshot_available();
#else
    return false;
#endif
}
void HostlinkConfigSnapshot::close(const uint8_t* session)
{
    if (session && owned_ && std::memcmp(session, session_.data(), session_.size())) return;
    if (awaiting_) cancel_tms_snapshot();
    awaiting_ = owned_ = false;
    document_.clear();
    session_.fill(0);
    hash_.fill(0);
}
Status HostlinkConfigSnapshot::process(const hostlink::management::EnvelopeView& request,
                                       uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    if (!supported()) return Status::Unsupported;
    if (!request.session || !output || capacity < 384) return Status::InvalidField;
    geocaching::protocol::CmpReader reader({request.body, request.body_size});
    size_t fields = 0;
    uint64_t offset = 0, count = 0;
    if (!reader.array(fields, 3) || fields != 3 || !reader.unsignedInteger(offset) ||
        !reader.unsignedInteger(count) || !count || count > 320 || offset > UINT32_MAX) return Status::InvalidField;
    geocaching::ByteView expected;
    if (!reader.nil())
    {
        geocaching::protocol::CmpReader again({request.body, request.body_size});
        if (!again.array(fields, 3) || !again.unsignedInteger(offset) || !again.unsignedInteger(count) ||
            !again.binary(expected, 32) || expected.size != 32 || !again.finished()) return Status::InvalidField;
    }
    else if (!reader.finished()) return Status::InvalidField;
    if (offset && !expected.data) return Status::InvalidField;
    if (owned_ && std::memcmp(session_.data(), request.session, session_.size())) return Status::Unauthorized;
    if (!offset && !expected.data && !awaiting_)
    {
        owned_ = true;
        awaiting_ = true;
        std::copy_n(request.session, session_.size(), session_.begin());
        document_.clear();
    }
    if (awaiting_)
    {
        const auto state = request_tms_snapshot(document_);
        if (state == TmsSnapshotRead::Busy) return Status::Busy;
        awaiting_ = false;
        if (state != TmsSnapshotRead::Ready) return Status::StorageUnavailable;
        LinuxGeocachingCrypto crypto;
        if (!crypto.sha256({reinterpret_cast<const uint8_t*>(document_.data()), document_.size()}, hash_.data()))
        {
            document_.clear();
            return Status::Internal;
        }
    }
    if (!owned_ || document_.empty()) return Status::Expired;
    if (expected.data && std::memcmp(expected.data, hash_.data(), hash_.size())) return Status::Conflict;
    if (offset > document_.size()) return Status::InvalidField;
    const size_t length = std::min<size_t>(count, document_.size() - static_cast<size_t>(offset));
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!writer.array(4) || !writer.unsignedInteger(offset) || !writer.unsignedInteger(document_.size()) ||
        !writer.binary({hash_.data(), hash_.size()}) ||
        !writer.binary({reinterpret_cast<const uint8_t*>(document_.data() + offset), length})) return Status::Internal;
    written = writer.size();
    return Status::Ok;
}
} // namespace platform::linux_runtime
