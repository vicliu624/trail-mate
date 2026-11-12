#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using nvs_handle_t = uint32_t;
constexpr int ESP_OK = 0, ESP_ERR_NVS_NOT_FOUND = 1, NVS_READWRITE = 0;
namespace fake_nvs
{
inline std::map<std::string, std::vector<uint8_t>> durable, pending;
inline bool fail_commit = false, fail_read = false;
inline unsigned commits = 0;
} // namespace fake_nvs
inline int nvs_open(const char*, int, nvs_handle_t* out)
{
    *out = 1;
    return 0;
}
inline void nvs_close(nvs_handle_t) { fake_nvs::pending.clear(); }
inline int nvs_get_blob(nvs_handle_t, const char* key, void* output, size_t* size)
{
    if (fake_nvs::fail_read) return 2;
    auto it = fake_nvs::durable.find(key);
    if (it == fake_nvs::durable.end()) return ESP_ERR_NVS_NOT_FOUND;
    if (*size < it->second.size()) return 2;
    std::memcpy(output, it->second.data(), it->second.size());
    *size = it->second.size();
    return 0;
}
inline int nvs_set_blob(nvs_handle_t, const char* key, const void* input, size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(input);
    fake_nvs::pending[key] = {bytes, bytes + size};
    return 0;
}
inline int nvs_commit(nvs_handle_t)
{
    ++fake_nvs::commits;
    if (fake_nvs::fail_commit) return 2;
    for (const auto& item : fake_nvs::pending) fake_nvs::durable[item.first] = item.second;
    fake_nvs::pending.clear();
    return 0;
}
