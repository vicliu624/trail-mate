#include "tmap/reader.h"
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

class NativeFile final : public tmap::RandomAccessFile
{
  public:
    explicit NativeFile(const char* path)
    {
        file_ = std::fopen(path, "rb");
        if (!file_) return;
#if defined(_WIN32)
        if (_fseeki64(file_, 0, SEEK_END) == 0) size_ = static_cast<uint64_t>(_ftelli64(file_));
#else
        static_assert(sizeof(off_t) >= 8, "Native TMAP probe needs 64-bit file offsets");
        if (fseeko(file_, 0, SEEK_END) == 0) size_ = static_cast<uint64_t>(ftello(file_));
#endif
    }
    ~NativeFile() override
    {
        if (file_) std::fclose(file_);
    }
    uint64_t size() const override { return size_; }
    tmap::Status readAt(uint64_t offset, uint8_t* output, size_t bytes) override
    {
        if (!file_ || offset > size_ || bytes > size_ - offset) return tmap::Status::IoError;
#if defined(_WIN32)
        if (_fseeki64(file_, static_cast<__int64>(offset), SEEK_SET)) return tmap::Status::IoError;
#else
        if (fseeko(file_, static_cast<off_t>(offset), SEEK_SET)) return tmap::Status::IoError;
#endif
        return std::fread(output, 1, bytes, file_) == bytes ? tmap::Status::Ok : tmap::Status::IoError;
    }

  private:
    std::FILE* file_ = nullptr;
    uint64_t size_ = 0;
};

int main(int argc, char** argv)
{
    if (argc != 5)
    {
        std::fprintf(stderr, "tmap_probe file zoom longitude latitude\n");
        return 2;
    }
    const auto z = static_cast<uint8_t>(std::atoi(argv[2]));
    const auto lon = std::atof(argv[3]), lat = std::atof(argv[4]);
    const auto side = std::ldexp(1.0, z);
    const auto x = static_cast<uint32_t>((lon + 180) / 360 * side);
    const auto y = static_cast<uint32_t>((1 - std::asinh(std::tan(lat * 3.14159265358979323846 / 180)) / 3.14159265358979323846) / 2 * side);
    NativeFile file(argv[1]);
    auto workspace = std::make_unique<tmap::Workspace>();
    tmap::Reader reader(*workspace);
    auto status = reader.open(file);
    if (status != tmap::Status::Ok)
    {
        std::fprintf(stderr, "open status=%u\n", unsigned(status));
        return 1;
    }
    tmap::Tile tile{};
    status = reader.lookupTile(1, z, x, y, tile);
    if (status != tmap::Status::Ok)
    {
        std::fprintf(stderr, "lookup status=%u\n", unsigned(status));
        return 1;
    }
    std::vector<uint8_t> pixels(tile.bytes);
    status = reader.readTile(tile, pixels.data(), pixels.size());
    if (status != tmap::Status::Ok)
    {
        std::fprintf(stderr, "pixels status=%u\n", unsigned(status));
        return 1;
    }
    std::printf("file=%" PRIu64 " offset=%" PRIu64 " tile=%u/%u/%u crc=%08x codec=%u workspace=%u reader=%u pois=%" PRIu64 "\n",
                file.size(), tile.offset, unsigned(z), x, y,
                tile.crc, unsigned(tile.codec), unsigned(sizeof(tmap::Workspace)), unsigned(sizeof(tmap::Reader)), reader.poiCount());
    size_t annotations = 0;
    status = reader.visitAnnotations(
        z, x, y, [](void* context, const tmap::Annotation&, const tmap::Poi& poi)
        { auto& count = *static_cast<size_t*>(context); if (++count <= 3) std::printf("annotation %s %.7f %.7f\n", poi.name, poi.latitude / 1e7, poi.longitude / 1e7); return true; },
        &annotations);
    if (status != tmap::Status::Ok && status != tmap::Status::Missing)
    {
        std::fprintf(stderr, "annotation status=%u\n", unsigned(status));
        return 1;
    }
    auto poi = std::make_unique<tmap::Poi>();
    status = reader.readPoi(reader.poiCount(), *poi);
    if (status != tmap::Status::Ok)
    {
        std::fprintf(stderr, "poi status=%u\n", unsigned(status));
        return 1;
    }
    const auto id = poi->id;
    status = reader.findPoi(id, *poi);
    if (status != tmap::Status::Ok || poi->row != reader.poiCount())
    {
        std::fprintf(stderr, "identity status=%u\n", unsigned(status));
        return 1;
    }
    std::printf("annotations=%u page_reads=%" PRIu64 "\n", unsigned(annotations), reader.pageReads());
    auto cursor = std::make_unique<tmap::SearchCursor>();
    const auto* query = z >= 8 ? u8"昆明" : u8"北京";
    size_t matches = 0, steps = 0;
    status = reader.beginSearch(query, std::strlen(query), tmap::SearchMode::Substring, *cursor);
    if (status != tmap::Status::Ok)
    {
        std::fprintf(stderr, "begin search status=%u\n", unsigned(status));
        return 1;
    }
    do
    {
        status = reader.searchStep(
            *cursor, 64, [](void* context, const tmap::Poi& p, tmap::SearchMode)
            { if (++*static_cast<size_t*>(context) <= 3) std::printf("search %s %.7f %.7f\n", p.name, p.latitude / 1e7, p.longitude / 1e7); return true; },
            &matches);
        if (++steps > 10000)
        {
            std::fprintf(stderr, "unbounded search\n");
            return 1;
        }
    } while (status == tmap::Status::More);
    if (status != tmap::Status::Ok || !matches)
    {
        std::fprintf(stderr, "search status=%u\n", unsigned(status));
        return 1;
    }
    std::printf("search_matches=%u search_steps=%u cursor=%u\n", unsigned(matches), unsigned(steps), unsigned(sizeof(tmap::SearchCursor)));
    return 0;
}
