#include "platform/esp/arduino_common/geocaching/sd_gpx_stage.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <vector>

namespace fixture
{
std::map<std::string, std::string> files;
bool ready = true, external = false, flush_ok = true;
bool corrupt_read = false;
size_t write_limit = SIZE_MAX;
unsigned opens = 0, calls = 0;
size_t transferred = 0;
} // namespace fixture
namespace platform::esp::arduino_common::storage
{
class SdRuntimeFile::Impl
{
  public:
    std::string path;
    bool open = false;
    size_t offset = 0;
};
SdRuntimeFile::SdRuntimeFile() : impl_(new Impl) {}
SdRuntimeFile::~SdRuntimeFile() { delete impl_; }
bool SdRuntimeFile::open(const char* path, const char* mode)
{
    ++fixture::opens;
    ++fixture::calls;
    impl_->path = path;
    impl_->open = true;
    impl_->offset = 0;
    if (*mode == 'w') fixture::files[path].clear();
    return fixture::files.count(path) != 0;
}
void SdRuntimeFile::close()
{
    ++fixture::calls;
    impl_->open = false;
}
bool SdRuntimeFile::is_open() const { return impl_->open; }
std::size_t SdRuntimeFile::write(const void* data, std::size_t size)
{
    ++fixture::calls;
    fixture::transferred += size;
    const auto n = std::min(size, fixture::write_limit);
    fixture::files[impl_->path].append(static_cast<const char*>(data), n);
    return n;
}
bool SdRuntimeFile::flush()
{
    ++fixture::calls;
    return fixture::flush_ok;
}
int SdRuntimeFile::read(void* data, size_t size)
{
    ++fixture::calls;
    fixture::transferred += size;
    const auto& bytes = fixture::files[impl_->path];
    const auto count = std::min(size, bytes.size() - impl_->offset);
    std::memcpy(data, bytes.data() + impl_->offset, count);
    impl_->offset += count;
    if (fixture::corrupt_read && count) static_cast<uint8_t*>(data)[0] ^= 1;
    return static_cast<int>(count);
}
uint64_t SdRuntimeFile::size() const
{
    ++fixture::calls;
    return fixture::files[impl_->path].size();
}
bool sd_card_ready() { return fixture::ready; }
bool sd_external_block_owner_active() { return fixture::external; }
bool sd_is_directory(const char*)
{
    ++fixture::calls;
    return true;
}
bool sd_exists(const char* path)
{
    ++fixture::calls;
    return fixture::files.count(path) != 0;
}
} // namespace platform::esp::arduino_common::storage
struct WriterCrypto final : geocaching::protocol::RecordCrypto
{
    bool sha256(geocaching::ByteView, uint8_t out[32]) override
    {
        std::memset(out, 0, 32);
        return true;
    }
    geocaching::protocol::VerificationResult verifyEd25519(geocaching::ByteView, geocaching::ByteView, geocaching::ByteView) override
    {
        return geocaching::protocol::VerificationResult::CryptoUnavailable;
    }
};
using namespace platform::esp::arduino_common::geocaching;
// Only the test driver loops; production code returns to the owner per step.
StageResult finish(SdGpxStage& stage, StageResult result)
{
    for (unsigned i = 0; result == StageResult::InProgress && i < 256; ++i)
    {
        fixture::calls = 0;
        fixture::transferred = 0;
        result = stage.step();
        if (fixture::calls > 1 || fixture::transferred > 512) return StageResult::InvalidTransaction;
    }
    return result;
}
struct StringSink final : gps::gpx::OutputSink
{
    std::string value;
    bool write(std::string_view bytes) override
    {
        value.append(bytes.data(), bytes.size());
        return true;
    }
};
int main(int argc, char** argv)
{
    using namespace platform::esp::arduino_common::geocaching;
    if (argc != 2) return 1;
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)), {});
    geocaching::protocol::CmpReader reader({data.data(), data.size()});
    geocaching::protocol::VerifiedRecordView record;
    geocaching::ByteView encoded;
    size_t count = 0;
    if (!reader.array(count, 2) || !reader.binary(encoded, 4096) || !reader.binary(record.signature, 64) ||
        !geocaching::protocol::decodeGeocacheRecord(encoded, record.record)) return 2;
    // Storage fault tests only; signature verification is covered by crypto_pipeline.
    WriterCrypto crypto;
    std::array<uint8_t, 16> transaction{};
    transaction[0] = 1;
    auto stage = std::make_unique<SdGpxStage>();
    if (finish(*stage, stage->begin(transaction, record, crypto)) != StageResult::Written) return 3;
    const auto original = fixture::files.at(stage->path());
    StringSink expected;
    if (!geocaching::gpx::writeGeocacheGpx(record, crypto, expected) || original != expected.value) return 10;
    if (original.find("<wpt lat=") == std::string::npos || original.find("</gpx>") == std::string::npos) return 4;
    auto collision = std::make_unique<SdGpxStage>();
    const auto opens = fixture::opens;
    if (finish(*collision, collision->begin(transaction, record, crypto)) != StageResult::AlreadyExists || fixture::opens != opens ||
        fixture::files.at(stage->path()) != original) return 5;
    transaction[0] = 2;
    fixture::write_limit = 7;
    auto short_write = std::make_unique<SdGpxStage>();
    if (finish(*short_write, short_write->begin(transaction, record, crypto)) != StageResult::IoError) return 6;
    fixture::write_limit = SIZE_MAX;
    fixture::flush_ok = false;
    transaction[0] = 3;
    auto flush_failure = std::make_unique<SdGpxStage>();
    if (finish(*flush_failure, flush_failure->begin(transaction, record, crypto)) != StageResult::IoError) return 7;
    fixture::flush_ok = true;
    fixture::external = true;
    transaction[0] = 4;
    auto busy = std::make_unique<SdGpxStage>();
    if (finish(*busy, busy->begin(transaction, record, crypto)) != StageResult::Unavailable) return 8;
    if (fixture::files.at(stage->path()) != original) return 9;
    fixture::external = false;
    transaction[0] = 5;
    auto cancelled = std::make_unique<SdGpxStage>();
    fixture::calls = 0;
    if (cancelled->begin(transaction, record, crypto) != StageResult::InProgress || fixture::calls) return 11;
    if (cancelled->cancel() != StageResult::Cancelled || cancelled->step() != StageResult::Cancelled ||
        fixture::files.count(cancelled->path())) return 12;
    transaction[0] = 6;
    fixture::corrupt_read = true;
    auto corrupt = std::make_unique<SdGpxStage>();
    if (finish(*corrupt, corrupt->begin(transaction, record, crypto)) != StageResult::IoError) return 13;
    return 0;
}
