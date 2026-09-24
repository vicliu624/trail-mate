#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ui::geocaching
{
enum class Section : std::uint8_t
{
    Discover,
    Downloaded,
    Published
};
struct Item
{
    // Draft rows use their 16-byte local draft ID in the leading bytes of id;
    // they are never cache IDs and cannot be downloaded or navigated to.
    bool is_draft = false;
    uint64_t edit_generation = 0;
    uint32_t publication_revision = 0;
    bool publication_confirmed = false;
    std::array<std::uint8_t, 32> id{};
    std::array<std::uint8_t, 32> revision_hash{};
    std::array<char, 97> name{};
    std::array<char, 160> detail{};
    int32_t latitude_e7 = 0, longitude_e7 = 0;
    bool can_download = false;
    bool downloaded = false;
};
struct Snapshot
{
    std::uint64_t generation = 0;
    // Metadata only; count covers all results, rows are read individually.
    std::size_t count = 0;
    std::array<char, 128> status{};
    bool can_refresh = false;
    bool has_more = false;
    bool can_create = false;
};
struct DraftInput
{
    std::array<uint8_t, 16> id{};
    uint64_t generation = 0;
    std::string_view name, description, hint;
    int32_t latitude_e7 = 0, longitude_e7 = 0;
    uint8_t state = 0, difficulty_x2 = 2, terrain_x2 = 2, container_size = 0;
    bool has_coordinates = false;
};
enum class DraftSaveStatus : uint8_t
{
    Failed,
    Pending,
    Saved
};
enum class DraftReadStatus : uint8_t
{
    Failed,
    Pending,
    Ready
};
class Source
{
  public:
    virtual ~Source() = default;
    virtual void activate(bool) {}
    // UI-thread call: copy a coherent, bounded in-memory projection, without
    // network or SD I/O. Increment generation for every visible change within
    // a section, including status and action availability. Strings are NUL
    // terminated. No list-sized copy or I/O is allowed here.
    virtual void snapshot(Section section, Snapshot& out) = 0;
    // Read a single row from this generation without I/O. False means stale.
    virtual bool item(Section section, std::size_t index, std::uint64_t generation, Item& out) = 0;
    virtual void refresh(Section section) = 0;
    virtual void open(const Item& item, std::uint64_t generation) = 0;
    virtual bool loadMore() { return false; }
    virtual bool download(const Item&, std::uint64_t) { return false; }
    // UI-thread call: enqueue/poll without I/O. Pending never calls or retains
    // the sink/context. Ready calls it synchronously with borrowed text; the
    // sink consumes it immediately and must not reenter Source.
    virtual DraftReadStatus readDraft(const std::array<uint8_t, 16>&, void (*)(const DraftInput&, void*), void*) { return DraftReadStatus::Failed; }
    // Nonblocking UI notification: drop interest in this pending read. The
    // storage owner releases its lease on its next step; no UI callback follows.
    virtual void cancelDraftRead(const std::array<uint8_t, 16>&) {}
    // Assigns a stable ID to a new draft; Saved is queried separately after I/O.
    virtual bool saveDraft(DraftInput&) { return false; }
    virtual DraftSaveStatus draftSaveStatus(const std::array<uint8_t, 16>&, uint64_t) { return DraftSaveStatus::Failed; }
    virtual bool publicationAuthor(const std::array<uint8_t, 16>&, uint64_t, std::array<uint8_t, 64>&, uint32_t* = nullptr, uint32_t* = nullptr) { return false; }
    virtual bool publishDraft(const std::array<uint8_t, 16>&, uint64_t, const std::array<uint8_t, 64>&, uint32_t) { return false; }
};
} // namespace ui::geocaching
