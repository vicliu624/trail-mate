#include "platform/esp/arduino_common/geocaching/sd_journal.h"
#include <vector>

std::vector<uint8_t> disk;
namespace platform::esp::arduino_common::storage
{
SdFileReadResult sd_read_file(const char*, uint8_t* buffer, size_t capacity)
{
    SdFileReadResult result;
    result.status = disk.empty() ? SdFileReadStatus::Missing : SdFileReadStatus::Ready;
    result.file_size = disk.size();
    result.bytes_read = std::min(capacity, disk.size());
    if (result.bytes_read) std::memcpy(buffer, disk.data(), result.bytes_read);
    return result;
}
}
int main()
{
    using namespace platform::esp::arduino_common::geocaching;
    const uint8_t payload[] = {0x93, 1, 0, 0x91, 0x93, 5, 0xc4, 1, 0xab, 0xc0};
    ::geocaching::storage::RecordHeader header;
    if (!::geocaching::storage::makeRecordHeader(::geocaching::storage::RecordKind::Transaction, 1, {payload, sizeof(payload)}, header)) return 1;
    disk.assign(header.begin(), header.end()); disk.insert(disk.end(), payload, payload + sizeof(payload));
    std::array<uint8_t, 128> buffer{};
    ::geocaching::storage::MutationView scratch;
    ::geocaching::storage::TransactionView out;
    if (readJournalTransaction(1, 0, buffer.data(), buffer.size(), &scratch, 1, out) != JournalReadResult::Parsed || out.count != 1) return 2;
    if (readJournalTransaction(2, 1, buffer.data(), buffer.size(), &scratch, 1, out) != JournalReadResult::Corrupt || out.count) return 3;
    if (readJournalTransaction(1, 0, buffer.data(), 24, &scratch, 1, out) != JournalReadResult::Corrupt) return 4;
    disk.back() ^= 1;
    if (readJournalTransaction(1, 0, buffer.data(), buffer.size(), &scratch, 1, out) != JournalReadResult::Corrupt) return 5;
    disk.clear();
    if (readJournalTransaction(1, 0, buffer.data(), buffer.size(), &scratch, 1, out) != JournalReadResult::Missing) return 6;
    return 0;
}
