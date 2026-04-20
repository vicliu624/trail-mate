#pragma once

#include "platform/ui/settings_store.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "esp_err.h"
#include "nvs.h"

namespace platform::esp::common::settings_store_detail
{

constexpr std::size_t kMaxNvsKeyLength = 15;

struct StorageKeyAlias
{
    const char* key;
    const char* storage_key;
};

constexpr StorageKeyAlias kStorageKeyAliases[] = {
    // Legacy key kept for one-shot locale migration from int -> locale id.
    {"display_language", "disp_lang"},
    {"display_locale", "disp_locale"},
    {"screen_brightness", "screen_bright"},
    {"vibration_enabled", "vibe_enabled"},
    {"gauge_design_mah", "gauge_dsgn"},
};

inline const char* safe_label(const char* value)
{
    return value ? value : "<null>";
}

inline const char* bool_label(bool value)
{
    return value ? "true" : "false";
}

inline void logf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
}

inline const char* resolve_storage_key(const char* key)
{
    if (!key)
    {
        return nullptr;
    }

    for (const auto& alias : kStorageKeyAliases)
    {
        if (std::strcmp(alias.key, key) == 0)
        {
            return alias.storage_key;
        }
    }

    return key;
}

inline bool validate_storage_key(const char* op,
                                 const char* ns,
                                 const char* key,
                                 const char* storage_key)
{
    if (!storage_key || storage_key[0] == '\0')
    {
        logf("[CfgStore][%s][ERR] ns=%s key=%s storage_key=%s invalid_key=true\n",
             safe_label(op),
             safe_label(ns),
             safe_label(key),
             safe_label(storage_key));
        return false;
    }

    const std::size_t len = std::strlen(storage_key);
    if (len <= kMaxNvsKeyLength)
    {
        return true;
    }

    logf("[CfgStore][%s][ERR] ns=%s key=%s storage_key=%s len=%lu max=%lu\n",
         safe_label(op),
         safe_label(ns),
         safe_label(key),
         safe_label(storage_key),
         static_cast<unsigned long>(len),
         static_cast<unsigned long>(kMaxNvsKeyLength));
    return false;
}

inline bool open_namespace(const char* ns, bool read_only, nvs_handle_t* handle)
{
    if (!ns || !handle)
    {
        return false;
    }

    return nvs_open(ns, read_only ? NVS_READONLY : NVS_READWRITE, handle) == ESP_OK;
}

inline void log_open_failure(const char* op,
                             const char* ns,
                             const char* key,
                             const char* storage_key)
{
    logf("[CfgStore][%s][ERR] ns=%s key=%s storage_key=%s open=false\n",
         safe_label(op),
         safe_label(ns),
         safe_label(key),
         safe_label(storage_key));
}

inline bool commit_and_close(nvs_handle_t handle)
{
    const bool ok = nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok;
}

inline bool erase_key_if_present(nvs_handle_t handle, const char* storage_key)
{
    const esp_err_t err = nvs_erase_key(handle, storage_key);
    return err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND;
}

} // namespace platform::esp::common::settings_store_detail

