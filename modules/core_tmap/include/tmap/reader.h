#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace tmap
{
enum class Status : uint8_t
{
    Ok,
    Missing,
    Busy,
    Invalid,
    IoError,
    Cancelled,
    More
};

// Implementations perform exact reads. Busy means no output can be consumed.
// Media replacement must invalidate both the file and its Reader workspace.
class RandomAccessFile
{
  public:
    virtual ~RandomAccessFile() = default;
    virtual Status readAt(uint64_t offset, uint8_t* output, size_t bytes) = 0;
    virtual uint64_t size() const = 0;
};

struct Bounds
{
    int32_t west = 0, south = 0, east = 0, north = 0;
    bool contains(int32_t latitude, int32_t longitude) const;
    bool intersects(const Bounds& other) const;
};
struct Package
{
    std::array<uint8_t, 16> id{}, series{}, build{};
    uint64_t revision = 0;
    Bounds bounds{};
    uint32_t zoom_mask = 0, capabilities = 0;
    uint16_t specificity = 0;
};
struct Section
{
    uint32_t type = 0, id = 0, owner = 0;
    uint64_t offset = 0, length = 0, count = 0, root = 0;
    bool paged = false;
};
struct Tile
{
    uint64_t offset = 0;
    uint32_t bytes = 0, crc = 0;
    uint16_t codec = 0;
};
struct Poi
{
    uint64_t row = 0;
    std::array<uint8_t, 16> id{};
    int32_t latitude = 0, longitude = 0;
    uint32_t category = 0, kind = 0;
    uint16_t importance = 0;
    char name[513]{};
};
struct Annotation
{
    uint64_t poi_row = 0;
    int32_t latitude = 0, longitude = 0;
    uint16_t kind = 0, priority = 0;
    uint8_t point_count = 0;
    std::array<int32_t, 16> path{};
};

// Caller-owned continuation, including the current POI/name. Store in PSRAM.
// A completed stage is never repeated after Busy, even if another query runs.
struct AnnotationCursor
{
    std::array<uint8_t, 16> build{};
    uint64_t key = 0, offset = 0, next = 0, geometry = 0, name = 0, text = 0;
    uint64_t fast_first = 0;
    std::array<uint8_t, 176> fast_record{};
    uint32_t expected = 0, seen = 0, slot = 0, count = 0;
    Annotation annotation{};
    Poi poi{};
    uint8_t zoom = 0, stage = 0;
    bool initialized = false, complete = true;
};
static_assert(sizeof(AnnotationCursor) <= 1024, "Annotation continuation must be bounded");

// No allocations inside Reader. This storage belongs on heap/PSRAM, not a task stack.
// 64 section slots cover all 13 raster semantics emitted by Center v1.
struct Workspace
{
    static constexpr size_t kPageBytes = 4096, kMaxSections = 64, kCachePages = 2;
    struct Page
    {
        std::array<uint8_t, kPageBytes> bytes{};
        uint64_t offset = 0;
        uint32_t section = 0, age = 0;
        bool valid = false;
    };
    std::array<Section, kMaxSections> sections{};
    std::array<Page, kCachePages> pages{};
    std::array<char, 513> matched_name{};
    std::array<char, 513> matched_display{}, administrative_path{};
    uint8_t administrative_levels = 0, administrative_flags = 1;
    std::array<uint8_t, 96> record{};
    Poi poi{};
};
static_assert(sizeof(Workspace) <= 16 * 1024, "TMAP workspace must remain bounded");

using AnnotationVisitor = bool (*)(void*, const Annotation&, const Poi&);
using PoiVisitor = bool (*)(void*, const Poi&);
enum class SearchMode : uint8_t
{
    Exact,
    Prefix,
    Substring
};
using SearchVisitor = bool (*)(void*, const Poi&, SearchMode match);
struct Posting
{
    uint64_t offset = 0, count = 0;
    uint32_t blocks = 0;
};
// Caller-owned resumable state; keep with the query's PSRAM storage.
struct SearchCursor
{
    char query[513]{};
    std::array<uint8_t, 16> build{};
    std::array<Posting, 8> filters{};
    std::array<uint64_t, 8> filter_positions{};
    Posting posting{};
    uint64_t position = 0, leaf = 0, visited_leaves = 0;
    uint32_t slot = 0;
    uint8_t filter_count = 0;
    SearchMode mode = SearchMode::Substring;
    bool complete = true;
    bool advancing_name = false;
    bool initialized = false;
    uint16_t query_offset = 0;
    uint32_t previous_scalar = 0;
};
static_assert(sizeof(SearchCursor) <= 1024, "Search continuation must be bounded");

class Reader
{
  public:
    explicit Reader(Workspace& workspace) : workspace_(workspace) {}
    Status open(RandomAccessFile& file);
    void close();
    bool isOpen() const { return file_ != nullptr && opened_; }
    const Package& package() const { return package_; }
    uint64_t poiCount() const;
    Status lookupTile(uint32_t semantic, uint8_t zoom, uint32_t x, uint32_t y, Tile& tile);
    Status lookupAnnotations(uint8_t zoom, uint32_t x, uint32_t y);
    Status readTile(const Tile& tile, uint8_t* output, size_t capacity);
    Status readPoi(uint64_t row, Poi& output);
    Status findPoi(const std::array<uint8_t, 16>& id, Poi& output);
    Status visitAnnotations(uint8_t zoom, uint32_t x, uint32_t y, AnnotationVisitor visitor, void* context);
    Status beginAnnotations(uint8_t zoom, uint32_t x, uint32_t y, AnnotationCursor& cursor);
    Status annotationStep(AnnotationCursor& cursor, size_t stage_budget, AnnotationVisitor visitor, void* context);
    Status categoryName(uint32_t id, char* output, size_t capacity);
    Status administrativeLocation(uint64_t row, char* output, size_t capacity, uint8_t& levels, uint8_t& flags);
    // Valid only during the SearchVisitor callback, owned by caller Workspace.
    const char* searchMatchedName() const { return workspace_.matched_display.data(); }
    const char* searchAdministrativePath() const { return workspace_.administrative_path.data(); }
    uint8_t searchAdministrativeFlags() const { return workspace_.administrative_flags; }
    Status queryBounds(const Bounds& bounds, PoiVisitor visitor, void* context);
    Status beginSearch(const char* query, size_t bytes, SearchMode mode, SearchCursor& cursor);
    // More preserves cursor. Busy retries the current candidate; already visited results remain valid.
    // A name is the index document; callers deduplicate retained results by the full stable POI ID.
    Status searchStep(SearchCursor& cursor, size_t candidate_budget, SearchVisitor visitor, void* context);
    static bool normalizeName(const char* input, size_t bytes, char* output, size_t capacity);
    uint64_t pageReads() const { return page_reads_; }
    static bool tileKey(uint8_t zoom, uint32_t x, uint32_t y, uint64_t& key);
    static uint32_t crc32c(const uint8_t* bytes, size_t count);

  private:
    enum class KeyKind : uint8_t
    {
        Number,
        Id,
        Text
    };
    Workspace& workspace_;
    RandomAccessFile* file_ = nullptr;
    Package package_{};
    size_t section_count_ = 0;
    bool opened_ = false;
    uint32_t clock_ = 0;
    uint64_t page_reads_ = 0;
    const Section* section(uint32_t id) const;
    Status read(uint64_t offset, uint8_t* output, size_t size);
    Status page(const Section& section, uint64_t offset, const uint8_t*& output);
    Status row(uint32_t section, uint64_t id, size_t bytes, uint8_t* output);
    Status string(uint64_t reference, char* output, size_t capacity, uint32_t section_id = 23);
    Status find(const Section& section, KeyKind kind, const uint8_t* key, size_t key_bytes,
                uint64_t number, uint8_t* value, size_t value_bytes);
    Status metadata();
    Status seekName(SearchCursor& cursor);
    Status initializeSearch(SearchCursor& cursor, size_t& budget);
    Status namePosting(SearchCursor& cursor);
    Status postingValue(const Posting& posting, uint64_t position, uint64_t& id);
    Status postingContains(const Posting& posting, uint64_t id, bool& found);
    Status postingSeek(const Posting& posting, uint64_t id, uint64_t& position, bool& found);
    Status visitSpatial(const Section& section, uint64_t offset, unsigned depth, const Bounds& bounds,
                        PoiVisitor visitor, void* context);
};
static_assert(sizeof(Reader) <= 192, "Reader control state must be small");
} // namespace tmap
