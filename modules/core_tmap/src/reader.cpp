#include "tmap/reader.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

namespace tmap
{
namespace
{
uint16_t u16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
uint32_t u32(const uint8_t* p) { return uint32_t(u16(p)) | (uint32_t(u16(p + 2)) << 16); }
uint64_t u64(const uint8_t* p) { return uint64_t(u32(p)) | (uint64_t(u32(p + 4)) << 32); }
int32_t i32(const uint8_t* p) { return static_cast<int32_t>(u32(p)); }
Bounds box(const uint8_t* p) { return {i32(p), i32(p + 4), i32(p + 8), i32(p + 12)}; }
bool bounded(uint64_t offset, uint64_t length, uint64_t size) { return offset <= size && length <= size - offset; }
bool knownSection(uint32_t type)
{
    return type == 1 || type == 2 || type == 10 || type == 11 || (type >= 20 && type <= 24) ||
           (type >= 30 && type <= 32) || (type >= 40 && type <= 44) || (type >= 50 && type <= 54);
}
constexpr std::array<uint32_t, 256> crcTable()
{
    std::array<uint32_t, 256> table{};
    for (size_t i = 0; i < table.size(); ++i)
    {
        uint32_t c = static_cast<uint32_t>(i);
        for (int bit = 0; bit < 8; ++bit) c = (c >> 1) ^ ((c & 1U) ? 0x82F63B78U : 0U);
        table[i] = c;
    }
    return table;
}
constexpr auto kCrc = crcTable();
uint32_t crcWithZero(const uint8_t* bytes, size_t count, size_t field)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < count; ++i)
    {
        const auto byte = i >= field && i - field < 4 ? uint8_t(0) : bytes[i];
        crc = kCrc[(crc ^ byte) & 255] ^ (crc >> 8);
    }
    return ~crc;
}
bool validUtf8(const uint8_t* text, size_t size)
{
    for (size_t at = 0; at < size;)
    {
        const auto first = text[at++];
        if (first < 0x80)
        {
            if (first == 0) return false;
            continue;
        }
        uint32_t scalar = 0, minimum = 0;
        unsigned continuation = 0;
        if (first >= 0xc2 && first <= 0xdf)
        {
            scalar = first & 31;
            minimum = 0x80;
            continuation = 1;
        }
        else if (first >= 0xe0 && first <= 0xef)
        {
            scalar = first & 15;
            minimum = 0x800;
            continuation = 2;
        }
        else if (first >= 0xf0 && first <= 0xf4)
        {
            scalar = first & 7;
            minimum = 0x10000;
            continuation = 3;
        }
        else return false;
        if (continuation > size - at) return false;
        while (continuation--)
        {
            const auto c = text[at++];
            if ((c & 0xc0) != 0x80) return false;
            scalar = (scalar << 6) | (c & 63);
        }
        if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) return false;
    }
    return true;
}
struct Entry
{
    const uint8_t* key = nullptr;
    const uint8_t* value = nullptr;
    size_t key_bytes = 0, value_bytes = 0;
};
bool entry(const uint8_t* page, size_t slot, unsigned kind, bool inner, Entry& out)
{
    const auto count = u32(page + 16);
    if (slot >= count) return false;
    if (kind == 3)
    {
        if (count > 2016) return false;
        const auto end = 4096 - count * 2;
        const auto at = u16(page + 4096 - 2 * (slot + 1));
        if (at < 64 || at > end || end - at < 4) return false;
        out.key_bytes = u16(page + at);
        out.value_bytes = u16(page + at + 2);
        if (!out.key_bytes || out.key_bytes > 512 || out.value_bytes != (inner ? 8U : 32U) ||
            out.key_bytes + out.value_bytes > end - at - 4) return false;
        out.key = page + at + 4;
        out.value = out.key + out.key_bytes;
    }
    else
    {
        const auto size = u16(page + 20);
        const size_t key_size = kind == 1 ? 8 : 16;
        if (size < key_size + 8 || count > 4032 / size) return false;
        out.key = page + 64 + slot * size;
        out.key_bytes = key_size;
        out.value = out.key + key_size;
        out.value_bytes = size - key_size;
    }
    return true;
}
int compare(const Entry& entry, unsigned kind, const uint8_t* key, size_t size, uint64_t number)
{
    if (kind == 1)
    {
        const auto value = u64(entry.key);
        return value < number ? -1 : value > number ? 1
                                                    : 0;
    }
    const auto compared = std::memcmp(entry.key, key, std::min(size, entry.key_bytes));
    return compared ? compared : entry.key_bytes < size ? -1
                             : entry.key_bytes > size   ? 1
                                                        : 0;
}
} // namespace

