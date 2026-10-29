#include "platform/linux/music_storage.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <sys/stat.h>
#include <unistd.h>

namespace platform::linux_runtime
{
using ::music::StorageResult;
struct LinuxMusicStorage::Impl
{
    struct Handle final : ::music::File
    {
        FILE* file = nullptr;
        StorageResult* result = nullptr;
        ~Handle() override
        {
            if (file) std::fclose(file);
        }
        uint32_t size() const override
        {
            struct stat status
            {
            };
            if (fstat(fileno(file), &status) || status.st_size < 0 || uint64_t(status.st_size) > UINT32_MAX)
            {
                *result = StorageResult::Error;
                return 0;
            }
            const auto position = ftello(file);
            return static_cast<uint32_t>(std::max<uint64_t>(status.st_size, position > 0 ? position : 0));
        }
        std::size_t read_at(uint32_t offset, void* output, std::size_t count) override
        {
            if (fseeko(file, offset, SEEK_SET))
            {
                *result = StorageResult::Error;
                return 0;
            }
            const auto read = std::fread(output, 1, count, file);
            *result = std::ferror(file) ? StorageResult::Error : StorageResult::Ok;
            return read;
        }
        std::size_t write(const void* data, std::size_t count) override
        {
            const auto written = std::fwrite(data, 1, count, file);
            *result = written == count ? StorageResult::Ok : StorageResult::Error;
            return written;
        }
        bool flush() override
        {
            const bool ok = std::fflush(file) == 0 && fsync(fileno(file)) == 0;
            *result = ok ? StorageResult::Ok : StorageResult::Error;
            return ok;
        }
    };
    struct Directory
    {
        std::filesystem::directory_iterator iterator;
        uint64_t cursor = 0;
    };
    explicit Impl(std::filesystem::path path)
    {
        std::error_code error;
        std::filesystem::create_directories(path, error);
        root = std::filesystem::weakly_canonical(path, error);
        if (error) root.clear();
    }
    bool resolve(const char* logical, std::filesystem::path& output)
    {
        if (root.empty() || !logical || logical[0] != '/') return false;
        std::filesystem::path relative = std::filesystem::path(logical).relative_path();
        for (const auto& component : relative)
            if (component == "..") return false;
        std::error_code error;
        output = std::filesystem::weakly_canonical(root / relative, error);
        if (error) return false;
        auto base = root.begin(), child = output.begin();
        for (; base != root.end(); ++base, ++child)
            if (child == output.end() || *base != *child) return false;
        return true;
    }
    std::filesystem::path root;
    StorageResult result = StorageResult::Ok;
    std::array<std::unique_ptr<Handle>, 8> handles;
    std::map<std::string, Directory> directories;
};

LinuxMusicStorage::LinuxMusicStorage(std::filesystem::path root) : impl_(std::make_unique<Impl>(std::move(root))) {}
LinuxMusicStorage::~LinuxMusicStorage() = default;
bool LinuxMusicStorage::ready() const
{
    std::error_code error;
    return !impl_->root.empty() && std::filesystem::is_directory(impl_->root, error);
}
uint32_t LinuxMusicStorage::session() const
{
    struct stat status
    {
    };
    if (stat(impl_->root.c_str(), &status) != 0) return 0;
    const auto value = static_cast<uint32_t>(status.st_dev) ^ static_cast<uint32_t>(status.st_ino) ^
                       static_cast<uint32_t>(uint64_t(status.st_ino) >> 32);
    return value ? value : 1;
}
::music::File* LinuxMusicStorage::open(const char* path, ::music::FileMode mode)
{
    std::filesystem::path resolved;
    if (!ready())
    {
        impl_->result = StorageResult::Unavailable;
        return nullptr;
    }
    if (!impl_->resolve(path, resolved))
    {
        impl_->result = StorageResult::Error;
        return nullptr;
    }
    for (auto& slot : impl_->handles)
    {
        if (slot) continue;
        auto handle = std::make_unique<Impl::Handle>();
        handle->result = &impl_->result;
        handle->file = std::fopen(resolved.c_str(), mode == ::music::FileMode::Read ? "rb" : mode == ::music::FileMode::Write ? "wb"
                                                                                                                              : "ab");
        if (!handle->file)
        {
            impl_->result = StorageResult::Missing;
            return nullptr;
        }
        slot = std::move(handle);
        impl_->result = StorageResult::Ok;
        return slot.get();
    }
    impl_->result = StorageResult::Busy;
    return nullptr;
}
void LinuxMusicStorage::close(::music::File*& file)
{
    if (!file) return;
    for (auto& handle : impl_->handles)
        if (handle.get() == file)
        {
            handle.reset();
            file = nullptr;
            return;
        }
    impl_->result = StorageResult::Error;
}
StorageResult LinuxMusicStorage::last_result() const { return impl_->result; }
StorageResult LinuxMusicStorage::next(const char* path, uint64_t& cursor, ::music::Entry& entry)
{
    std::filesystem::path resolved;
    if (!ready()) return impl_->result = StorageResult::Unavailable;
    if (!impl_->resolve(path, resolved)) return impl_->result = StorageResult::Error;
    const auto key = resolved.string();
    std::error_code error;
    auto found = impl_->directories.find(key);
    if (found == impl_->directories.end() || cursor == 0 || found->second.cursor != cursor)
    {
        if (impl_->directories.size() >= 32) impl_->directories.erase(impl_->directories.begin());
        auto& state = impl_->directories[key];
        state.iterator = std::filesystem::directory_iterator(resolved, error);
        state.cursor = 0;
        if (error) return impl_->result = StorageResult::Missing;
        while (state.cursor < cursor && state.iterator != std::filesystem::directory_iterator{})
        {
            state.iterator.increment(error);
            if (error) return impl_->result = StorageResult::Error;
            ++state.cursor;
        }
        found = impl_->directories.find(key);
    }
    auto& directory = found->second;
    while (directory.iterator != std::filesystem::directory_iterator{})
    {
        const auto item = *directory.iterator;
        directory.iterator.increment(error);
        ++directory.cursor;
        cursor = directory.cursor;
        if (error) return impl_->result = StorageResult::Error;
        const auto name = item.path().filename().string();
        if (name.size() >= sizeof(entry.name) || item.is_symlink(error)) continue;
        entry = {};
        ::music::copy_text(entry.name, sizeof(entry.name), name.c_str());
        entry.directory = item.is_directory(error);
        entry.size = entry.directory ? 0 : item.file_size(error);
        if (error) return impl_->result = StorageResult::Error;
        return impl_->result = StorageResult::Ok;
    }
    impl_->directories.erase(key);
    return impl_->result = StorageResult::End;
}
bool LinuxMusicStorage::exists(const char* path)
{
    std::filesystem::path resolved;
    std::error_code error;
    return impl_->resolve(path, resolved) && std::filesystem::exists(resolved, error);
}
bool LinuxMusicStorage::mkdir(const char* path)
{
    std::filesystem::path resolved;
    std::error_code error;
    if (!impl_->resolve(path, resolved)) return false;
    return std::filesystem::is_directory(resolved, error) || std::filesystem::create_directories(resolved, error);
}
bool LinuxMusicStorage::remove(const char* path)
{
    std::filesystem::path resolved;
    std::error_code error;
    return impl_->resolve(path, resolved) && resolved != impl_->root && std::filesystem::remove(resolved, error);
}
bool LinuxMusicStorage::rename(const char* from, const char* to)
{
    std::filesystem::path source, target;
    std::error_code error;
    if (!impl_->resolve(from, source) || !impl_->resolve(to, target) || source == impl_->root || target == impl_->root) return false;
    std::filesystem::rename(source, target, error);
    return !error;
}
} // namespace platform::linux_runtime