namespace platform::ui::settings_store
{

void put_int(const char* ns, const char* key, int value)
{
    using namespace ::platform::esp::common::settings_store_detail;

    if (!key)
    {
        return;
    }

    const char* storage_key = resolve_storage_key(key);
    if (!validate_storage_key("WRITE", ns, key, storage_key))
    {
        return;
    }

    nvs_handle_t handle = 0;
    if (!open_namespace(ns, false, &handle))
    {
        log_open_failure("WRITE", ns, key, storage_key);
        return;
    }

    bool ok = nvs_set_i32(handle, storage_key, static_cast<int32_t>(value)) == ESP_OK;
    if (ok)
    {
        ok = commit_and_close(handle);
    }
    else
    {
        nvs_close(handle);
    }

    logf("[CfgStore][WRITE] ns=%s key=%s storage_key=%s type=int value=%d ok=%s\n",
         safe_label(ns),
         safe_label(key),
         safe_label(storage_key),
         value,
         bool_label(ok));
}

void put_bool(const char* ns, const char* key, bool value)
{
    using namespace ::platform::esp::common::settings_store_detail;

    if (!key)
    {
        return;
    }

    const char* storage_key = resolve_storage_key(key);
    if (!validate_storage_key("WRITE", ns, key, storage_key))
    {
        return;
    }

    nvs_handle_t handle = 0;
    if (!open_namespace(ns, false, &handle))
    {
        log_open_failure("WRITE", ns, key, storage_key);
        return;
    }

    bool ok = nvs_set_u8(handle, storage_key, value ? 1U : 0U) == ESP_OK;
    if (ok)
    {
        ok = commit_and_close(handle);
    }
    else
    {
        nvs_close(handle);
    }

    logf("[CfgStore][WRITE] ns=%s key=%s storage_key=%s type=bool value=%s ok=%s\n",
         safe_label(ns),
         safe_label(key),
         safe_label(storage_key),
         bool_label(value),
         bool_label(ok));
}

void put_uint(const char* ns, const char* key, uint32_t value)
{
    using namespace ::platform::esp::common::settings_store_detail;

    if (!key)
    {
        return;
    }

    const char* storage_key = resolve_storage_key(key);
    if (!validate_storage_key("WRITE", ns, key, storage_key))
    {
        return;
    }

    nvs_handle_t handle = 0;
    if (!open_namespace(ns, false, &handle))
    {
        log_open_failure("WRITE", ns, key, storage_key);
        return;
    }

    bool ok = nvs_set_u32(handle, storage_key, value) == ESP_OK;
    if (ok)
    {
        ok = commit_and_close(handle);
    }
    else
    {
        nvs_close(handle);
    }

    logf("[CfgStore][WRITE] ns=%s key=%s storage_key=%s type=uint value=%lu ok=%s\n",
         safe_label(ns),
         safe_label(key),
         safe_label(storage_key),
         static_cast<unsigned long>(value),
         bool_label(ok));
}

bool put_string(const char* ns, const char* key, const char* value)
{
    using namespace ::platform::esp::common::settings_store_detail;

    if (!key || !value)
    {
        return false;
    }

    const char* storage_key = resolve_storage_key(key);
    if (!validate_storage_key("WRITE", ns, key, storage_key))
    {
        return false;
    }

    nvs_handle_t handle = 0;
    if (!open_namespace(ns, false, &handle))
    {
        log_open_failure("WRITE", ns, key, storage_key);
        return false;
    }

    bool ok = false;
    if (value[0] == '\0')
    {
        ok = erase_key_if_present(handle, storage_key);
        if (ok)
        {
            ok = commit_and_close(handle);
        }
        else
        {
            nvs_close(handle);
        }

        logf("[CfgStore][REMOVE] ns=%s key=%s storage_key=%s type=string ok=%s\n",
             safe_label(ns),
             safe_label(key),
             safe_label(storage_key),
             bool_label(ok));
        return ok;
    }

    ok = nvs_set_str(handle, storage_key, value) == ESP_OK;
    if (ok)
    {
        ok = commit_and_close(handle);
    }
    else
    {
        nvs_close(handle);
    }

    logf("[CfgStore][WRITE] ns=%s key=%s storage_key=%s type=string len=%lu value=%s ok=%s\n",
         safe_label(ns),
         safe_label(key),
         safe_label(storage_key),
         static_cast<unsigned long>(std::strlen(value)),
         safe_label(value),
         bool_label(ok));
    return ok;
}

bool put_blob(const char* ns, const char* key, const void* data, std::size_t len)
{
    using namespace ::platform::esp::common::settings_store_detail;

    if (!key || (!data && len != 0))
    {
        return false;
    }

    const char* storage_key = resolve_storage_key(key);
    if (!validate_storage_key("WRITE", ns, key, storage_key))
    {
        return false;
    }

    nvs_handle_t handle = 0;
    if (!open_namespace(ns, false, &handle))
    {
        log_open_failure("WRITE", ns, key, storage_key);
        return false;
    }

    bool ok = false;
    if (len == 0)
    {
        ok = erase_key_if_present(handle, storage_key);
        if (ok)
        {
            ok = commit_and_close(handle);
        }
        else
        {
            nvs_close(handle);
        }

        logf("[CfgStore][REMOVE] ns=%s key=%s storage_key=%s type=blob ok=%s\n",
             safe_label(ns),
             safe_label(key),
             safe_label(storage_key),
             bool_label(ok));
        return ok;
    }

    ok = nvs_set_blob(handle, storage_key, data, len) == ESP_OK;
    if (ok)
    {
        ok = commit_and_close(handle);
    }
    else
    {
        nvs_close(handle);
    }

    logf("[CfgStore][WRITE] ns=%s key=%s storage_key=%s type=blob len=%lu ok=%s\n",
         safe_label(ns),
         safe_label(key),
         safe_label(storage_key),
         static_cast<unsigned long>(len),
         bool_label(ok));
    return ok;
}

int get_int(const char* ns, const char* key, int default_value)
{
    using namespace ::platform::esp::common::settings_store_detail;

    if (!key)
    {
        return default_value;
    }

    const char* storage_key = resolve_storage_key(key);
    if (!validate_storage_key("READ", ns, key, storage_key))
    {
        return default_value;
    }

    nvs_handle_t handle = 0;
    if (!open_namespace(ns, true, &handle))
    {
        log_open_failure("READ", ns, key, storage_key);
        return default_value;
    }

    int32_t value = static_cast<int32_t>(default_value);
    const bool exists = nvs_get_i32(handle, storage_key, &value) == ESP_OK;
    nvs_close(handle);

    logf("[CfgStore][READ] ns=%s key=%s storage_key=%s type=int source=%s value=%d default=%d\n",
         safe_label(ns),
         safe_label(key),
         safe_label(storage_key),
         exists ? "stored" : "default",
         static_cast<int>(value),
         default_value);
    return static_cast<int>(value);
}

bool get_bool(const char* ns, const char* key, bool default_value)
{
    using namespace ::platform::esp::common::settings_store_detail;

    if (!key)
    {
        return default_value;
    }

    const char* storage_key = resolve_storage_key(key);
    if (!validate_storage_key("READ", ns, key, storage_key))
    {
        return default_value;
    }

    nvs_handle_t handle = 0;
    if (!open_namespace(ns, true, &handle))
    {
        log_open_failure("READ", ns, key, storage_key);
        return default_value;
    }

    uint8_t value = default_value ? 1U : 0U;
    const bool exists = nvs_get_u8(handle, storage_key, &value) == ESP_OK;
    nvs_close(handle);

    logf("[CfgStore][READ] ns=%s key=%s storage_key=%s type=bool source=%s value=%s default=%s\n",
         safe_label(ns),
         safe_label(key),
         safe_label(storage_key),
         exists ? "stored" : "default",
         bool_label(value != 0U),
         bool_label(default_value));
    return value != 0U;
}

uint32_t get_uint(const char* ns, const char* key, uint32_t default_value)
{
    using namespace ::platform::esp::common::settings_store_detail;

    if (!key)
    {
        return default_value;
    }

    const char* storage_key = resolve_storage_key(key);
    if (!validate_storage_key("READ", ns, key, storage_key))
    {
        return default_value;
    }

    nvs_handle_t handle = 0;
    if (!open_namespace(ns, true, &handle))
    {
        log_open_failure("READ", ns, key, storage_key);
        return default_value;
    }

    uint32_t value = default_value;
    const bool exists = nvs_get_u32(handle, storage_key, &value) == ESP_OK;
    nvs_close(handle);

    logf("[CfgStore][READ] ns=%s key=%s storage_key=%s type=uint source=%s value=%lu default=%lu\n",
         safe_label(ns),
         safe_label(key),
         safe_label(storage_key),
         exists ? "stored" : "default",
         static_cast<unsigned long>(value),
         static_cast<unsigned long>(default_value));
    return value;
}

bool get_string(const char* ns, const char* key, std::string& out)
{
    using namespace ::platform::esp::common::settings_store_detail;

    out.clear();
    if (!key)
    {
        return false;
    }

    const char* storage_key = resolve_storage_key(key);
    if (!validate_storage_key("READ", ns, key, storage_key))
    {
        return false;
    }

    nvs_handle_t handle = 0;
    if (!open_namespace(ns, true, &handle))
    {
        log_open_failure("READ", ns, key, storage_key);
        return false;
    }

    std::size_t len = 0;
    const esp_err_t len_err = nvs_get_str(handle, storage_key, nullptr, &len);
    if (len_err != ESP_OK || len == 0)
    {
        logf("[CfgStore][READ] ns=%s key=%s storage_key=%s type=string source=%s len=0\n",
             safe_label(ns),
             safe_label(key),
             safe_label(storage_key),
             len_err == ESP_OK ? "empty" : "missing");
        nvs_close(handle);
        return false;
    }

    std::string buffer(len, '\0');
    std::size_t read = len;
    const bool ok =
        nvs_get_str(handle, storage_key, buffer.empty() ? nullptr : &buffer[0], &read) == ESP_OK &&
        read == len;
    nvs_close(handle);

    if (!ok)
    {
        logf("[CfgStore][READ] ns=%s key=%s storage_key=%s type=string source=stored len=%lu ok=false\n",
             safe_label(ns),
             safe_label(key),
             safe_label(storage_key),
             static_cast<unsigned long>(len));
        return false;
    }

    if (!buffer.empty() && buffer.back() == '\0')
    {
        buffer.pop_back();
    }
    out = std::move(buffer);

    logf("[CfgStore][READ] ns=%s key=%s storage_key=%s type=string source=stored len=%lu value=%s ok=true\n",
         safe_label(ns),
         safe_label(key),
         safe_label(storage_key),
         static_cast<unsigned long>(out.size()),
         safe_label(out.c_str()));
    return true;
}

bool get_blob(const char* ns, const char* key, std::vector<uint8_t>& out)
{
    using namespace ::platform::esp::common::settings_store_detail;

    out.clear();
    if (!key)
    {
        return false;
    }

    const char* storage_key = resolve_storage_key(key);
    if (!validate_storage_key("READ", ns, key, storage_key))
    {
        return false;
    }

    nvs_handle_t handle = 0;
    if (!open_namespace(ns, true, &handle))
    {
        log_open_failure("READ", ns, key, storage_key);
        return false;
    }

    std::size_t len = 0;
    const esp_err_t len_err = nvs_get_blob(handle, storage_key, nullptr, &len);
    if (len_err != ESP_OK || len == 0)
    {
        logf("[CfgStore][READ] ns=%s key=%s storage_key=%s type=blob source=%s len=0\n",
             safe_label(ns),
             safe_label(key),
             safe_label(storage_key),
             len_err == ESP_OK ? "empty" : "missing");
        nvs_close(handle);
        return false;
    }

    out.resize(len);
    std::size_t read = len;
    const bool ok = nvs_get_blob(handle, storage_key, out.data(), &read) == ESP_OK && read == len;
    nvs_close(handle);

    logf("[CfgStore][READ] ns=%s key=%s storage_key=%s type=blob source=stored len=%lu ok=%s\n",
         safe_label(ns),
         safe_label(key),
         safe_label(storage_key),
         static_cast<unsigned long>(len),
         bool_label(ok));

    if (!ok)
    {
        out.clear();
        return false;
    }

    return true;
}

void remove_keys(const char* ns, const char* const* keys, std::size_t key_count)
{
    using namespace ::platform::esp::common::settings_store_detail;

    if (!keys)
    {
        return;
    }

    nvs_handle_t handle = 0;
    if (!open_namespace(ns, false, &handle))
    {
        log_open_failure("REMOVE", ns, nullptr, nullptr);
        return;
    }

    bool changed = false;
    for (std::size_t i = 0; i < key_count; ++i)
    {
        if (!keys[i])
        {
            continue;
        }

        const char* storage_key = resolve_storage_key(keys[i]);
        if (!validate_storage_key("REMOVE", ns, keys[i], storage_key))
        {
            continue;
        }

        const bool ok = erase_key_if_present(handle, storage_key);
        changed = changed || ok;
        logf("[CfgStore][REMOVE] ns=%s key=%s storage_key=%s ok=%s\n",
             safe_label(ns),
             safe_label(keys[i]),
             safe_label(storage_key),
             bool_label(ok));
    }

    if (changed)
    {
        const bool ok = commit_and_close(handle);
        if (!ok)
        {
            logf("[CfgStore][REMOVE][ERR] ns=%s commit=false\n", safe_label(ns));
        }
        return;
    }

    nvs_close(handle);
}

void clear_namespace(const char* ns)
{
    using namespace ::platform::esp::common::settings_store_detail;

    nvs_handle_t handle = 0;
    if (!open_namespace(ns, false, &handle))
    {
        log_open_failure("CLEAR", ns, nullptr, nullptr);
        return;
    }

    bool ok = nvs_erase_all(handle) == ESP_OK;
    if (ok)
    {
        ok = commit_and_close(handle);
    }
    else
    {
        nvs_close(handle);
    }

    logf("[CfgStore][CLEAR] ns=%s ok=%s\n",
         safe_label(ns),
         bool_label(ok));
}

} // namespace platform::ui::settings_store
