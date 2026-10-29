#include "platform/linux/resource_pack_policy.h"
#include "platform/linux/build_identity.h"
#include <algorithm>
#include <mutex>
#include <optional>
#include <string_view>

namespace platform::linux_runtime
{
namespace
{
struct PolicyState
{
    std::mutex mutex;
    std::optional<ResourcePackPolicy> policy;
};
PolicyState& state()
{
    static PolicyState value;
    return value;
}
using Parts = std::vector<std::string_view>;
Parts split(std::string_view text, char delimiter)
{
    Parts result;
    for (;;)
    {
        const auto end = text.find(delimiter);
        result.push_back(text.substr(0, end));
        if (end == text.npos) return result;
        text.remove_prefix(end + 1);
    }
}
bool numeric(std::string_view value)
{
    return !value.empty() && std::all_of(value.begin(), value.end(), [](char ch)
                                         { return ch >= '0' && ch <= '9'; });
}
bool identifiers(std::string_view text, bool prerelease)
{
    for (const auto part : split(text, '.'))
    {
        if (part.empty() || !std::all_of(part.begin(), part.end(), [](char ch)
                                         { return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '-'; })) return false;
        if (prerelease && numeric(part) && part.size() > 1 && part[0] == '0') return false;
    }
    return true;
}
struct Version
{
    Parts core;
    Parts pre;
};
std::optional<Version> parse(std::string_view text)
{
    if (text.size() > 256) return {};
    if (!text.empty() && text.front() == 'v') text.remove_prefix(1);
    if (const auto plus = text.find('+'); plus != text.npos)
    {
        if (!identifiers(text.substr(plus + 1), false)) return {};
        text = text.substr(0, plus);
    }
    Version version;
    if (const auto dash = text.find('-'); dash != text.npos)
    {
        const auto pre = text.substr(dash + 1);
        if (!identifiers(pre, true)) return {};
        version.pre = split(pre, '.');
        text = text.substr(0, dash);
    }
    version.core = split(text, '.');
    if (version.core.size() != 3) return {};
    for (const auto part : version.core)
        if (!numeric(part) || (part.size() > 1 && part[0] == '0')) return {};
    return version;
}
int number_compare(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    return a == b ? 0 : a < b ? -1
                              : 1;
}
bool meets_minimum(const std::string& current, const std::string& minimum)
{
    if (minimum.empty()) return true;
    const auto a = parse(current), b = parse(minimum);
    if (!a || !b) return false;
    for (size_t i = 0; i < 3; ++i)
        if (const int comparison = number_compare(a->core[i], b->core[i]); comparison) return comparison > 0;
    if (a->pre.empty() || b->pre.empty()) return b->pre.empty() ? a->pre.empty() : true;
    for (size_t i = 0; i < std::min(a->pre.size(), b->pre.size()); ++i)
    {
        const auto left = a->pre[i], right = b->pre[i];
        if (left == right) continue;
        const bool x = numeric(left), y = numeric(right);
        if (x != y) return !x;
        return x ? number_compare(left, right) > 0 : left > right;
    }
    return a->pre.size() >= b->pre.size();
}
std::string lower(std::string value)
{
    for (char& ch : value)
        if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    return value;
}
} // namespace

bool configure_resource_pack_policy(const ResourcePackPolicy& policy)
{
    auto& shared = state();
    std::lock_guard<std::mutex> lock(shared.mutex);
    if (shared.policy) return shared.policy->firmware_version == policy.firmware_version && shared.policy->memory_profile == policy.memory_profile;
    shared.policy = policy;
    return true;
}
ResourcePackPolicy resource_pack_policy()
{
    auto& shared = state();
    std::lock_guard<std::mutex> lock(shared.mutex);
    return shared.policy.value_or(ResourcePackPolicy{runtime_firmware_version(), {}});
}
void evaluate_resource_pack_compatibility(::ui::runtime::packs::PackageRecord& package, const ResourcePackPolicy& policy)
{
    package.compatible_firmware = meets_minimum(policy.firmware_version, package.min_firmware_version);
    package.compatible_memory_profile = package.supported_memory_profiles.empty() ||
                                        std::any_of(package.supported_memory_profiles.begin(), package.supported_memory_profiles.end(),
                                                    [&](const auto& supported)
                                                    { return !policy.memory_profile.empty() && lower(supported) == lower(policy.memory_profile); });
}
std::vector<std::string> resource_memory_profiles(const std::string& csv)
{
    std::vector<std::string> result;
    for (auto part : split(csv, ','))
    {
        const auto first = part.find_first_not_of(" \t\r\n");
        if (first == part.npos) continue;
        result.emplace_back(part.substr(first, part.find_last_not_of(" \t\r\n") - first + 1));
    }
    return result;
}
} // namespace platform::linux_runtime
