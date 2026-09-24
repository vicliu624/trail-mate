#pragma once

#include <FsLib/FsLib.h>
#include <cstddef>

namespace platform::esp::arduino_common::storage
{
enum class SdPathEvidence
{
    Present,
    Absent,
    Uncertain
};

// Allocate as runtime-owned scratch, never as an ESP automatic local. Only
// used after failed open; successful tile reads pay no directory-scan cost.
struct SdPathProbeScratch
{
    FsFile directory;
    FsFile entry;
    char name[256]{};
};

// A failed boolean open is not proof of absence. Walk the directory using its
// end/error distinction. Generated map paths are ASCII; unsupported relative
// components/non-ASCII inputs stay uncertain rather than entering a negative
// cache. The enclosing filesystem lock must cover the complete probe.
inline SdPathEvidence probe_sd_path(FsVolume& volume, const char* path, SdPathProbeScratch& scratch)
{
    scratch.entry.close();
    scratch.directory.close();
    struct Close
    {
        SdPathProbeScratch& scratch;
        ~Close()
        {
            scratch.entry.close();
            scratch.directory.close();
        }
    } close{scratch};
    if (!path || path[0] != '/' || !scratch.directory.openRoot(&volume)) return SdPathEvidence::Uncertain;
    while (*path == '/') ++path;
    if (!*path) return SdPathEvidence::Present;
    while (*path)
    {
        const char* end = path;
        while (*end && *end != '/')
        {
            const auto c = static_cast<unsigned char>(*end);
            if (c < 32 || c >= 127) return SdPathEvidence::Uncertain;
            ++end;
        }
        const auto length = static_cast<std::size_t>(end - path);
        if (length == 0 || length > 255 || (length == 1 && path[0] == '.') ||
            (length == 2 && path[0] == '.' && path[1] == '.')) return SdPathEvidence::Uncertain;
        bool found = false;
        while (scratch.entry.openNext(&scratch.directory, O_RDONLY))
        {
            const auto name_length = scratch.entry.getName(scratch.name, sizeof(scratch.name));
            if (name_length == 0 || scratch.entry.getError()) return SdPathEvidence::Uncertain;
            bool equal = name_length == length;
            for (std::size_t i = 0; equal && i < length; ++i)
            {
                const auto lower = [](unsigned char c)
                { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
                equal = lower(static_cast<unsigned char>(scratch.name[i])) == lower(static_cast<unsigned char>(path[i]));
            }
            if (equal)
            {
                found = true;
                break;
            }
            if (!scratch.entry.close()) return SdPathEvidence::Uncertain;
        }
        if (!found)
        {
            if (scratch.directory.getError()) return SdPathEvidence::Uncertain;
            // openNext also fails on malformed entries (e.g. FAT LFN checksum)
            // without setting the parent's read-error bit. Require the actual
            // zero end-of-directory marker, shared by FAT and exFAT. A full
            // directory with no marker stays uncertain rather than risking a
            // false negative cache entry. Only one byte of the last 32-byte
            // directory record is inspected; no private SdFat cache access.
            const auto position = scratch.directory.curPosition();
            if (position < 32 || !scratch.directory.seekSet(position - 32)) return SdPathEvidence::Uncertain;
            uint8_t marker = 0xFF;
            if (scratch.directory.read(&marker, 1) != 1 || scratch.directory.getError()) return SdPathEvidence::Uncertain;
            return marker == 0 ? SdPathEvidence::Absent : SdPathEvidence::Uncertain;
        }
        while (*end == '/') ++end;
        if (!*end) return SdPathEvidence::Present;
        if (!scratch.entry.isDir()) return SdPathEvidence::Uncertain;
        if (!scratch.directory.close()) return SdPathEvidence::Uncertain;
        scratch.directory.move(&scratch.entry);
        path = end;
    }
    return SdPathEvidence::Uncertain;
}
} // namespace platform::esp::arduino_common::storage
