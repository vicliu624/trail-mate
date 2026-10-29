#pragma once
#include <cstdint>
#include <string_view>
namespace track_sharing
{
// Reserved immutable route names. References from KML never supply SD paths.
inline bool isInstalledPackageAssetId(std::string_view id)
{
    if (id.size() != 66 || id.substr(0, 2) != "p-") return false;
    for (size_t i = 2; i < 66; ++i)
        if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) return false;
    return true;
}
inline bool isInstalledPackageRoute(std::string_view path)
{
    return path.size() == 78 && path.substr(0, 8) == "/routes/" && path.substr(74) == ".kml" && isInstalledPackageAssetId(path.substr(8, 66));
}
// Platform route roots may be configurable host paths. Only a direct child of
// the supplied root is recognized; basename matches elsewhere confer no status.
inline bool isInstalledPackageRouteAtRoot(std::string_view path, std::string_view root)
{
    while (root.size() > 1 && root.back() == '/') root.remove_suffix(1);
    if (root.empty() || path.size() != root.size() + 71 || path.substr(0, root.size()) != root || path[root.size()] != '/') return false;
    const auto name = path.substr(root.size() + 1);
    return name.substr(66) == ".kml" && isInstalledPackageAssetId(name.substr(0, 66));
}
inline bool installedPackageRevision(std::string_view path, uint8_t output[32])
{
    if (!output || !isInstalledPackageRoute(path)) return false;
    const auto digit = [](char c)
    { return c <= '9' ? unsigned(c - '0') : unsigned(c - 'a' + 10); };
    for (size_t i = 0; i < 32; ++i) output[i] = uint8_t((digit(path[10 + i * 2]) << 4) | digit(path[11 + i * 2]));
    return true;
}
} // namespace track_sharing
