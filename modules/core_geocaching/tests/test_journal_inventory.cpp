#include "platform/esp/arduino_common/geocaching/sd_journal_inventory.h"
#include <algorithm>
#include <string>
#include <vector>

std::vector<std::string> names;
bool busy_once = false;
bool fail_after_first = false;
namespace platform::esp::arduino_common::storage
{
class SdRuntimeDir::Impl
{
  public:
    bool open = false;
    size_t index = 0;
};
SdRuntimeDir::SdRuntimeDir() : impl_(new Impl) {}
SdRuntimeDir::~SdRuntimeDir() { delete impl_; }
bool SdRuntimeDir::open(const char*)
{
    impl_->open = true;
    impl_->index = 0;
    return true;
}
void SdRuntimeDir::close() { impl_->open = false; }
bool SdRuntimeDir::is_open() const { return impl_->open; }
SdDirReadStatus SdRuntimeDir::read_next_status(char* name, size_t capacity, bool* is_dir)
{
    if (busy_once)
    {
        busy_once = false;
        return SdDirReadStatus::Busy;
    }
    if (fail_after_first && impl_->index == 1)
    {
        fail_after_first = false;
        ++impl_->index; // A failed read may leave the device cursor uncertain.
        return SdDirReadStatus::IoError;
    }
    if (impl_->index == names.size()) return SdDirReadStatus::End;
    std::snprintf(name, capacity, "%s", names[impl_->index++].c_str());
    *is_dir = false;
    return SdDirReadStatus::Entry;
}
SdFileReadResult sd_read_file(const char*, uint8_t* buffer, size_t capacity)
{
    const auto header = ::geocaching::storage::encodeVolumeHeader({});
    SdFileReadResult result;
    result.status = SdFileReadStatus::Ready;
    result.file_size = header.size();
    result.bytes_read = std::min(capacity, header.size());
    std::memcpy(buffer, header.data(), result.bytes_read);
    return result;
}
} // namespace platform::esp::arduino_common::storage
int main()
{
    using namespace platform::esp::arduino_common::geocaching;
    names = {"0000000000000002.gcj", "0000000000000001.gcj"};
    SdJournalInventory valid({}, 0);
    if (valid.step() != InventoryStep::Scanning || valid.range().complete ||
        valid.step() != InventoryStep::Scanning || valid.step() != InventoryStep::Complete || valid.range().last_start != 2) return 1;
    names = {"0000000000000002.gcj"};
    SdJournalInventory gap({}, 0);
    if (gap.step() != InventoryStep::Scanning || gap.step() != InventoryStep::Complete || gap.range().first_start != 1) return 2;
    SdJournalInventory checkpoint({}, 1);
    if (checkpoint.step() != InventoryStep::Scanning || checkpoint.step() != InventoryStep::Complete || checkpoint.range().first_start != 2) return 3;
    names = {"000000000000000A.gcj"};
    SdJournalInventory invalid({}, 0);
    if (invalid.step() != InventoryStep::Corrupt) return 4;
    names = {"0000000000000001.gcj", "0000000000000002.gcj"};
    SdJournalInventory interrupted({}, 0);
    busy_once = true;
    if (interrupted.step() != InventoryStep::RetryLater || interrupted.range().complete ||
        interrupted.step() != InventoryStep::Scanning) return 5;
    fail_after_first = true;
    if (interrupted.step() != InventoryStep::RetryLater || interrupted.range().complete) return 6;
    if (interrupted.step() != InventoryStep::Scanning || interrupted.step() != InventoryStep::Scanning ||
        interrupted.step() != InventoryStep::Complete || interrupted.range().last_start != 2) return 7;
    names = {"0000000000000001.gcj", "0000000000000008.gcj"};
    SdJournalInventory spanning({}, 4);
    if (spanning.step() != InventoryStep::Scanning || spanning.step() != InventoryStep::Scanning ||
        spanning.step() != InventoryStep::Complete || spanning.range().first_start != 1 || spanning.range().last_start != 8) return 8;
    return 0;
}
