#include "platform/esp/arduino_common/geocaching/sd_journal_segment.h"
#include <vector>

std::vector<uint8_t> data;
size_t bytes_read = 0, calls = 0, short_limit = SIZE_MAX;
namespace platform::esp::arduino_common::storage
{
class SdRuntimeFile::Impl { public: bool open = false; size_t offset = 0; };
SdRuntimeFile::SdRuntimeFile() : impl_(new Impl) {}
SdRuntimeFile::~SdRuntimeFile() { delete impl_; }
bool SdRuntimeFile::open(const char*, const char*) { impl_->open = true; impl_->offset = 0; return true; }
void SdRuntimeFile::close() { impl_->open = false; }
bool SdRuntimeFile::is_open() const { return impl_->open; }
uint64_t SdRuntimeFile::size() const { return data.size(); }
int SdRuntimeFile::read(void* out, size_t count)
{
    ++calls;
    const auto n = std::min(std::min(count, short_limit), data.size() - impl_->offset);
    std::memcpy(out, data.data() + impl_->offset, n); impl_->offset += n; bytes_read += n;
    return static_cast<int>(n);
}
}
bool exercise(size_t size, size_t read_limit)
{
    using namespace platform::esp::arduino_common::geocaching;
    std::vector<uint8_t> payload(size, 7), output(size + 24);
    ::geocaching::storage::RecordHeader header;
    if (!::geocaching::storage::makeRecordHeader(::geocaching::storage::RecordKind::Transaction, 1, {payload.data(), payload.size()}, header)) return false;
    data.assign(header.begin(), header.end()); data.insert(data.end(), payload.begin(), payload.end());
    bytes_read = calls = 0; short_limit = read_limit;
    SdJournalSegment segment;
    if (!segment.open(1)) return false;
    bool yielded = false;
    ::geocaching::storage::RecordFrameView frame;
    for (size_t iteration = 0; iteration < data.size() + 1; ++iteration)
    {
        const auto prior_bytes = bytes_read, prior_calls = calls;
        const auto result = segment.next(output.data(), output.size(), frame);
        if (bytes_read - prior_bytes > 512 || calls - prior_calls > 2) return false;
        if (result == SegmentReadResult::InProgress)
        {
            if (frame.payload.data || frame.payload.size) return false;
            yielded = true; continue;
        }
        return result == SegmentReadResult::Record && yielded && frame.payload.size == payload.size() &&
            !std::memcmp(frame.payload.data, payload.data(), payload.size()) && bytes_read == data.size();
    }
    return false;
}
int main()
{
    if (!exercise(65536, SIZE_MAX)) return 1;
    if (!exercise(2048, 13)) return 2;
    return 0;
}