bool Bounds::contains(int32_t latitude, int32_t longitude) const
{
    return longitude >= west && longitude <= east && latitude >= south && latitude <= north;
}
bool Bounds::intersects(const Bounds& other) const
{
    return west <= other.east && east >= other.west && south <= other.north && north >= other.south;
}
uint32_t Reader::crc32c(const uint8_t* bytes, size_t count)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < count; ++i) crc = kCrc[(crc ^ bytes[i]) & 255] ^ (crc >> 8);
    return ~crc;
}
bool Reader::tileKey(uint8_t zoom, uint32_t x, uint32_t y, uint64_t& key)
{
    if (zoom > 29 || uint64_t(x) >= (UINT64_C(1) << zoom) || uint64_t(y) >= (UINT64_C(1) << zoom)) return false;
    key = uint64_t(zoom) << 58;
    for (unsigned bit = 0; bit < 29; ++bit)
        key |= (uint64_t((x >> bit) & 1U) << (bit * 2)) | (uint64_t((y >> bit) & 1U) << (bit * 2 + 1));
    return true;
}
void Reader::close()
{
    file_ = nullptr;
    opened_ = false;
    section_count_ = 0;
    package_ = {};
    clock_ = 0;
    for (auto& slot : workspace_.pages) slot.valid = false;
}
Status Reader::read(uint64_t offset, uint8_t* output, size_t bytes)
{
    if (!file_ || !output || !bounded(offset, bytes, file_->size())) return Status::Invalid;
    return file_->readAt(offset, output, bytes);
}
Status Reader::open(RandomAccessFile& file)
{
    close();
    file_ = &file;
    page_reads_ = 0;
    auto& bytes = workspace_.pages[0].bytes;
    auto status = read(0, bytes.data(), 256);
    if (status != Status::Ok)
    {
        close();
        return status;
    }
    const auto* h = bytes.data();
    constexpr uint8_t magic[] = {'T', 'M', 'A', 'P', '\r', '\n', 0x1a, '\n'};
    const auto count = u32(h + 48);
    if (std::memcmp(h, magic, 8) || u16(h + 8) != 1 || u16(h + 10) != 0 || u32(h + 12) != 256 ||
        u32(h + 16) != 0x01020304 || u32(h + 20) != 4096 || (u64(h + 24) & ~UINT64_C(7)) ||
        u64(h + 32) != file.size() || u64(h + 40) != 4096 || !count || count > Workspace::kMaxSections ||
        u32(h + 52) != 64 || u32(h + 168) != 1 || u32(h + 172) != 2 || u32(h + 160) != crcWithZero(h, 256, 160))
    {
        close();
        return Status::Invalid;
    }
    std::memcpy(package_.id.data(), h + 56, 16);
    std::memcpy(package_.build.data(), h + 72, 16);
    std::memcpy(package_.series.data(), h + 88, 16);
    package_.revision = u64(h + 104);
    package_.bounds = box(h + 120);
    package_.zoom_mask = u32(h + 136);
    package_.capabilities = u32(h + 140);
    if (package_.bounds.west < -1800000000 || package_.bounds.east > 1800000000 ||
        package_.bounds.south < -900000000 || package_.bounds.north > 900000000 ||
        package_.bounds.west >= package_.bounds.east || package_.bounds.south >= package_.bounds.north ||
        (package_.capabilities & ~7U))
    {
        close();
        return Status::Invalid;
    }
    const auto meta_offset = u64(h + 144), meta_size = u64(h + 152);
    const auto directory_crc = u32(h + 164);
    status = read(4096, bytes.data(), count * 64);
    if (status != Status::Ok)
    {
        close();
        return status;
    }
    if (crc32c(bytes.data(), count * 64) != directory_crc)
    {
        close();
        return Status::Invalid;
    }
    uint64_t previous_end = ((4096 + uint64_t(count) * 64 + 4095) / 4096) * 4096;
    for (size_t i = 0; i < count; ++i)
    {
        const auto* d = bytes.data() + i * 64;
        auto& section = workspace_.sections[i];
        section = {u32(d), u32(d + 8), u32(d + 12), u64(d + 16), u64(d + 24), u64(d + 32), u64(d + 40), (u16(d + 6) & 2U) != 0};
        if (u16(d + 4) != 1 || (u16(d + 6) & ~3U) || (!knownSection(section.type) && (u16(d + 6) & 1U)) ||
            section.offset % 4096 || section.offset < previous_end || !bounded(section.offset, section.length, file.size()) ||
            (section.paged && section.length % 4096) || (section.root && (section.root % 4096 || section.root >= section.length)))
        {
            close();
            return Status::Invalid;
        }
        for (size_t earlier = 0; earlier < i; ++earlier)
            if (workspace_.sections[earlier].id == section.id)
            {
                close();
                return Status::Invalid;
            }
        previous_end = section.offset + section.length;
    }
    section_count_ = count;
    for (const auto id : {1U, 2U, 20U, 21U, 22U, 23U, 24U, 30U, 31U, 32U, 40U, 41U, 42U})
    {
        const auto* s = section(id);
        if (!s || s->type != id)
        {
            close();
            return Status::Invalid;
        }
    }
    const auto* meta = section(1);
    if (meta->offset != meta_offset || meta->length != meta_size || meta_size > 65536 || meta->paged || section(2)->paged || section(2)->length % 96)
    {
        close();
        return Status::Invalid;
    }
    opened_ = true;
    status = metadata();
    if (status != Status::Ok) close();
    return status;
}
const Section* Reader::section(uint32_t id) const
{
    for (size_t i = 0; i < section_count_; ++i)
        if (workspace_.sections[i].id == id) return &workspace_.sections[i];
    return nullptr;
}
uint64_t Reader::poiCount() const
{
    const auto* s = section(20);
    return s ? s->count : 0;
}
Status Reader::page(const Section& section, uint64_t offset, const uint8_t*& output)
{
    output = nullptr;
    if (!section.paged || offset % 4096 || !bounded(offset, 4096, section.length)) return Status::Invalid;
    for (auto& p : workspace_.pages)
        if (p.valid && p.section == section.id && p.offset == offset)
        {
            p.age = ++clock_;
            output = p.bytes.data();
            return Status::Ok;
        }
    auto* target = &workspace_.pages[0];
    for (auto& p : workspace_.pages)
        if (!p.valid || p.age < target->age) target = &p;
    target->valid = false;
    const auto status = read(section.offset + offset, target->bytes.data(), 4096);
    if (status != Status::Ok) return status;
    ++page_reads_;
    const auto* p = target->bytes.data();
    if (u32(p) != 0x31475054 || u64(p + 8) != offset || u32(p + 16) > 2016 || u32(p + 48) != crcWithZero(p, 4096, 48)) return Status::Invalid;
    target->section = section.id;
    target->offset = offset;
    target->age = ++clock_;
    target->valid = true;
    output = p;
    return Status::Ok;
}
Status Reader::find(const Section& section, KeyKind kind, const uint8_t* key, size_t key_size,
                    uint64_t number, uint8_t* value, size_t value_size)
{
    if (!section.root) return section.count == 0 ? Status::Missing : Status::Invalid;
    const auto key_kind = static_cast<unsigned>(kind) + 1;
    auto offset = section.root;
    unsigned expected = 32;
    for (unsigned depth = 0; depth < 32; ++depth)
    {
        const uint8_t* p = nullptr;
        const auto status = page(section, offset, p);
        if (status != Status::Ok) return status;
        const auto level = u16(p + 6);
        const auto count = u32(p + 16);
        const auto type = u16(p + 4);
        const auto inner_type = kind == KeyKind::Number ? 2U : kind == KeyKind::Id ? 4U
                                                                                   : 6U;
        if (!count || level >= expected || (level && type != inner_type) ||
            (!level && type != (kind == KeyKind::Number ? (value_size == 32 ? 8U : 3U) : kind == KeyKind::Id ? 5U
                                                                                                             : 7U))) return Status::Invalid;
        size_t low = 0, high = count;
        Entry item{};
        while (low < high)
        {
            const auto middle = low + (high - low) / 2;
            if (!entry(p, middle, key_kind, level != 0, item)) return Status::Invalid;
            if (compare(item, key_kind, key, key_size, number) < 0) low = middle + 1;
            else high = middle;
        }
        if (low == count) return Status::Missing;
        if (!entry(p, low, key_kind, level != 0, item)) return Status::Invalid;
        if (!level)
        {
            if (compare(item, key_kind, key, key_size, number) != 0) return Status::Missing;
            if (item.value_bytes != value_size) return Status::Invalid;
            std::memcpy(value, item.value, value_size);
            return Status::Ok;
        }
        if (item.value_bytes != 8) return Status::Invalid;
        expected = level;
        offset = u64(item.value);
    }
    return Status::Invalid;
}
Status Reader::layerCoverage(std::array<uint32_t, 13>& zooms)
{
    zooms.fill(0);
    if (!isOpen()) return Status::Invalid;
    const auto* layers = section(2);
    if (!layers || layers->length > 64U * 96U) return Status::Invalid;
    auto& record = workspace_.record;
    for (uint64_t at = 0; at < layers->length; at += 96)
    {
        const auto status = read(layers->offset + at, record.data(), 96);
        if (status != Status::Ok) return status;
        const auto* d = record.data();
        const auto semantic = u16(d + 4);
        const unsigned slot = semantic >= 1 && semantic <= 3       ? semantic - 1
                              : semantic >= 100 && semantic <= 104 ? semantic - 100 + 3
                              : semantic >= 110 && semantic <= 114 ? semantic - 110 + 8
                                                                   : 13;
        if (slot == 13) continue;
        if (u16(d + 6) != 256 || u16(d + 14)) return Status::Invalid;
        const auto* index = section(u32(d + 16));
        const auto* pixels = section(u32(d + 20));
        if (!index || !pixels || index->type != 10 || pixels->type != 11 ||
            index->owner != u32(d) || pixels->owner != u32(d)) return Status::Invalid;
        if (index->count && pixels->length) zooms[slot] |= u32(d + 8) & package_.zoom_mask;
    }
    return Status::Ok;
}
Status Reader::lookupTile(uint32_t semantic, uint8_t zoom, uint32_t x, uint32_t y, Tile& output)
{
    output = {};
    uint64_t key = 0;
    if (!isOpen() || !tileKey(zoom, x, y, key)) return Status::Invalid;
    if (!(package_.zoom_mask & (1U << zoom))) return Status::Missing;
    const auto* layers = section(2);
    const auto* index = static_cast<const Section*>(nullptr);
    const Section* pixels = nullptr;
    auto& record = workspace_.record;
    for (uint64_t at = 0; at < layers->length; at += 96)
    {
        auto status = read(layers->offset + at, record.data(), 96);
        if (status != Status::Ok) return status;
        const auto* d = record.data();
        if (u16(d + 4) != semantic) continue;
        if (u16(d + 6) != 256 || u16(d + 14) != 0) return Status::Invalid;
        if (!(u32(d + 8) & (1U << zoom))) continue;
        index = section(u32(d + 16));
        pixels = section(u32(d + 20));
        if (!index || !pixels || index->type != 10 || pixels->type != 11 || index->owner != u32(d) || pixels->owner != u32(d)) return Status::Invalid;
        break;
    }
    if (!index) return Status::Missing;
    auto status = find(*index, KeyKind::Number, nullptr, 0, key, record.data(), 24);
    if (status != Status::Ok) return status;
    const auto* d = record.data();
    const auto offset = u64(d);
    const auto length = u32(d + 8);
    const auto codec = u16(d + 16);
    if ((codec != 1 && codec != 2) || length != (codec == 1 ? 131072U : 262144U) || u32(d + 12) != length || u16(d + 18) ||
        !bounded(offset, length, pixels->length) || pixels->paged) return Status::Invalid;
    output = {pixels->offset + offset, length, u32(d + 20), codec};
    return Status::Ok;
}
Status Reader::readTile(const Tile& tile, uint8_t* output, size_t capacity)
{
    if (!isOpen() || !output || capacity < tile.bytes || (tile.bytes != 131072 && tile.bytes != 262144)) return Status::Invalid;
    const auto status = read(tile.offset, output, tile.bytes);
    if (status != Status::Ok) return status;
    return crc32c(output, tile.bytes) == tile.crc ? Status::Ok : Status::Invalid;
}
Status Reader::row(uint32_t section_id, uint64_t id, size_t size, uint8_t* output)
{
    const auto* s = section(section_id);
    if (!s || !id || id > s->count || !size || size > 4032) return Status::Invalid;
    const auto capacity = 4032 / size;
    // Validate the page number before multiplying a potentially corrupt row ID.
    const auto page_number = (id - 1) / capacity;
    if (page_number >= s->length / 4096) return Status::Invalid;
    const uint8_t* p = nullptr;
    const auto status = page(*s, page_number * 4096, p);
    if (status != Status::Ok) return status;
    const auto slot = (id - 1) % capacity;
    if (u16(p + 4) != 9 || u16(p + 20) != size || u32(p + 16) > capacity || slot >= u32(p + 16)) return Status::Invalid;
    std::memcpy(output, p + 64 + slot * size, size);
    return Status::Ok;
}
Status Reader::string(uint64_t reference, char* output, size_t capacity, uint32_t section_id)
{
    if (!output || !capacity) return Status::Invalid;
    output[0] = 0;
    const auto* strings = section(section_id);
    const uint8_t* p = nullptr;
    if (!strings) return Status::Invalid;
    auto status = page(*strings, reference / 4096 * 4096, p);
    if (status != Status::Ok) return status;
    const auto at = reference % 4096;
    if (u16(p + 4) != 10 || at < 64 || at > 4092) return Status::Invalid;
    const auto length = u16(p + at);
    if (length > 512 || length > 4096 - at - 4 || length >= capacity || u16(p + at + 2) || !validUtf8(p + at + 4, length)) return Status::Invalid;
    std::memcpy(output, p + at + 4, length);
    output[length] = 0;
    return Status::Ok;
}
Status Reader::administrativeLocation(uint64_t id, char* output, size_t capacity, uint8_t& levels, uint8_t& flags)
{
    if (!isOpen() || !output || !capacity || !id || id > poiCount()) return Status::Invalid;
    output[0] = 0;
    levels = 0;
    flags = 1;
    const auto* references = section(50);
    const auto* strings = section(51);
    if (!references && !strings) return Status::Ok; // Legacy package, explicitly unknown.
    if (!references || !strings || references->count != poiCount()) return Status::Invalid;
    auto& record = workspace_.record;
    auto status = row(50, id, 16, record.data());
    if (status != Status::Ok) return status;
    const uint64_t text = u64(record.data());
    levels = record[8];
    flags = record[9];
    if ((levels & ~63U) || (flags & ~3U) || record[13] || record[14] || record[15]) return Status::Invalid;
    return text ? string(text, output, capacity, 51) : Status::Ok;
}
Status Reader::readPoi(uint64_t id, Poi& output)
{
    new (&output) Poi{}; // Initialize caller storage, no large task-stack temporary.
    auto& record = workspace_.record;
    auto status = row(20, id, 96, record.data());
    if (status != Status::Ok) return status;
    const auto* p = record.data();
    output.row = id;
    std::memcpy(output.id.data(), p, 16);
    output.latitude = i32(p + 16);
    output.longitude = i32(p + 20);
    output.category = u32(p + 24);
    output.kind = u32(p + 28);
    output.importance = u16(p + 56);
    const auto primary = u64(p + 32), first = u64(p + 40);
    const auto count = u32(p + 48);
    const auto* names = section(22);
    if (output.latitude < -900000000 || output.latitude > 900000000 || output.longitude < -1800000000 || output.longitude >= 1800000000 ||
        !names || (count && (!first || first > names->count || count > names->count - first + 1 || primary < first || primary - first >= count)) ||
        (!count && (primary || first))) return Status::Invalid;
    if (!primary) return Status::Ok;
    status = row(22, primary, 48, record.data());
    if (status != Status::Ok) return status;
    if (u64(record.data()) != id) return Status::Invalid;
    return string(u64(record.data() + 8), output.name, sizeof(output.name));
}
Status Reader::findPoi(const std::array<uint8_t, 16>& id, Poi& output)
{
    const auto* s = section(21);
    if (!isOpen() || !s) return Status::Invalid;
    auto& record = workspace_.record;
    auto status = find(*s, KeyKind::Id, id.data(), 16, 0, record.data(), 16);
    if (status != Status::Ok) return status;
    return readPoi(u64(record.data()), output);
}
Status Reader::visitAnnotations(uint8_t zoom, uint32_t x, uint32_t y, AnnotationVisitor visitor, void* context)
{
    uint64_t key = 0;
    if (!isOpen() || !visitor || !tileKey(zoom, x, y, key)) return Status::Invalid;
    auto& record = workspace_.record;
    const auto* index = section(40);
    const auto* data = section(41);
    const auto* geometry = section(42);
    if (!index || !data || !geometry) return Status::Invalid;
    auto status = find(*index, KeyKind::Number, nullptr, 0, key, record.data(), 24);
    if (status != Status::Ok) return status;
    auto offset = u64(record.data());
    const auto bytes = u32(record.data() + 8);
    if (!offset || !bytes || bytes % 40 || bytes > 8000) return Status::Invalid;
    const auto expected = bytes / 40;
    uint32_t seen = 0;
    while (offset)
    {
        const uint8_t* p = nullptr;
        status = page(*data, offset, p);
        if (status != Status::Ok) return status;
        const auto count = u32(p + 16);
        const auto next = u64(p + 24);
        if (u16(p + 4) != 15 || u16(p + 20) != 40 || !count || count > 100 || count > expected - seen) return Status::Invalid;
        for (uint32_t slot = 0; slot < count; ++slot)
        {
            // Reload after POI/string/geometry access evicts this page from the two-page cache.
            status = page(*data, offset, p);
            if (status != Status::Ok) return status;
            std::memcpy(record.data(), p + 64 + slot * 40, 40);
            const auto* r = record.data();
            Annotation annotation{};
            annotation.poi_row = u64(r);
            annotation.kind = u16(r + 8);
            annotation.priority = u16(r + 12);
            annotation.latitude = i32(r + 16);
            annotation.longitude = i32(r + 20);
            const auto reference = u64(r + 24);
            if (annotation.kind < 1 || annotation.kind > 3 || r[10] > zoom || r[11] < zoom ||
                annotation.latitude < -900000000 || annotation.latitude > 900000000 ||
                annotation.longitude < -1800000000 || annotation.longitude >= 1800000000) return Status::Invalid;
            if (reference)
            {
                status = page(*geometry, reference / 4096 * 4096, p);
                if (status != Status::Ok) return status;
                const auto at = reference % 4096;
                if (u16(p + 4) != 16 || at < 64 || at > 4080 || u16(p + at) != 1) return Status::Invalid;
                const auto points = u32(p + at + 8);
                if (points > 8 || u32(p + at + 4) != points * 8 || points * 8 > 4096 - at - 16) return Status::Invalid;
                annotation.point_count = static_cast<uint8_t>(points);
                for (size_t i = 0; i < points * 2; ++i) annotation.path[i] = i32(p + at + 16 + i * 4);
                for (size_t i = 0; i < points; ++i)
                    if (annotation.path[2 * i] < -900000000 || annotation.path[2 * i] > 900000000 ||
                        annotation.path[2 * i + 1] < -1800000000 || annotation.path[2 * i + 1] >= 1800000000) return Status::Invalid;
            }
            status = readPoi(annotation.poi_row, workspace_.poi);
            if (status != Status::Ok) return status;
            ++seen;
            if (!visitor(context, annotation, workspace_.poi)) return Status::Cancelled;
        }
        if (seen == expected && next) return Status::Invalid;
        offset = next;
    }
    return seen == expected ? Status::Ok : Status::Invalid;
}
Status Reader::beginAnnotations(uint8_t zoom, uint32_t x, uint32_t y, AnnotationCursor& cursor)
{
    uint64_t key = 0;
    if (!isOpen() || !tileKey(zoom, x, y, key)) return Status::Invalid;
    new (&cursor) AnnotationCursor{};
    cursor.key = key;
    cursor.zoom = zoom;
    cursor.build = package_.build;
    cursor.complete = false;
    return Status::Ok;
}
Status Reader::lookupAnnotations(uint8_t zoom, uint32_t x, uint32_t y)
{
    uint64_t key = 0;
    if (!isOpen() || !tileKey(zoom, x, y, key)) return Status::Invalid;
    const auto* fast = section(44);
    const auto* index = fast ? fast : section(40);
    if (!index) return Status::Invalid;
    return find(*index, KeyKind::Number, nullptr, 0, key, workspace_.record.data(), fast ? 16 : 24);
}
Status Reader::annotationStep(AnnotationCursor& c, size_t budget, AnnotationVisitor visitor, void* context)
{
    if (!isOpen() || !visitor || c.build != package_.build) return Status::Invalid;
    if (c.complete) return Status::Ok;
    const auto* index = section(40);
    const auto* data = section(41);
    const auto* geometry = section(42);
    if (!index || !data || !geometry) return Status::Invalid;
    auto& record = workspace_.record;
    budget = std::min<size_t>(budget, 64);
    while (budget--)
    {
        const uint8_t* p = nullptr;
        Status status = Status::Ok;
        switch (c.stage)
        {
        case 0: // Locate the tile's annotation list only once.
            if (const auto* fast_index = section(44))
            {
                const auto* fast_data = section(43);
                if (!fast_data || fast_index->type != 44 || fast_data->type != 43 || !fast_data->paged || !fast_index->paged) return Status::Invalid;
                status = find(*fast_index, KeyKind::Number, nullptr, 0, c.key, record.data(), 16);
                if (status != Status::Ok) return status;
                c.fast_first = u64(record.data());
                c.expected = u32(record.data() + 8);
                if (!c.fast_first || !c.expected || c.expected > 200 || c.fast_first > fast_data->count ||
                    c.expected > fast_data->count - c.fast_first + 1 || u32(record.data() + 12)) return Status::Invalid;
                c.initialized = true;
                c.stage = 7;
                break;
            }
            status = find(*index, KeyKind::Number, nullptr, 0, c.key, record.data(), 24);
            if (status != Status::Ok) return status;
            c.offset = u64(record.data());
            c.expected = u32(record.data() + 8);
            if (!c.offset || !c.expected || c.expected % 40 || c.expected > 8000) return Status::Invalid;
            c.expected /= 40;
            c.initialized = true;
            c.stage = 1;
            break;
        case 1: // Copy a record before POI/name pages evict the list page.
            status = page(*data, c.offset, p);
            if (status != Status::Ok) return status;
            c.count = u32(p + 16);
            c.next = u64(p + 24);
            if (u16(p + 4) != 15 || u16(p + 20) != 40 || !c.count || c.count > 100 ||
                c.slot >= c.count || c.seen < c.slot || c.count > c.expected - (c.seen - c.slot)) return Status::Invalid;
            std::memcpy(record.data(), p + 64 + c.slot * 40, 40);
            new (&c.annotation) Annotation{};
            c.annotation.poi_row = u64(record.data());
            c.annotation.kind = u16(record.data() + 8);
            c.annotation.priority = u16(record.data() + 12);
            c.annotation.latitude = i32(record.data() + 16);
            c.annotation.longitude = i32(record.data() + 20);
            c.geometry = u64(record.data() + 24);
            if (c.annotation.kind < 1 || c.annotation.kind > 3 || record[10] > c.zoom || record[11] < c.zoom ||
                c.annotation.latitude < -900000000 || c.annotation.latitude > 900000000 ||
                c.annotation.longitude < -1800000000 || c.annotation.longitude >= 1800000000) return Status::Invalid;
            c.stage = c.geometry ? 2 : 3;
            break;
        case 2:
        {
            status = page(*geometry, c.geometry / 4096 * 4096, p);
            if (status != Status::Ok) return status;
            const auto at = c.geometry % 4096;
            if (u16(p + 4) != 16 || at < 64 || at > 4080 || u16(p + at) != 1) return Status::Invalid;
            const auto points = u32(p + at + 8);
            if (points > 8 || u32(p + at + 4) != points * 8 || points * 8 > 4096 - at - 16) return Status::Invalid;
            c.annotation.point_count = static_cast<uint8_t>(points);
            for (size_t i = 0; i < points * 2; ++i) c.annotation.path[i] = i32(p + at + 16 + i * 4);
            for (size_t i = 0; i < points; ++i)
                if (c.annotation.path[2 * i] < -900000000 || c.annotation.path[2 * i] > 900000000 ||
                    c.annotation.path[2 * i + 1] < -1800000000 || c.annotation.path[2 * i + 1] >= 1800000000) return Status::Invalid;
            c.stage = 3;
            break;
        }
        case 3: // POI row, name row, and text are separate resumable stages.
        {
            status = row(20, c.annotation.poi_row, 96, record.data());
            if (status != Status::Ok) return status;
            new (&c.poi) Poi{};
            c.poi.row = c.annotation.poi_row;
            std::memcpy(c.poi.id.data(), record.data(), 16);
            c.poi.latitude = i32(record.data() + 16);
            c.poi.longitude = i32(record.data() + 20);
            c.poi.category = u32(record.data() + 24);
            c.poi.kind = u32(record.data() + 28);
            c.poi.importance = u16(record.data() + 56);
            c.name = u64(record.data() + 32);
            const auto first = u64(record.data() + 40);
            const auto count = u32(record.data() + 48);
            const auto* names = section(22);
            if (c.poi.latitude < -900000000 || c.poi.latitude > 900000000 || c.poi.longitude < -1800000000 || c.poi.longitude >= 1800000000 ||
                !names || (count && (!first || first > names->count || count > names->count - first + 1 || c.name < first || c.name - first >= count)) ||
                (!count && (c.name || first))) return Status::Invalid;
            c.stage = c.name ? 4 : 6;
            break;
        }
        case 4:
            status = row(22, c.name, 48, record.data());
            if (status != Status::Ok) return status;
            if (u64(record.data()) != c.poi.row) return Status::Invalid;
            c.text = u64(record.data() + 8);
            c.stage = 5;
            break;
        case 5:
            status = string(c.text, c.poi.name, sizeof(c.poi.name));
            if (status != Status::Ok) return status;
            c.stage = 6;
            break;
        case 6:
            ++c.seen;
            ++c.slot;
            if (!visitor(context, c.annotation, c.poi))
            {
                c.complete = true;
                return Status::Cancelled;
            }
            if (c.slot == c.count)
            {
                if (c.seen == c.expected && c.next) return Status::Invalid;
                if (!c.next)
                {
                    if (c.seen != c.expected) return Status::Invalid;
                    c.complete = true;
                    return Status::Ok;
                }
                c.offset = c.next;
                c.slot = 0;
            }
            c.stage = 1;
            break;
        case 7: // Optional display rows: one contiguous table, no name joins.
        {
            status = row(43, c.fast_first + c.seen, 176, c.fast_record.data());
            if (status != Status::Ok) return status;
            const auto* r = c.fast_record.data();
            new (&c.annotation) Annotation{};
            new (&c.poi) Poi{};
            std::memcpy(c.poi.id.data(), r, 16);
            c.annotation.latitude = c.poi.latitude = i32(r + 16);
            c.annotation.longitude = c.poi.longitude = i32(r + 20);
            c.annotation.kind = u16(r + 24);
            c.annotation.priority = u16(r + 26);
            c.annotation.point_count = r[28];
            const auto length = r[29];
            if (c.annotation.kind < 1 || c.annotation.kind > 3 || c.annotation.point_count > 8 || length > 79 ||
                u16(r + 30) || r[32 + length] || (length && !validUtf8(r + 32, length)) ||
                c.annotation.latitude < -900000000 || c.annotation.latitude > 900000000 ||
                c.annotation.longitude < -1800000000 || c.annotation.longitude >= 1800000000) return Status::Invalid;
            std::memcpy(c.poi.name, r + 32, length);
            for (size_t i = 0; i < c.annotation.point_count * 2; ++i) c.annotation.path[i] = i32(r + 112 + i * 4);
            for (size_t i = 0; i < c.annotation.point_count; ++i)
                if (c.annotation.path[2 * i] < -900000000 || c.annotation.path[2 * i] > 900000000 ||
                    c.annotation.path[2 * i + 1] < -1800000000 || c.annotation.path[2 * i + 1] >= 1800000000) return Status::Invalid;
            ++c.seen;
            if (!visitor(context, c.annotation, c.poi))
            {
                c.complete = true;
                return Status::Cancelled;
            }
            if (c.seen == c.expected)
            {
                c.complete = true;
                return Status::Ok;
            }
            break;
        }
        default:
            return Status::Invalid;
        }
    }
    return Status::More;
}
Status Reader::metadata()
{
    const auto* s = section(1);
    if (!s) return Status::Invalid;
    auto& r = workspace_.record;
    for (uint64_t at = 0; at < s->length;)
    {
        if (!bounded(at, 8, s->length)) return Status::Invalid;
        auto status = read(s->offset + at, r.data(), 8);
        if (status != Status::Ok) return status;
        const auto tag = u16(r.data()), flags = u16(r.data() + 2);
        const auto length = u32(r.data() + 4);
        if (!bounded(at + 8, length, s->length) || (tag > 17 && (flags & 1U))) return Status::Invalid;
        if (tag == 6)
        {
            if (length != 2) return Status::Invalid;
            status = read(s->offset + at + 8, r.data(), 2);
            if (status != Status::Ok) return status;
            package_.specificity = u16(r.data());
        }
        at = (at + 8 + length + 7) / 8 * 8;
        if (at > s->length) return Status::Invalid;
    }
    return Status::Ok;
}
Status Reader::categoryName(uint32_t id, char* output, size_t capacity)
{
    if (!isOpen() || !output || !capacity) return Status::Invalid;
    output[0] = 0;
    const auto* s = section(1);
    auto& r = workspace_.record;
    for (uint64_t at = 0; at < s->length;)
    {
        auto status = read(s->offset + at, r.data(), 8);
        if (status != Status::Ok) return status;
        const auto tag = u16(r.data());
        const auto length = u32(r.data() + 4);
        if (!bounded(at + 8, length, s->length)) return Status::Invalid;
        if (tag == 11)
        {
            for (uint64_t item = 0; item < length;)
            {
                if (!bounded(item, 8, length)) return Status::Invalid;
                status = read(s->offset + at + 8 + item, r.data(), 8);
                if (status != Status::Ok) return status;
                const auto key = u32(r.data());
                const auto bytes = u16(r.data() + 4);
                if (bytes > 512 || !bounded(item + 8, bytes, length)) return Status::Invalid;
                if (key == id)
                {
                    if (bytes >= capacity) return Status::Invalid;
                    status = read(s->offset + at + 16 + item, reinterpret_cast<uint8_t*>(output), bytes);
                    if (status != Status::Ok) return status;
                    output[bytes] = 0;
                    return validUtf8(reinterpret_cast<const uint8_t*>(output), bytes) ? Status::Ok : Status::Invalid;
                }
                item = (item + 8 + bytes + 7) / 8 * 8;
            }
            return Status::Missing;
        }
        at = (at + 8 + length + 7) / 8 * 8;
    }
    return Status::Missing;
}
Status Reader::queryBounds(const Bounds& bounds, PoiVisitor visitor, void* context)
{
    const auto* s = section(24);
    if (!isOpen() || !s || !visitor || bounds.west > bounds.east || bounds.south > bounds.north) return Status::Invalid;
    return s->root ? visitSpatial(*s, s->root, 0, bounds, visitor, context) : Status::Ok;
}
Status Reader::visitSpatial(const Section& section, uint64_t offset, unsigned depth, const Bounds& bounds, PoiVisitor visitor, void* context)
{
    if (depth > 16) return Status::Invalid;
    const uint8_t* p = nullptr;
    auto status = page(section, offset, p);
    if (status != Status::Ok) return status;
    const auto count = u32(p + 16);
    const auto type = u16(p + 4);
    const auto level = u16(p + 6);
    if (!count || count > 126 || u16(p + 20) != 32 || (type != 13 && type != 14) || (type == 14 && level)) return Status::Invalid;
    for (uint32_t slot = 0; slot < count; ++slot)
    {
        status = page(section, offset, p);
        if (status != Status::Ok) return status;
        const auto* item = p + 64 + slot * 32;
        const auto child = u64(item + 16);
        const auto rows = u32(item + 24);
        if (!bounds.intersects(box(item))) continue;
        if (type == 13)
        {
            const uint8_t* child_page = nullptr;
            status = page(section, child, child_page);
            if (status != Status::Ok) return status;
            if (u16(child_page + 6) + 1 != level) return Status::Invalid;
            status = visitSpatial(section, child, depth + 1, bounds, visitor, context);
            if (status != Status::Ok) return status;
        }
        else
        {
            if (!child || !rows || rows > 42 || child > poiCount() || rows > poiCount() - child + 1) return Status::Invalid;
            for (uint32_t i = 0; i < rows; ++i)
            {
                status = readPoi(child + i, workspace_.poi);
                if (status != Status::Ok) return status;
                if (bounds.contains(workspace_.poi.latitude, workspace_.poi.longitude) && !visitor(context, workspace_.poi)) return Status::Cancelled;
            }
        }
    }
    return Status::Ok;
}
} // namespace tmap
