#pragma once

#include "geocaching/protocol/cmp_reader.h"

namespace geocaching::protocol
{

inline bool validRecordText(std::string_view text, bool multiline, bool require_content)
{
    bool content = false;
    std::size_t i = 0;
    while (i < text.size())
    {
        const auto lead = static_cast<std::uint8_t>(text[i++]);
        std::uint32_t cp = lead;
        unsigned continuation = 0;
        std::uint32_t minimum = 0;
        if (lead >= 0xc2 && lead <= 0xdf)
        {
            cp = lead & 0x1f;
            continuation = 1;
            minimum = 0x80;
        }
        else if (lead >= 0xe0 && lead <= 0xef)
        {
            cp = lead & 0x0f;
            continuation = 2;
            minimum = 0x800;
        }
        else if (lead >= 0xf0 && lead <= 0xf4)
        {
            cp = lead & 7;
            continuation = 3;
            minimum = 0x10000;
        }
        else if (lead >= 0x80) return false;
        if (continuation > text.size() - i) return false;
        for (unsigned n = 0; n < continuation; ++n)
        {
            const auto byte = static_cast<std::uint8_t>(text[i++]);
            if ((byte & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (byte & 0x3f);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) || cp == 0xfffe || cp == 0xffff) return false;
        if ((cp < 0x20 && !(multiline && (cp == 9 || cp == 10))) || (cp >= 0x7f && cp <= 0x9f)) return false;
        const bool whitespace = cp == 0x20 || cp == 9 || cp == 10 || cp == 0xa0 ||
                                cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200a) || cp == 0x2028 ||
                                cp == 0x2029 || cp == 0x202f || cp == 0x205f || cp == 0x3000;
        content = content || !whitespace;
    }
    return !require_content || content;
}

// Validates encoding and field semantics only. The returned views borrow input.
// Public-key/signature verification and cross-version checks are separate.
inline bool decodeGeocacheRecord(ByteView input, RecordView& out)
{
    out = {};
    if (!input.data || input.size == 0 || input.size > kMaxRecordBytes) return false;
    CmpReader reader(input);
    RecordView candidate;
    std::size_t fields = 0;
    std::uint64_t value = 0;
    std::int64_t coordinate = 0;
    if (!reader.array(fields, 16) || fields != 16 || !reader.unsignedInteger(value) || value != 1) return false;
    if (!reader.binary(candidate.author_public_key, 64) || candidate.author_public_key.size != 64 ||
        !reader.binary(candidate.creation_nonce, 16) || candidate.creation_nonce.size != 16) return false;
    if (!reader.unsignedInteger(value) || value == 0 || value > UINT32_MAX) return false;
    candidate.revision = static_cast<std::uint32_t>(value);
    if (candidate.revision == 1)
    {
        if (!reader.nil()) return false;
    }
    else if (!reader.binary(candidate.previous_hash, 32) || candidate.previous_hash.size != 32) return false;
    if (!reader.unsignedInteger(value) || value > 2) return false;
    candidate.state = static_cast<CacheState>(value);
    if (!reader.signedInteger(coordinate) || coordinate < -900000000 || coordinate > 900000000) return false;
    candidate.latitude_e7 = static_cast<std::int32_t>(coordinate);
    if (!reader.signedInteger(coordinate) || coordinate < -1800000000 || coordinate >= 1800000000) return false;
    candidate.longitude_e7 = static_cast<std::int32_t>(coordinate);
    if (!reader.text(candidate.name, kMaxNameBytes) || !validRecordText(candidate.name, false, true) ||
        !reader.text(candidate.description, kMaxDescriptionBytes) || !validRecordText(candidate.description, true, false) ||
        !reader.text(candidate.hint, kMaxHintBytes) || !validRecordText(candidate.hint, true, false)) return false;
    if (!reader.unsignedInteger(value) || value < 2 || value > 10) return false;
    candidate.difficulty_x2 = static_cast<std::uint8_t>(value);
    if (!reader.unsignedInteger(value) || value < 2 || value > 10) return false;
    candidate.terrain_x2 = static_cast<std::uint8_t>(value);
    if (!reader.unsignedInteger(value) || value > 5) return false;
    candidate.container_size = static_cast<ContainerSize>(value);
    if (!reader.unsignedInteger(candidate.created_at) || candidate.created_at > 253402300799ULL ||
        !reader.unsignedInteger(candidate.updated_at) || candidate.updated_at > 253402300799ULL || !reader.finished()) return false;
    candidate.encoded = input;
    out = candidate;
    return true;
}

} // namespace geocaching::protocol
