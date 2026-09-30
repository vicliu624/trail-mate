#include "chat/infra/reticulum/reticulum_wire.h"
#include "geocaching/storage/checkpoint_verifier.h"
#include "platform/esp/arduino_common/geocaching/sd_checkpoint_writer.h"
#include <map>
#include <string>
#include <vector>

std::map<std::string, std::vector<uint8_t>> files;
bool busy = false, ready = true, flush_ok = true;
size_t transferred = 0;
geocaching::storage::VolumeInstance mounted{};
namespace platform::esp::arduino_common::storage
{
bool sd_card_ready() { return ready; }
bool sd_external_block_owner_active() { return busy; }
bool sd_exists(const char* path) { return files.count(path) != 0; }
SdFileReadResult sd_read_file(const char*, uint8_t* out, size_t capacity)
{
    auto header = ::geocaching::storage::encodeVolumeHeader(mounted);
    const auto n = std::min(capacity, header.size());
    std::memcpy(out, header.data(), n);
    transferred += n;
    SdFileReadResult result;
    result.status = SdFileReadStatus::Ready;
    result.file_size = header.size();
    result.bytes_read = n;
    return result;
}
class SdRuntimeFile::Impl
{
  public:
    std::string path;
    size_t offset = 0;
    bool open = false;
};
SdRuntimeFile::SdRuntimeFile() : impl_(new Impl) {}
SdRuntimeFile::~SdRuntimeFile() { delete impl_; }
bool SdRuntimeFile::open(const char* path, const char* mode)
{
    if (*mode == 'r' && !files.count(path)) return false;
    impl_->path = path;
    impl_->offset = *mode == 'a' ? files[path].size() : 0;
    impl_->open = true;
    return true;
}
void SdRuntimeFile::close() { impl_->open = false; }
uint64_t SdRuntimeFile::size() const { return files.at(impl_->path).size(); }
bool SdRuntimeFile::seek(uint64_t offset)
{
    if (offset > size()) return false;
    impl_->offset = offset;
    return true;
}
int SdRuntimeFile::read(void* out, size_t wanted)
{
    auto& bytes = files.at(impl_->path);
    const auto n = std::min({wanted, bytes.size() - impl_->offset, size_t(37)});
    std::memcpy(out, bytes.data() + impl_->offset, n);
    impl_->offset += n;
    transferred += n;
    return static_cast<int>(n);
}
size_t SdRuntimeFile::write(const void* data, size_t count)
{
    auto& bytes = files.at(impl_->path);
    bytes.resize(impl_->offset + count);
    std::memcpy(bytes.data() + impl_->offset, data, count);
    impl_->offset += count;
    transferred += count;
    return count;
}
bool SdRuntimeFile::flush() { return flush_ok; }
} // namespace platform::esp::arduino_common::storage
struct Digest
{
    std::vector<uint8_t> bytes;
    void update(const uint8_t* data, size_t size) { bytes.insert(bytes.end(), data, data + size); }
    bool finalize(uint8_t* output, size_t)
    {
        chat::reticulum::fullHash(bytes.data(), bytes.size(), output);
        return true;
    }
};
int main()
{
    using namespace platform::esp::arduino_common::geocaching;
    using namespace geocaching::storage;
    using Writer = SdCheckpointWriter<Digest>;
    const std::string old_slot = "/trailmate/geocaching/.state/checkpoint/a.gcs";
    files[old_slot] = {0x73, 0x61, 0x66, 0x65};
    const auto original = files;
    const auto pump = [](auto& writer)
    {
        auto state = CheckpointWriteStep::Working;
        for (unsigned i = 0; i < 4096 && state == CheckpointWriteStep::Working; ++i)
        {
            transferred = 0;
            state = writer.step();
            if (transferred > 512) return CheckpointWriteStep::Invalid;
        }
        return state;
    };
    const uint8_t keys[] = {1, 2};
    std::vector<uint8_t> value(700, 0x5a);
    MutationView rows[] = {{5, {keys, 1}, {value.data(), value.size()}, false},
                           {5, {keys + 1, 1}, {value.data(), value.size()}, false}};
    Digest digest;
    Writer writer({}, digest);
    if (!writer.begin(7)) return 1;
    busy = true;
    transferred = 0;
    if (writer.step() != CheckpointWriteStep::Busy || transferred || files != original) return 2;
    busy = false;
    ready = false;
    if (writer.step() != CheckpointWriteStep::Unavailable || transferred || files != original) return 3;
    ready = true;
    if (pump(writer) != CheckpointWriteStep::Ready) return 4;
    for (unsigned i = 0; i < 2; ++i)
        if (!writer.page(rows + i, 1) || pump(writer) != CheckpointWriteStep::Ready) return 5;
    if (writer.page(rows, 1)) return 6; // Cross-page key order is mandatory.
    if (!writer.finish() || pump(writer) != CheckpointWriteStep::Verified || files.at(old_slot) != original.at(old_slot)) return 7;
    const auto completed = files.at(Writer::path);
    const auto verify = [](const std::vector<uint8_t>& bytes)
    {
        Digest read_digest;
        CheckpointVerifier<Digest> reader(read_digest);
        size_t offset = 0, records = 0;
        while (offset < bytes.size())
        {
            if (bytes.size() - offset < 24) return false;
            uint32_t size = 0;
            for (unsigned j = 0; j < 4; ++j) size = (size << 8) | bytes[offset + 8 + j];
            if (size > bytes.size() - offset - 24) return false;
            CheckpointPageCursor cursor;
            if (!reader.accept({bytes.data() + offset, size_t(size) + 24}, cursor)) return false;
            records += cursor.count();
            offset += size + 24;
        }
        return reader.finish() && reader.sequence() == 7 && records == 2;
    };
    if (!verify(completed)) return 8;
    // Every truncated durable prefix must fail validation, including missing tail.
    for (size_t n = 0; n < completed.size(); ++n)
        if (verify({completed.begin(), completed.begin() + n})) return 9;
    Digest existing_digest;
    Writer existing({}, existing_digest);
    if (!existing.begin(8) || pump(existing) != CheckpointWriteStep::Exists || files.at(Writer::path) != completed) return 10;
    files = original;
    Digest failed_digest;
    Writer failed({}, failed_digest);
    if (!failed.begin(7) || pump(failed) != CheckpointWriteStep::Ready || !failed.page(rows, 1)) return 11;
    flush_ok = false;
    if (pump(failed) != CheckpointWriteStep::IoError || files.at(old_slot) != original.at(old_slot)) return 12;
    flush_ok = true;
    files = original;
    Digest changed_digest;
    Writer changed({}, changed_digest);
    if (!changed.begin(7) || pump(changed) != CheckpointWriteStep::Ready || !changed.page(rows, 1)) return 13;
    mounted[0] = 1;
    if (pump(changed) != CheckpointWriteStep::VolumeChanged || files != original) return 14;
    return 0;
}
