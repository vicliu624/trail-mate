/**
 * @file blob_store_io.h
 * @brief Shared raw blob I/O helpers for legacy Arduino storage shells
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace chat
{
namespace infra
{

struct PreferencesBlobMetadata
{
    size_t len = 0;
    bool has_version = false;
    uint8_t version = 0;
    bool has_crc = false;
    uint32_t crc = 0;
};

bool loadRawBlobFromSd(const char* path, std::vector<uint8_t>& out);
bool saveRawBlobToSd(const char* path, const uint8_t* data, size_t len);
bool loadRawBlobFromPreferences(const char* ns, const char* key, std::vector<uint8_t>& out);
bool saveRawBlobToPreferences(const char* ns, const char* key, const uint8_t* data, size_t len);
void clearRawBlobFromPreferences(const char* ns, const char* key);

bool loadRawBlobFromPreferencesWithMetadata(const char* ns, const char* key,
                                            const char* version_key,
                                            const char* crc_key,
                                            std::vector<uint8_t>& out,
                                            PreferencesBlobMetadata* meta);
bool saveRawBlobToPreferencesWithMetadata(const char* ns, const char* key,
                                          const char* version_key,
                                          const char* crc_key,
                                          const uint8_t* data, size_t len,
                                          const PreferencesBlobMetadata* meta,
                                          bool retry_after_clear);
void clearPreferencesKeys(const char* ns,
                          const char* key_a,
                          const char* key_b = nullptr,
                          const char* key_c = nullptr);

} // namespace infra
} // namespace chat
