#include "platform/esp/arduino_common/geocaching/sd_volume.h"
#include <map>
#include <set>
#include <string>
#include <algorithm>

std::map<std::string, std::string> files;
std::set<std::string> directories;
unsigned writes = 0;
namespace platform::esp::arduino_common::storage
{
class SdRuntimeFile::Impl { public: std::string path; };
SdRuntimeFile::SdRuntimeFile() : impl_(new Impl) {}
SdRuntimeFile::~SdRuntimeFile() { delete impl_; }
bool SdRuntimeFile::open(const char* path, const char*) { impl_->path = path; files[path].clear(); return true; }
void SdRuntimeFile::close() {}
size_t SdRuntimeFile::write(const void* data, size_t count)
{
    ++writes; files[impl_->path].append(static_cast<const char*>(data), count); return count;
}
bool SdRuntimeFile::flush() { return true; }
bool sd_card_ready() { return true; }
bool sd_external_block_owner_active() { return false; }
bool sd_exists(const char* path) { return directories.count(path) || files.count(path); }
bool sd_is_directory(const char* path) { return directories.count(path) != 0; }
bool sd_mkdir(const char* path) { directories.insert(path); return true; }
SdFileReadResult sd_read_file(const char* path, uint8_t* buffer, size_t capacity)
{
    SdFileReadResult result;
    auto found = files.find(path);
    if (found == files.end()) { result.status = SdFileReadStatus::Missing; return result; }
    result.file_size = found->second.size(); result.bytes_read = std::min(capacity, found->second.size());
    std::memcpy(buffer, found->second.data(), result.bytes_read);
    result.status = SdFileReadStatus::Ready; return result;
}
}
int main()
{
    using namespace platform::esp::arduino_common::geocaching;
    ::geocaching::storage::VolumeInstance candidate{}, actual;
    candidate[0] = 1;
    if (createNewSdVolume(candidate, actual) != SdVolumeResult::Ready || actual != candidate || writes != 1) return 1;
    const auto saved = files;
    candidate[0] = 2;
    if (createNewSdVolume(candidate, actual) != SdVolumeResult::Ready || actual[0] != 1 || writes != 1 || files != saved) return 2;
    files.clear();
    if (createNewSdVolume(candidate, actual) != SdVolumeResult::Corrupt || actual != ::geocaching::storage::VolumeInstance{} || writes != 1) return 3;
    return 0;
}
