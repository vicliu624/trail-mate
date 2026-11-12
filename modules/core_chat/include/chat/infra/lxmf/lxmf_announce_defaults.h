#pragma once

#include <cstdint>

namespace chat::lxmf
{
// One default for embedded, Linux radio and Linux gateway advertisements.
// First announcements, explicit broadcasts and path responses have their own
// scheduling; this interval applies after a successful periodic announcement.
inline constexpr uint32_t kDefaultAnnounceIntervalMs = 4U * 60U * 60U * 1000U;
inline constexpr char kDefaultAnnounceDisplayName[] = "Anonymous Peer";

// Saved names have no provenance marker. Never infer that a nonempty name is
// an old factory default: it may be the user's explicit choice.
inline const char* announceDisplayName(const char* long_name, const char* short_name = nullptr)
{
    if (long_name && long_name[0]) return long_name;
    if (short_name && short_name[0]) return short_name;
    return kDefaultAnnounceDisplayName;
}
} // namespace chat::lxmf
