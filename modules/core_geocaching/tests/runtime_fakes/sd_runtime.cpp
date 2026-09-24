#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "runtime_environment.h"

namespace platform::esp::arduino_common::storage
{
using namespace runtime_test;
bool sd_card_ready() { return card_ready; }
bool sd_external_block_owner_active() { return external_owner; }
bool sd_is_directory(const char* path) { return access() && directories.count(path); }
bool sd_exists(const char* path) { return access() && (files.count(path) || directories.count(path)); }
bool sd_mkdir(const char* path)
{
    if (!access() || files.count(path)) return false;
    const std::string name(path);
    const auto parent = name.substr(0, name.find_last_of('/'));
    if (!directories.count(parent.empty() ? "/" : parent)) return false;
    directories.insert(name);
    return true;
}
bool sd_rename(const char* from, const char* to)
{
    if (!access() || files.count(to) || directories.count(to)) return false;
    if (files.count(from))
    {
        files[to] = std::move(files.at(from));
        files.erase(from);
        return true;
    }
    if (!directories.count(from)) return false;
    const std::string prefix = std::string(from) + '/';
    std::map<std::string, std::vector<uint8_t>> moved_files;
    std::set<std::string> moved_directories;
    for (auto i = files.begin(); i != files.end();)
    {
        if (i->first.compare(0, prefix.size(), prefix))
        {
            ++i;
            continue;
        }
        moved_files[std::string(to) + i->first.substr(std::strlen(from))] = std::move(i->second);
        i = files.erase(i);
    }
    for (auto i = directories.begin(); i != directories.end();)
    {
        if (*i != from && i->compare(0, prefix.size(), prefix))
        {
            ++i;
            continue;
        }
        moved_directories.insert(std::string(to) + i->substr(std::strlen(from)));
        i = directories.erase(i);
    }
    files.insert(moved_files.begin(), moved_files.end());
    directories.insert(moved_directories.begin(), moved_directories.end());
    return true;
}
bool sd_remove(const char* path) { return access() && files.erase(path) == 1; }
bool sd_rmdir(const char* path)
{
    if (!access() || !directories.count(path)) return false;
    const std::string prefix = std::string(path) + '/';
    for (const auto& file : files)
        if (!file.first.compare(0, prefix.size(), prefix)) return false;
    for (const auto& directory : directories)
        if (!directory.compare(0, prefix.size(), prefix)) return false;
    return directories.erase(path) == 1;
}
class SdRuntimeFile::Impl
{
  public:
    std::string path;
    size_t offset = 0;
    bool opened = false, writable = false, append = false;
};
SdRuntimeFile::SdRuntimeFile() : impl_(new Impl) {}
SdRuntimeFile::~SdRuntimeFile()
{
    close();
    delete impl_;
}
bool SdRuntimeFile::open(const char* path, const char* mode)
{
    close();
    if (!access()) return false;
    impl_->path = path;
    impl_->offset = 0;
    impl_->writable = mode[0] == 'w' || mode[0] == 'a';
    impl_->append = mode[0] == 'a';
    const auto parent = impl_->path.substr(0, impl_->path.find_last_of('/'));
    if (!directories.count(parent)) return false;
    if (mode[0] == 'w') files[path].clear();
    if (impl_->append) files.try_emplace(path);
    impl_->opened = files.count(path);
    if (impl_->opened) ++open_files;
    return impl_->opened;
}
void SdRuntimeFile::close()
{
    if (impl_->opened) --open_files;
    impl_->opened = false;
}
bool SdRuntimeFile::is_open() const { return impl_->opened; }
uint64_t SdRuntimeFile::size() const { return impl_->opened ? files.at(impl_->path).size() : 0; }
bool SdRuntimeFile::seek(uint64_t offset)
{
    if (!access() || !impl_->opened || offset > size()) return false;
    impl_->offset = static_cast<size_t>(offset);
    return true;
}
int SdRuntimeFile::read(void* output, size_t capacity)
{
    if (!access() || !impl_->opened) return -1;
    if (fail_read || fail_read_path == impl_->path)
    {
        fail_read = false;
        fail_read_path.clear();
        return -1;
    }
    const auto& bytes = files.at(impl_->path);
    const auto count = std::min(capacity, bytes.size() - impl_->offset);
    std::memcpy(output, bytes.data() + impl_->offset, count);
    impl_->offset += count;
    io_bytes += count;
    if (profile_reads) read_bytes_by_path[impl_->path] += count;
    return static_cast<int>(count);
}
size_t SdRuntimeFile::write(const void* input, size_t count)
{
    if (!access() || !impl_->opened || !impl_->writable) return 0;
    auto& bytes = files.at(impl_->path);
    if (impl_->append) impl_->offset = bytes.size();
    bytes.resize(std::max(bytes.size(), impl_->offset + count));
    std::memcpy(bytes.data() + impl_->offset, input, count);
    impl_->offset += count;
    io_bytes += count;
    return count;
}
bool SdRuntimeFile::flush() { return access() && impl_->opened; }
SdFileReadResult sd_read_file(const char* path, uint8_t* output, size_t capacity)
{
    SdFileReadResult result;
    if (!access())
    {
        result.status = external_owner ? SdFileReadStatus::Busy : SdFileReadStatus::Unavailable;
        return result;
    }
    if (fail_read)
    {
        fail_read = false;
        return result;
    }
    const auto found = files.find(path);
    if (found == files.end())
    {
        result.status = SdFileReadStatus::Missing;
        return result;
    }
    result.file_size = found->second.size();
    result.bytes_read = std::min(capacity, found->second.size());
    std::memcpy(output, found->second.data(), result.bytes_read);
    io_bytes += result.bytes_read;
    result.status = SdFileReadStatus::Ready;
    return result;
}
class SdRuntimeDir::Impl
{
  public:
    bool opened = false;
    std::vector<std::pair<std::string, bool>> entries;
    size_t offset = 0;
};
SdRuntimeDir::SdRuntimeDir() : impl_(new Impl) {}
SdRuntimeDir::~SdRuntimeDir()
{
    close();
    delete impl_;
}
bool SdRuntimeDir::open(const char* path)
{
    close();
    if (!access() || !directories.count(path)) return false;
    impl_->entries.clear();
    impl_->offset = 0;
    const std::string prefix = std::string(path) + '/';
    const auto add = [&](const std::string& name, bool directory)
    {
        if (name.compare(0, prefix.size(), prefix) == 0 && name.find('/', prefix.size()) == std::string::npos)
            impl_->entries.emplace_back(name.substr(prefix.size()), directory);
    };
    for (const auto& file : files) add(file.first, false);
    for (const auto& directory : directories) add(directory, true);
    impl_->opened = true;
    ++open_dirs;
    return true;
}
void SdRuntimeDir::close()
{
    if (impl_->opened) --open_dirs;
    impl_->opened = false;
}
bool SdRuntimeDir::is_open() const { return impl_->opened; }
SdDirReadStatus SdRuntimeDir::read_next_status(char* output, size_t capacity, bool* directory)
{
    if (!access() || !impl_->opened) return SdDirReadStatus::IoError;
    if (impl_->offset == impl_->entries.size()) return SdDirReadStatus::End;
    const auto& entry = impl_->entries[impl_->offset++];
    if (capacity <= entry.first.size()) return SdDirReadStatus::Invalid;
    std::memcpy(output, entry.first.c_str(), entry.first.size() + 1);
    if (directory) *directory = entry.second;
    return SdDirReadStatus::Entry;
}
} // namespace platform::esp::arduino_common::storage
