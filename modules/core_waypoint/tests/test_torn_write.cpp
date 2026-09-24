#include "platform/esp/common/storage/waypoint_file_store.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace
{
using Store = platform::esp::storage::WaypointFileStore;
using waypoint::Result;
constexpr const char* path = "waypoint-torn-test.dat";
constexpr const char* staging = "waypoint-torn-test.init";
using Image = std::array<uint8_t, Store::kFileBytes>;
Image readImage()
{
    Image image{};
    auto* file = std::fopen(path, "rb");
    assert(file && std::fread(image.data(), 1, image.size(), file) == image.size());
    assert(!std::fclose(file));
    return image;
}
void restoreImage(const Image& image)
{
    auto* file = std::fopen(path, "wb");
    assert(file && std::fwrite(image.data(), 1, image.size(), file) == image.size());
    assert(!std::fclose(file));
}
bool equal(const waypoint::Record& a, const waypoint::Record& b)
{
    return a.id == b.id && a.latitude_e7 == b.latitude_e7 && a.longitude_e7 == b.longitude_e7 &&
           !std::strcmp(a.name, b.name);
}
void verifyCuts(const Image& before, const Image& after, unsigned bank,
                const waypoint::Record& oldRecord, const waypoint::Record& newRecord)
{
    const unsigned offset = 64 + bank * 64;
    for (unsigned cut = 0; cut <= 64; ++cut)
    {
        Image torn = before;
        std::memcpy(torn.data() + offset, after.data() + offset, cut);
        restoreImage(torn);
        Store reopened(path, staging);
        assert(reopened.begin() == Result::Ok);
        waypoint::Record recovered;
        assert(reopened.read(0, recovered) == Result::Ok);
        assert(equal(recovered, oldRecord) || equal(recovered, newRecord));
        if (!cut) assert(equal(recovered, oldRecord));
        if (cut == 64) assert(equal(recovered, newRecord));
        // Recovery must leave the store writable, not merely readable.
        if (recovered.id) assert(reopened.update(recovered) == Result::Ok);
    }
    restoreImage(after);
}
} // namespace

int main()
{
    std::remove(path);
    std::remove(staging);
    Store store(path, staging);
    assert(store.begin() == Result::Ok);
    const auto emptyImage = readImage();
    waypoint::Record record;
    record.latitude_e7 = 123456;
    record.longitude_e7 = -654321;
    std::strcpy(record.name, "Camp before");
    uint32_t id = 0;
    assert(store.create(record, id) == Result::Ok);
    record.id = id;
    auto createdImage = readImage();
    verifyCuts(emptyImage, createdImage, 1, {}, record);
    auto original = record;
    record.latitude_e7 = -789012;
    std::strcpy(record.name, "Camp after");
    assert(store.update(record) == Result::Ok);
    auto updatedImage = readImage();
    verifyCuts(createdImage, updatedImage, 0, original, record);
    assert(store.remove(id) == Result::Ok);
    auto deletedImage = readImage();
    verifyCuts(updatedImage, deletedImage, 1, record, {});
    // Tombstones survive reopen and slot reuse assigns a different identity.
    Store reopened(path, staging);
    assert(reopened.begin() == Result::Ok);
    uint32_t replacement = 0;
    assert(reopened.create(record, replacement) == Result::Ok && replacement > id);
    assert(!std::remove(path));
    std::remove(staging);
}
