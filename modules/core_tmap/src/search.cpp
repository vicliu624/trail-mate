#include "tmap/reader.h"
#include <algorithm>
#include <cstring>
#include <new>

namespace tmap
{
namespace
{
uint16_t u16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
uint32_t u32(const uint8_t* p) { return uint32_t(u16(p)) | (uint32_t(u16(p + 2)) << 16); }
uint64_t u64(const uint8_t* p) { return uint64_t(u32(p)) | (uint64_t(u32(p + 4)) << 32); }
bool scalar(const char* text, size_t size, size_t& at, uint32_t& value)
{
    if (at >= size) return false;
    const auto first = static_cast<uint8_t>(text[at++]);
    if (first < 128)
    {
        value = first;
        return first != 0;
    }
    unsigned more = 0;
    uint32_t minimum = 0;
    if (first >= 0xc2 && first <= 0xdf)
    {
        value = first & 31;
        more = 1;
        minimum = 0x80;
    }
    else if (first >= 0xe0 && first <= 0xef)
    {
        value = first & 15;
        more = 2;
        minimum = 0x800;
    }
    else if (first >= 0xf0 && first <= 0xf4)
    {
        value = first & 7;
        more = 3;
        minimum = 0x10000;
    }
    else return false;
    if (more > size - at) return false;
    while (more--)
    {
        const auto c = static_cast<uint8_t>(text[at++]);
        if ((c & 0xc0) != 0x80) return false;
        value = (value << 6) | (c & 63);
    }
    return value >= minimum && value <= 0x10ffff && !(value >= 0xd800 && value <= 0xdfff);
}
bool whitespace(uint32_t c)
{
    return (c >= 9 && c <= 13) || c == 32 || c == 0x85 || c == 0xa0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}
bool textEntry(const uint8_t* p, size_t slot, bool inner, const uint8_t*& key, size_t& size, const uint8_t*& value)
{
    const auto count = u32(p + 16);
    if (count > 2016 || slot >= count) return false;
    const auto end = 4096 - count * 2;
    const auto at = u16(p + 4096 - 2 * (slot + 1));
    if (at < 64 || at > end || end - at < 4) return false;
    size = u16(p + at);
    const auto bytes = u16(p + at + 2);
    if (!size || size > 512 || bytes != (inner ? 8U : 32U) || size + bytes > end - at - 4) return false;
    key = p + at + 4;
    value = key + size;
    return true;
}
Posting reference(const uint8_t* value) { return {u64(value), u64(value + 8), u32(value + 16)}; }
bool validPosting(const Posting& p, const Section& section, uint64_t names)
{
    if (!p.offset || p.offset % 4096 || !p.count || p.count > names ||
        p.blocks != p.count / 504 + (p.count % 504 ? 1 : 0)) return false;
    const auto bytes = (uint64_t(p.blocks) / 168 + (p.blocks % 168 ? 1 : 0)) * 4096;
    return p.offset <= section.length && bytes <= section.length - p.offset;
}
int compare(const uint8_t* key, size_t size, const char* query, size_t query_size)
{
    const auto n = std::memcmp(key, query, std::min(size, query_size));
    return n ? n : size < query_size ? -1
               : size > query_size   ? 1
                                     : 0;
}
bool prefix(const uint8_t* key, size_t size, const char* query, size_t query_size)
{
    return size >= query_size && std::memcmp(key, query, query_size) == 0;
}
} // namespace

bool Reader::normalizeName(const char* input, size_t bytes, char* output, size_t capacity)
{
    if (!input || !output || !capacity || bytes > 512) return false;
    size_t at = 0, used = 0;
    bool pending_space = false;
    while (at < bytes)
    {
        uint32_t c = 0;
        if (!scalar(input, bytes, at, c)) return false;
        if (c >= 0xff01 && c <= 0xff5e) c -= 0xfee0;
        if (c >= 'A' && c <= 'Z') c += 32;
        if (whitespace(c))
        {
            pending_space = used != 0;
            continue;
        }
        const auto n = c < 0x80 ? 1U : c < 0x800 ? 2U
                                   : c < 0x10000 ? 3U
                                                 : 4U;
        if (used + n + (pending_space ? 1 : 0) >= capacity) return false;
        if (pending_space) output[used++] = ' ';
        pending_space = false;
        if (n == 1) output[used++] = static_cast<char>(c);
        else
        {
            output[used++] = static_cast<char>((n == 2 ? 0xc0 : n == 3 ? 0xe0
                                                                       : 0xf0) |
                                               (c >> (6 * (n - 1))));
            for (unsigned part = n - 1; part > 0; --part) output[used++] = static_cast<char>(0x80 | ((c >> (6 * (part - 1))) & 63));
        }
    }
    output[used] = 0;
    return true;
}
Status Reader::beginSearch(const char* query, size_t bytes, SearchMode mode, SearchCursor& cursor)
{
    // Construct directly in caller storage; avoid a cursor-sized stack temporary.
    new (&cursor) SearchCursor{};
    if (!isOpen() || static_cast<unsigned>(mode) > 2 || !normalizeName(query, bytes, cursor.query, sizeof(cursor.query))) return Status::Invalid;
    cursor.mode = mode;
    cursor.build = package_.build;
    const auto size = std::strlen(cursor.query);
    if (!size) return Status::Ok;
    cursor.complete = false;
    return Status::Ok;
}
Status Reader::initializeSearch(SearchCursor& cursor, size_t& budget)
{
    const auto size = std::strlen(cursor.query);
    if (cursor.mode == SearchMode::Prefix)
    {
        --budget;
        const auto status = seekName(cursor);
        if (status == Status::Ok) cursor.initialized = true;
        return status;
    }
    if (cursor.mode == SearchMode::Exact)
    {
        --budget;
        const auto* names = section(30);
        auto& record = workspace_.record;
        if (!names) return Status::Invalid;
        const auto status = find(*names, KeyKind::Text, reinterpret_cast<const uint8_t*>(cursor.query), size, 0, record.data(), 32);
        if (status == Status::Missing)
        {
            cursor.complete = true;
            return Status::Ok;
        }
        if (status != Status::Ok) return status;
        if (u16(record.data() + 20) != 0) return Status::Invalid;
        cursor.posting = reference(record.data());
        if (!validPosting(cursor.posting, *section(32), section(22)->count)) return Status::Invalid;
        cursor.initialized = true;
        return Status::Ok;
    }
    if (!cursor.query_offset)
    {
        size_t at = 0;
        if (!scalar(cursor.query, size, at, cursor.previous_scalar)) return Status::Invalid;
        cursor.query_offset = static_cast<uint16_t>(at);
    }
    auto add = [&](uint64_t gram)
    {
        auto& r = workspace_.record;
        auto status = find(*section(31), KeyKind::Number, nullptr, 0, gram, r.data(), 32);
        if (status != Status::Ok) return status;
        const auto posting = reference(r.data());
        if (u16(r.data() + 20) || !validPosting(posting, *section(32), section(22)->count)) return Status::Invalid;
        for (size_t i = 0; i < cursor.filter_count; ++i)
            if (cursor.filters[i].offset == posting.offset) return Status::Ok;
        size_t position = 0;
        while (position < cursor.filter_count && cursor.filters[position].count <= posting.count) ++position;
        if (position < cursor.filters.size())
        {
            const auto end = std::min<size_t>(cursor.filter_count, cursor.filters.size() - 1);
            for (auto i = end; i > position; --i) cursor.filters[i] = cursor.filters[i - 1];
            cursor.filters[position] = posting;
            if (cursor.filter_count < cursor.filters.size()) ++cursor.filter_count;
        }
        return Status::Ok;
    };
    while (budget)
    {
        size_t at = cursor.query_offset;
        uint32_t current = 0;
        const bool single = at == size;
        if (!single && !scalar(cursor.query, size, at, current)) return Status::Invalid;
        const auto gram = single ? (UINT64_C(1) << 42) | (uint64_t(cursor.previous_scalar) << 21)
                                 : (UINT64_C(2) << 42) | (uint64_t(cursor.previous_scalar) << 21) | current;
        --budget;
        const auto status = add(gram);
        if (status == Status::Missing)
        {
            cursor.complete = true;
            return Status::Ok;
        }
        if (status != Status::Ok) return status;
        // Commit the position only after successful I/O; Busy retries this gram.
        cursor.query_offset = static_cast<uint16_t>(at);
        cursor.previous_scalar = current;
        if (single || at == size)
        {
            if (!cursor.filter_count) return Status::Invalid;
            cursor.posting = cursor.filters[0];
            cursor.initialized = true;
            return Status::Ok;
        }
    }
    return Status::More;
}
Status Reader::seekName(SearchCursor& cursor)
{
    const auto* dictionary = section(30);
    if (!dictionary || !dictionary->root)
    {
        cursor.complete = true;
        return Status::Ok;
    }
    auto offset = dictionary->root;
    unsigned expected = 32;
    const auto size = std::strlen(cursor.query);
    for (unsigned depth = 0; depth < 32; ++depth)
    {
        const uint8_t* p = nullptr;
        auto status = page(*dictionary, offset, p);
        if (status != Status::Ok) return status;
        const auto level = u16(p + 6), type = u16(p + 4);
        const auto count = u32(p + 16);
        if (!count || level >= expected || type != (level ? 6 : 7)) return Status::Invalid;
        size_t low = 0, high = count;
        const uint8_t* key = nullptr;
        const uint8_t* value = nullptr;
        size_t length = 0;
        while (low < high)
        {
            const auto mid = low + (high - low) / 2;
            if (!textEntry(p, mid, level != 0, key, length, value)) return Status::Invalid;
            if (compare(key, length, cursor.query, size) < 0) low = mid + 1;
            else high = mid;
        }
        if (!level)
        {
            cursor.leaf = offset;
            cursor.slot = static_cast<uint32_t>(low);
            return namePosting(cursor);
        }
        if (low == count)
        {
            cursor.complete = true;
            return Status::Ok;
        }
        if (!textEntry(p, low, true, key, length, value)) return Status::Invalid;
        offset = u64(value);
        expected = level;
    }
    return Status::Invalid;
}
Status Reader::namePosting(SearchCursor& cursor)
{
    const auto* dictionary = section(30);
    const auto query_size = std::strlen(cursor.query);
    if (!dictionary) return Status::Invalid;
    while (cursor.leaf)
    {
        const uint8_t* p = nullptr;
        auto status = page(*dictionary, cursor.leaf, p);
        if (status != Status::Ok) return status;
        if (u16(p + 4) != 7 || u16(p + 6)) return Status::Invalid;
        if (cursor.slot < u32(p + 16))
        {
            const uint8_t* key = nullptr;
            const uint8_t* value = nullptr;
            size_t length = 0;
            if (!textEntry(p, cursor.slot, false, key, length, value)) return Status::Invalid;
            if (!prefix(key, length, cursor.query, query_size))
            {
                cursor.complete = true;
                return Status::Ok;
            }
            cursor.posting = reference(value);
            cursor.position = 0;
            return u16(value + 20) == 0 && validPosting(cursor.posting, *section(32), section(22)->count) ? Status::Ok : Status::Invalid;
        }
        const auto next = u64(p + 24);
        if (++cursor.visited_leaves > dictionary->length / 4096) return Status::Invalid;
        cursor.leaf = next;
        cursor.slot = 0;
    }
    cursor.complete = true;
    return Status::Ok;
}
Status Reader::postingValue(const Posting& posting, uint64_t position, uint64_t& id)
{
    const auto* s = section(32);
    const auto names = section(22)->count;
    if (!s || position >= posting.count || !validPosting(posting, *s, names)) return Status::Invalid;
    const auto block = position / 504;
    const auto directory = posting.offset + block / 168 * 4096;
    const uint8_t* p = nullptr;
    auto status = page(*s, directory, p);
    if (status != Status::Ok) return status;
    if (u16(p + 4) != 11 || u16(p + 20) != 24 || u32(p + 16) > 168 || block % 168 >= u32(p + 16)) return Status::Invalid;
    const auto* item = p + 64 + block % 168 * 24;
    const auto first = u64(item), last = u64(item + 8), offset = u64(item + 16);
    if (!first || first > last || last > names) return Status::Invalid;
    status = page(*s, offset, p);
    if (status != Status::Ok) return status;
    const auto count = u32(p + 16);
    const auto slot = position % 504;
    if (u16(p + 4) != 12 || u16(p + 20) != 8 || !count || count > 504 || slot >= count ||
        u64(p + 64) != first || u64(p + 64 + (count - 1) * 8) != last) return Status::Invalid;
    id = u64(p + 64 + slot * 8);
    if (!id || id > names || (slot && u64(p + 64 + (slot - 1) * 8) >= id) || (slot + 1 < count && u64(p + 64 + (slot + 1) * 8) <= id)) return Status::Invalid;
    return Status::Ok;
}
Status Reader::postingContains(const Posting& posting, uint64_t id, bool& found)
{
    found = false;
    const auto* s = section(32);
    if (!s || !validPosting(posting, *s, section(22)->count)) return Status::Invalid;
    uint32_t low = 0, high = posting.blocks;
    while (low < high)
    {
        const auto mid = low + (high - low) / 2;
        const uint8_t* p = nullptr;
        auto status = page(*s, posting.offset + uint64_t(mid / 168) * 4096, p);
        if (status != Status::Ok) return status;
        if (u16(p + 4) != 11 || u16(p + 20) != 24 || u32(p + 16) > 168 || mid % 168 >= u32(p + 16)) return Status::Invalid;
        if (u64(p + 64 + (mid % 168) * 24 + 8) < id) low = mid + 1;
        else high = mid;
    }
    if (low == posting.blocks) return Status::Ok;
    uint64_t first = uint64_t(low) * 504;
    high = static_cast<uint32_t>(std::min<uint64_t>(504, posting.count - first));
    low = 0;
    while (low < high)
    {
        const auto mid = low + (high - low) / 2;
        uint64_t value = 0;
        const auto status = postingValue(posting, first + mid, value);
        if (status != Status::Ok) return status;
        if (value < id) low = mid + 1;
        else high = mid;
    }
    if (first + low < posting.count && low < 504)
    {
        uint64_t value = 0;
        const auto status = postingValue(posting, first + low, value);
        if (status != Status::Ok) return status;
        found = value == id;
    }
    return Status::Ok;
}
Status Reader::searchStep(SearchCursor& cursor, size_t budget, SearchVisitor visitor, void* context)
{
    if (!isOpen() || !visitor || !budget || cursor.build != package_.build) return Status::Invalid;
    if (cursor.complete) return Status::Ok;
    budget = std::min<size_t>(budget, 64);
    if (!cursor.initialized)
    {
        const auto status = initializeSearch(cursor, budget);
        if (status != Status::Ok) return status;
        if (cursor.complete) return Status::Ok;
        if (!budget) return Status::More;
    }
    const auto query_size = std::strlen(cursor.query);
    if (!query_size || query_size > 512) return Status::Invalid;
    for (size_t processed = 0; processed < budget;)
    {
        if (cursor.position >= cursor.posting.count)
        {
            if (cursor.mode != SearchMode::Prefix)
            {
                cursor.complete = true;
                return Status::Ok;
            }
            if (!cursor.advancing_name)
            {
                ++cursor.slot;
                cursor.advancing_name = true;
            }
            auto status = namePosting(cursor);
            if (status != Status::Ok) return status;
            cursor.advancing_name = false;
            if (cursor.complete) return Status::Ok;
        }
        uint64_t name = 0;
        auto status = postingValue(cursor.posting, cursor.position, name);
        if (status != Status::Ok) return status;
        bool accepted = true;
        for (size_t i = 1; i < cursor.filter_count && accepted; ++i)
        {
            status = postingContains(cursor.filters[i], name, accepted);
            if (status != Status::Ok) return status;
        }
        if (accepted)
        {
            auto& record = workspace_.record;
            status = row(22, name, 48, record.data());
            if (status != Status::Ok) return status;
            const auto poi = u64(record.data()), display = u64(record.data() + 8), normalized = u64(record.data() + 16);
            status = string(normalized, workspace_.matched_name.data(), workspace_.matched_name.size());
            if (status != Status::Ok) return status;
            const auto* text = workspace_.matched_name.data();
            const auto size = std::strlen(text);
            const auto match = std::strcmp(text, cursor.query) == 0 ? SearchMode::Exact : prefix(reinterpret_cast<const uint8_t*>(text), size, cursor.query, query_size) ? SearchMode::Prefix
                                                                                                                                                                         : SearchMode::Substring;
            accepted = static_cast<unsigned>(match) <= static_cast<unsigned>(cursor.mode) && std::strstr(text, cursor.query) != nullptr;
            if (accepted)
            {
                status = readPoi(poi, workspace_.poi);
                if (status != Status::Ok) return status;
                status = string(display, workspace_.matched_display.data(), workspace_.matched_display.size());
                if (status != Status::Ok) return status;
                status = administrativeLocation(poi, workspace_.administrative_path.data(), workspace_.administrative_path.size(),
                                                workspace_.administrative_levels, workspace_.administrative_flags);
                if (status != Status::Ok) return status;
                if (!visitor(context, workspace_.poi, match)) return Status::Cancelled;
            }
        }
        ++cursor.position;
        ++processed;
    }
    if (cursor.mode != SearchMode::Prefix && cursor.position == cursor.posting.count)
    {
        cursor.complete = true;
        return Status::Ok;
    }
    return Status::More;
}
} // namespace tmap
