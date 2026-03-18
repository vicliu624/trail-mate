#include "platform/ui/settings_store.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{

std::string makeScopedKey(const char* ns, const char* key)
{
    const char* scope = ns ? ns : "";
    const char* name = key ? key : "";
    return std::string(scope) + ":" + name;
}

std::map<std::string, int>& intStore()
{
    static std::map<std::string, int> store;
    return store;
}

std::map<std::string, bool>& boolStore()
{
    static std::map<std::string, bool> store;
    return store;
}

std::map<std::string, uint32_t>& uintStore()
{
    static std::map<std::string, uint32_t> store;
    return store;
}

std::map<std::string, std::vector<uint8_t>>& blobStore()
{
    static std::map<std::string, std::vector<uint8_t>> store;
    return store;
}

} // namespace

namespace platform::ui::settings_store
{

void put_int(const char* ns, const char* key, int value)
{
    if (!key)
    {
        return;
    }
    intStore()[makeScopedKey(ns, key)] = value;
}

void put_bool(const char* ns, const char* key, bool value)
{
    if (!key)
    {
        return;
    }
    boolStore()[makeScopedKey(ns, key)] = value;
}

void put_uint(const char* ns, const char* key, uint32_t value)
{
    if (!key)
    {
        return;
    }
    uintStore()[makeScopedKey(ns, key)] = value;
}

bool put_blob(const char* ns, const char* key, const void* data, std::size_t len)
{
    if (!key || (!data && len != 0))
    {
        return false;
    }
    auto& blob = blobStore()[makeScopedKey(ns, key)];
    blob.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + len);
    return true;
}

int get_int(const char* ns, const char* key, int default_value)
{
    if (!key)
    {
        return default_value;
    }
    const auto it = intStore().find(makeScopedKey(ns, key));
    return it == intStore().end() ? default_value : it->second;
}

bool get_bool(const char* ns, const char* key, bool default_value)
{
    if (!key)
    {
        return default_value;
    }
    const auto it = boolStore().find(makeScopedKey(ns, key));
    return it == boolStore().end() ? default_value : it->second;
}

uint32_t get_uint(const char* ns, const char* key, uint32_t default_value)
{
    if (!key)
    {
        return default_value;
    }
    const auto it = uintStore().find(makeScopedKey(ns, key));
    return it == uintStore().end() ? default_value : it->second;
}

bool get_blob(const char* ns, const char* key, std::vector<uint8_t>& out)
{
    out.clear();
    if (!key)
    {
        return false;
    }
    const auto it = blobStore().find(makeScopedKey(ns, key));
    if (it == blobStore().end())
    {
        return false;
    }
    out = it->second;
    return true;
}

void remove_keys(const char* ns, const char* const* keys, std::size_t key_count)
{
    if (!keys)
    {
        return;
    }
    for (std::size_t index = 0; index < key_count; ++index)
    {
        if (!keys[index])
        {
            continue;
        }
        const std::string scoped = makeScopedKey(ns, keys[index]);
        intStore().erase(scoped);
        boolStore().erase(scoped);
        uintStore().erase(scoped);
        blobStore().erase(scoped);
    }
}

void clear_namespace(const char* ns)
{
    const std::string prefix = std::string(ns ? ns : "") + ":";

    for (auto it = intStore().begin(); it != intStore().end();)
    {
        it = (it->first.rfind(prefix, 0) == 0) ? intStore().erase(it) : std::next(it);
    }
    for (auto it = boolStore().begin(); it != boolStore().end();)
    {
        it = (it->first.rfind(prefix, 0) == 0) ? boolStore().erase(it) : std::next(it);
    }
    for (auto it = uintStore().begin(); it != uintStore().end();)
    {
        it = (it->first.rfind(prefix, 0) == 0) ? uintStore().erase(it) : std::next(it);
    }
    for (auto it = blobStore().begin(); it != blobStore().end();)
    {
        it = (it->first.rfind(prefix, 0) == 0) ? blobStore().erase(it) : std::next(it);
    }
}

} // namespace platform::ui::settings_store
