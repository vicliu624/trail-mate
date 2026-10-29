#include "platform/linux/geocaching_gpx_stage.h"
#include "geocaching/gpx/write_record.h"
#include <algorithm>
#include <vector>
#if defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace platform::linux_runtime
{
namespace gc = ::geocaching;
namespace
{
using Result = LinuxGeocachingGpxStage::Result;
struct BufferSink final : gc::gpx::OutputSink
{
    std::vector<uint8_t> bytes;
    bool write(std::string_view text) override
    {
        if (text.size() > gc::kMaxGpxBytes - bytes.size()) return false;
        bytes.insert(bytes.end(), text.begin(), text.end());
        return true;
    }
};
std::string name(const LinuxGeocachingGpxStage::TransactionId& id)
{
    if (std::all_of(id.begin(), id.end(), [](uint8_t value)
                    { return value == 0; })) return {};
    constexpr char alphabet[] = "0123456789abcdef";
    std::string result;
    for (const auto byte : id)
    {
        result += alphabet[byte >> 4];
        result += alphabet[byte & 15];
    }
    return result + ".gpx";
}
#if defined(__linux__)
struct Fd
{
    int value = -1;
    explicit Fd(int fd = -1) : value(fd) {}
    ~Fd()
    {
        if (value >= 0) ::close(value);
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& other) noexcept : value(other.value) { other.value = -1; }
    int release()
    {
        const int fd = value;
        value = -1;
        return fd;
    }
};
Result openError() { return errno == ELOOP || errno == ENOTDIR ? Result::Unsafe : errno == ENOENT ? Result::Missing
                                                                                                  : Result::IoError; }
#endif
} // namespace
struct LinuxGeocachingGpxStage::Impl
{
    Impl(std::filesystem::path path, gc::protocol::RecordCrypto& verifier) : root(std::move(path)), crypto(verifier) {}
    Impl(int descriptor, gc::protocol::RecordCrypto& verifier) : crypto(verifier)
    {
#if defined(__linux__)
        pinned = ::fcntl(descriptor, F_DUPFD_CLOEXEC, 0);
#else
        (void)descriptor;
#endif
    }
    ~Impl()
    {
#if defined(__linux__)
        if (pinned >= 0) ::close(pinned);
#endif
    }
    Result expected(const TransactionId& transaction, const gc::GeocacheId& id, const gc::RevisionHash& hash,
                    gc::ByteView signed_record, BufferSink& sink, Proof& proof)
    {
        proof = {};
        const auto filename = name(transaction);
        if (filename.empty()) return Result::Invalid;
        gc::protocol::VerifiedRecordView verified;
        const auto result = gc::protocol::verifyGeocache(signed_record, crypto, scratch.data(), scratch.size(), verified, &id, &hash);
        if (result == gc::protocol::VerificationResult::CryptoUnavailable) return Result::CryptoUnavailable;
        if (result != gc::protocol::VerificationResult::Valid) return Result::Invalid;
        if (!gc::gpx::writeGeocacheGpx(verified, crypto, sink) || sink.bytes.empty()) return Result::Invalid;
        if (!crypto.sha256({sink.bytes.data(), sink.bytes.size()}, proof.file_hash.data())) return Result::CryptoUnavailable;
        proof.bytes = sink.bytes.size();
        proof.relative_path = ".state/staging/" + filename;
        return Result::Ready;
    }
#if defined(__linux__)
    Fd directory(bool create, Result& result)
    {
        Fd base(pinned >= 0 ? ::fcntl(pinned, F_DUPFD_CLOEXEC, 0) : ::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (base.value < 0)
        {
            result = errno == ENOENT ? Result::Unavailable : openError();
            return Fd{};
        }
        if (create && ::mkdirat(base.value, ".state", 0700) && errno != EEXIST)
        {
            result = Result::IoError;
            return Fd{};
        }
        Fd state(::openat(base.value, ".state", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (state.value < 0)
        {
            result = openError();
            return Fd{};
        }
        if (create && ::mkdirat(state.value, "staging", 0700) && errno != EEXIST)
        {
            result = Result::IoError;
            return Fd{};
        }
        Fd staging(::openat(state.value, "staging", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (staging.value < 0)
        {
            result = openError();
            return Fd{};
        }
        if (::fsync(base.value) || ::fsync(state.value))
        {
            result = Result::IoError;
            return Fd{};
        }
        result = Result::Ready;
        return staging;
    }
    Result readback(int directory, const std::string& filename, const std::vector<uint8_t>& expected, const struct stat* original)
    {
        Fd file(::openat(directory, filename.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
        if (file.value < 0) return openError();
        struct stat before
        {
        }, after{};
        if (::fstat(file.value, &before)) return Result::IoError;
        if (!S_ISREG(before.st_mode) || before.st_nlink != 1) return Result::Unsafe;
        if (original && (original->st_dev != before.st_dev || original->st_ino != before.st_ino)) return Result::Unsafe;
        if (before.st_size < 0 || static_cast<uint64_t>(before.st_size) != expected.size()) return Result::IoError;
        std::array<uint8_t, 512> buffer{};
        size_t offset = 0;
        while (offset < expected.size())
        {
            const auto count = std::min(buffer.size(), expected.size() - offset);
            const auto bytes = ::read(file.value, buffer.data(), count);
            if (bytes < 0 && errno == EINTR) continue;
            if (bytes <= 0) return Result::IoError;
            if (!std::equal(buffer.begin(), buffer.begin() + bytes, expected.begin() + offset)) return Result::IoError;
            offset += static_cast<size_t>(bytes);
        }
        ssize_t extra;
        do
        {
            extra = ::read(file.value, buffer.data(), 1);
        } while (extra < 0 && errno == EINTR);
        if (extra != 0 || ::fstat(file.value, &after)) return Result::IoError;
        if (after.st_size != before.st_size || after.st_nlink != 1 || after.st_dev != before.st_dev || after.st_ino != before.st_ino ||
            after.st_mtim.tv_sec != before.st_mtim.tv_sec || after.st_mtim.tv_nsec != before.st_mtim.tv_nsec ||
            after.st_ctim.tv_sec != before.st_ctim.tv_sec || after.st_ctim.tv_nsec != before.st_ctim.tv_nsec) return Result::Unsafe;
        if (::fsync(file.value) || ::fsync(directory)) return Result::IoError;
        const int fd = file.release();
        return ::close(fd) == 0 ? Result::Ready : Result::IoError;
    }
#endif
    std::filesystem::path root;
    gc::protocol::RecordCrypto& crypto;
    std::array<uint8_t, 4200> scratch{};
    int pinned = -1;
};
LinuxGeocachingGpxStage::LinuxGeocachingGpxStage(std::filesystem::path root, gc::protocol::RecordCrypto& crypto) : impl_(std::make_unique<Impl>(std::move(root), crypto)) {}
LinuxGeocachingGpxStage::~LinuxGeocachingGpxStage() = default;
LinuxGeocachingGpxStage::LinuxGeocachingGpxStage(int descriptor, gc::protocol::RecordCrypto& crypto) : impl_(std::make_unique<Impl>(descriptor, crypto)) {}
Result LinuxGeocachingGpxStage::stage(const TransactionId& transaction, const gc::GeocacheId& id, const gc::RevisionHash& hash, gc::ByteView signed_record, Proof& out)
{
    out = {};
#if defined(__linux__)
    BufferSink sink;
    Proof proof;
    const auto prepared = impl_->expected(transaction, id, hash, signed_record, sink, proof);
    if (prepared != Result::Ready) return prepared;
    Result result;
    auto directory = impl_->directory(true, result);
    if (directory.value < 0) return result;
    const auto filename = name(transaction);
    Fd file(::openat(directory.value, filename.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (file.value < 0) return errno == EEXIST ? Result::Exists : openError();
    size_t offset = 0;
    while (offset < sink.bytes.size())
    {
        const auto written = ::write(file.value, sink.bytes.data() + offset, sink.bytes.size() - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return Result::IoError;
        offset += static_cast<size_t>(written);
    }
    struct stat original
    {
    };
    if (::fsync(file.value) || ::fstat(file.value, &original)) return Result::IoError;
    if (!S_ISREG(original.st_mode) || original.st_nlink != 1) return Result::Unsafe;
    const int fd = file.release();
    if (::close(fd)) return Result::IoError;
    // Resolve the configured path again, detecting replaced directory entries.
    auto reopened = impl_->directory(false, result);
    if (reopened.value < 0) return result;
    result = impl_->readback(reopened.value, filename, sink.bytes, &original);
    if (result == Result::Ready) out = std::move(proof);
    return result;
#else
    (void)transaction;
    (void)id;
    (void)hash;
    (void)signed_record;
    return Result::Unavailable;
#endif
}
Result LinuxGeocachingGpxStage::verify(const TransactionId& transaction, const gc::GeocacheId& id, const gc::RevisionHash& hash, gc::ByteView signed_record, Proof& out)
{
    out = {};
#if defined(__linux__)
    BufferSink sink;
    Proof proof;
    const auto prepared = impl_->expected(transaction, id, hash, signed_record, sink, proof);
    if (prepared != Result::Ready) return prepared;
    Result result;
    auto directory = impl_->directory(false, result);
    if (directory.value < 0) return result;
    result = impl_->readback(directory.value, name(transaction), sink.bytes, nullptr);
    if (result == Result::Ready) out = std::move(proof);
    return result;
#else
    (void)transaction;
    (void)id;
    (void)hash;
    (void)signed_record;
    return Result::Unavailable;
#endif
}
} // namespace platform::linux_runtime
