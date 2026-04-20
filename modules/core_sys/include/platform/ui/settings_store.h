#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace platform::ui::settings_store
{

void put_int(const char* ns, const char* key, int value);
void put_bool(const char* ns, const char* key, bool value);
void put_uint(const char* ns, const char* key, uint32_t value);
bool put_string(const char* ns, const char* key, const char* value);
bool put_blob(const char* ns, const char* key, const void* data, std::size_t len);
int get_int(const char* ns, const char* key, int default_value);
bool get_bool(const char* ns, const char* key, bool default_value);
uint32_t get_uint(const char* ns, const char* key, uint32_t default_value);
bool get_string(const char* ns, const char* key, std::string& out);
bool get_blob(const char* ns, const char* key, std::vector<uint8_t>& out);
void remove_keys(const char* ns, const char* const* keys, std::size_t key_count);
void clear_namespace(const char* ns);

} // namespace platform::ui::settings_store
