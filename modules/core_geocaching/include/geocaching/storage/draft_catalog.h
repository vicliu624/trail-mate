#pragma once
#include "geocaching/protocol/verify_record.h"
#include "geocaching/storage/draft_publication.h"

namespace geocaching::storage
{
// A fixed UI window, never a catalog-sized allocation. Long editor text is
// represented by fingerprints solely for local-change projection.
struct DraftCatalogEntry
{
    std::array<uint8_t, 16> id{};
    std::array<uint8_t, 64> author{};
    std::array<uint8_t, 32> base_hash{}, description_hash{}, hint_hash{};
    std::array<char, 97> name{};
    uint64_t generation = 0;
    int32_t latitude_e7 = 0, longitude_e7 = 0;
    uint8_t state = 0, difficulty_x2 = 2, terrain_x2 = 2, container_size = 0;
    bool has_author = false, has_base = false, has_coordinates = false;
    DraftPublication publication;
};
struct DraftCatalogPage
{
    std::array<DraftCatalogEntry, 4> rows{};
    size_t offset = 0, count = 0, total = 0;
};
static_assert(sizeof(DraftCatalogPage) <= 1536, "Draft catalog retains four small projections only");

inline bool describeDraft(ByteView key, const DraftView& draft, protocol::RecordCrypto& crypto, DraftCatalogEntry& out)
{
    out = {};
    if (!key.data || key.size != 16 || draft.name.size() >= out.name.size()) return false;
    std::memcpy(out.id.data(), key.data, 16);
    out.generation = draft.generation;
    out.has_author = draft.author.size == 64;
    out.has_base = draft.base_hash.size == 32;
    if (out.has_author) std::memcpy(out.author.data(), draft.author.data, 64);
    if (out.has_base) std::memcpy(out.base_hash.data(), draft.base_hash.data, 32);
    if (!draft.name.empty()) std::memcpy(out.name.data(), draft.name.data(), draft.name.size());
    out.latitude_e7 = draft.latitude_e7;
    out.longitude_e7 = draft.longitude_e7;
    out.state = draft.state;
    out.difficulty_x2 = draft.difficulty_x2;
    out.terrain_x2 = draft.terrain_x2;
    out.container_size = draft.container_size;
    out.has_coordinates = draft.has_coordinates;
    return crypto.sha256({reinterpret_cast<const uint8_t*>(draft.description.data()), draft.description.size()}, out.description_hash.data()) &&
           crypto.sha256({reinterpret_cast<const uint8_t*>(draft.hint.data()), draft.hint.size()}, out.hint_hash.data());
}
inline bool draftContentMatches(const DraftCatalogEntry& draft, const RecordView& record, protocol::RecordCrypto& crypto, bool& matches)
{
    matches = false;
    if (!draft.has_coordinates || record.state != static_cast<CacheState>(draft.state) || record.latitude_e7 != draft.latitude_e7 ||
        record.longitude_e7 != draft.longitude_e7 || record.name != draft.name.data() || record.difficulty_x2 != draft.difficulty_x2 ||
        record.terrain_x2 != draft.terrain_x2 || record.container_size != static_cast<ContainerSize>(draft.container_size)) return true;
    std::array<uint8_t, 32> digest;
    if (!crypto.sha256({reinterpret_cast<const uint8_t*>(record.description.data()), record.description.size()}, digest.data())) return false;
    if (digest != draft.description_hash) return true;
    if (!crypto.sha256({reinterpret_cast<const uint8_t*>(record.hint.data()), record.hint.size()}, digest.data())) return false;
    matches = digest == draft.hint_hash;
    return true;
}
template <class View>
bool readLogicalDraftCatalog(const View& view, size_t offset, protocol::RecordCrypto& crypto, DraftCatalogPage& page)
{
    page.count = page.total = 0;
    page.offset = offset;
    size_t cursor = 0;
    MutationView row;
    while (view.next(cursor, row))
    {
        if (row.table != 4) continue;
        const auto ordinal = page.total++;
        if (ordinal < offset || page.count == page.rows.size()) continue;
        DraftView draft;
        auto& entry = page.rows[page.count];
        if (!decodeDraft(row.key, row.value, draft) || !describeDraft(row.key, draft, crypto, entry)) return false;
        entry.publication = draftPublication(view, row.key, draft);
        ++page.count;
    }
    return true;
}
} // namespace geocaching::storage
