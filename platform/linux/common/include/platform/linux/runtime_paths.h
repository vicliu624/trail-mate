#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace platform::linux_runtime
{

// ---------------------------------------------------------------------------
// RuntimePaths
//
// Centralised path resolution for all Linux runtime implementations.
// Call resolve_paths() once early in the process and pass the result
// (or individual roots) into every runtime that needs storage.
// ---------------------------------------------------------------------------

struct RuntimePaths
{
    std::filesystem::path settings_root;
    std::filesystem::path sd_root;
    std::filesystem::path cache_root;
    std::filesystem::path state_root;
};

/// Compute the canonical set of runtime directories.
///
/// Precedence (highest first):
///   1. Environment variables:
///      - TRAIL_MATE_SETTINGS_ROOT
///      - TRAIL_MATE_SD_ROOT
///      - TRAIL_MATE_CACHE_ROOT
///   2. On Linux:   $HOME/.trailmate_cardputer_zero
///   3. On Windows: %APPDATA%/TrailMateCardputerZero
///   4. Fallback:   cwd/.trailmate_cardputer_zero
///
/// sd_root defaults to <settings_root>/sdcard.
/// cache_root defaults to <settings_root>/cache.
/// state_root is the same as settings_root.
RuntimePaths resolve_paths();

/// Convenience: settings file for a given namespace.
/// Returns <settings_root>/settings/<sanitized_ns>.kv
std::filesystem::path settings_file(const char* ns);

/// Convenience: SQLite database used by Linux runtime state.
/// Returns <settings_root>/trailmate.sqlite3
std::filesystem::path sqlite_database_path();

/// Convenience: child path under sd_root.
std::filesystem::path sd_child(std::string_view relative);

/// Ensure a directory tree exists. Returns true on success.
bool ensure_directory(const std::filesystem::path& path);

// ---------------------------------------------------------------------------
// Path safety
// ---------------------------------------------------------------------------

/// Resolve `child` relative to `root`, return the canonical result in `out`.
/// Returns false and leaves `out` empty when:
///   - `child` is empty
///   - `child` is absolute
///   - `child` contains ".." segments
///   - The resolved path is outside `root`
///   - Any filesystem error occurs
bool resolve_child_under_root(const std::filesystem::path& root,
                              std::string_view child,
                              std::filesystem::path& out);

/// Convenience: remove a file after verifying it lies under `root`.
/// Returns false on any safety violation or filesystem error.
bool safe_remove_under_root(const std::filesystem::path& root,
                            std::string_view child);

/// Convenience: open a file for writing under `root` with atomic rename.
/// Writes to a temp file, closes (which flushes user-space buffers), then
/// atomically renames over the target.  Does NOT fsync; a power-loss-safe
/// variant can be added later when needed on real devices.
/// Returns false on any safety violation or filesystem error.
bool safe_write_under_root(const std::filesystem::path& root,
                           std::string_view child,
                           const std::string& content);

} // namespace platform::linux_runtime
