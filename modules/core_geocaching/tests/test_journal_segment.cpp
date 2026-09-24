#include "platform/esp/arduino_common/geocaching/sd_journal_segment.h"
#include <vector>
std::vector<uint8_t> data;
namespace platform::esp::arduino_common::storage
{
class SdRuntimeFile::Impl
{
  public:
    size_t offset = 0;
    bool open = false;
};
SdRuntimeFile::SdRuntimeFile() : impl_(new Impl) {}
SdRuntimeFile::~SdRuntimeFile() { delete impl_; }
bool SdRuntimeFile::open(const char*, const char*)
{
    impl_->open = true;
    impl_->offset = 0;
    return true;
}
void SdRuntimeFile::close() { impl_->open = false; }
bool SdRuntimeFile::is_open() const { return impl_->open; }
uint64_t SdRuntimeFile::size() const { return data.size(); }
int SdRuntimeFile::read(void* buffer, size_t count)
{
    const auto n = std::min(count, data.size() - impl_->offset);
    std::memcpy(buffer, data.data() + impl_->offset, n);
    impl_->offset += n;
    return static_cast<int>(n);
}
} // namespace platform::esp::arduino_common::storage
int main()
{
    using namespace platform::esp::arduino_common::geocaching;
    const uint8_t payload[] = {0x93, 1, 0, 0x90};
    for (uint64_t sequence = 1; sequence <= 2; ++sequence)
    {
        ::geocaching::storage::RecordHeader header;
        if (!::geocaching::storage::makeRecordHeader(::geocaching::storage::RecordKind::Transaction, sequence, {payload, sizeof(payload)}, header)) return 1;
        data.insert(data.end(), header.begin(), header.end());
        data.insert(data.end(), payload, payload + sizeof(payload));
    }
    SdJournalSegment segment;
    uint8_t buffer[128];
    ::geocaching::storage::RecordFrameView frame;
    if (!segment.open(1) || segment.next(buffer, sizeof(buffer), frame) != SegmentReadResult::Record || frame.sequence != 1 ||
        segment.next(buffer, sizeof(buffer), frame) != SegmentReadResult::Record || frame.sequence != 2 ||
        segment.next(buffer, sizeof(buffer), frame) != SegmentReadResult::End) return 2;
    data.pop_back();
    if (!segment.open(1) || segment.next(buffer, sizeof(buffer), frame) != SegmentReadResult::Record ||
        segment.next(buffer, sizeof(buffer), frame) != SegmentReadResult::Truncated || frame.payload.data ||
        segment.next(buffer, sizeof(buffer), frame) != SegmentReadResult::Truncated) return 3;
    return 0;
}
