#include "chat/infra/reticulum/reticulum_wire.h"
#include "platform/esp/arduino_common/geocaching/sd_gpx_hash.h"
#include <cstring>
#include <vector>
std::vector<uint8_t> bytes(1025, 7);
namespace platform::esp::arduino_common::storage
{
class SdRuntimeFile::Impl
{
  public:
    bool open = false;
    size_t offset = 0;
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
uint64_t SdRuntimeFile::size() const { return bytes.size(); }
int SdRuntimeFile::read(void* out, size_t count)
{
    const auto n = std::min(count, bytes.size() - impl_->offset);
    std::memcpy(out, bytes.data() + impl_->offset, n);
    impl_->offset += n;
    return static_cast<int>(n);
}
} // namespace platform::esp::arduino_common::storage
struct Digest
{
    std::vector<uint8_t> data;
    void update(const uint8_t* bytes, size_t size) { data.insert(data.end(), bytes, bytes + size); }
    bool finalize(uint8_t* out, size_t)
    {
        chat::reticulum::fullHash(data.data(), data.size(), out);
        return true;
    }
};
int main()
{
    using namespace platform::esp::arduino_common::geocaching;
    Digest digest;
    SdGpxHash<Digest> reader(digest);
    std::array<uint8_t, 32> actual, expected;
    chat::reticulum::fullHash(bytes.data(), bytes.size(), expected.data());
    if (!reader.open("local.gpx") || reader.result(actual)) return 1;
    for (unsigned i = 0; i < 3; ++i)
        if (reader.step() != GpxHashStep::Reading) return 2;
    if (reader.step() != GpxHashStep::Complete || !reader.result(actual) || actual != expected) return 3;
    Digest growing_digest;
    SdGpxHash<Digest> growing(growing_digest);
    if (!growing.open("local.gpx")) return 4;
    for (unsigned i = 0; i < 3; ++i)
        if (growing.step() != GpxHashStep::Reading) return 5;
    bytes.push_back(9);
    if (growing.step() != GpxHashStep::IoError || growing.result(actual)) return 6;
    return 0;
}
