#pragma once

#include "platform/ui/setting_sensitivity.h"
#include "platform/ui/settings_store.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "esp_err.h"
#include "nvs.h"

namespace platform::esp::common::settings_store_detail
{

constexpr std::size_t kMaxNvsKeyLength = 15;

inline ::platform::ui::settings_store::ChangeObserver s_change_observer = nullptr;
inline void* s_change_observer_context = nullptr;
inline uint16_t s_change_batch_depth = 0U;
inline bool s_change_pending = false;

inline void notify_change(const char* ns, const char* key)
{
    if (s_change_batch_depth != 0U)
    {
        s_change_pending = true;
        return;
    }
    if (s_change_observer)
    {
        s_change_observer(s_change_observer_context, ns, key);
    }
}

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
    {"chat_message_alerts", "chat_msg_alert"},
    {"chat_contact_alerts", "chat_ct_alert"},
    {"chat_auto_reply_enabled", "chat_auto_reply"},
    {"chat_auto_reply_text", "chat_auto_txt"},
    {"timezone_profile", "timezone_prof"},
    {"gauge_design_mah", "gauge_dsgn"},
    {"wifi_profile_count", "wifi_prof_count"},
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

void set_change_observer(ChangeObserver observer, void* context)
{
    using namespace ::platform::esp::common::settings_store_detail;
    s_change_observer = observer;
    s_change_observer_context = context;
}

void begin_change_batch()
{
    using namespace ::platform::esp::common::settings_store_detail;
    if (s_change_batch_depth != UINT16_MAX)
    {
        ++s_change_batch_depth;
    }
}

void end_change_batch()
{
    using namespace ::platform::esp::common::settings_store_detail;
    if (s_change_batch_depth == 0U)
    {
        return;
    }
    --s_change_batch_depth;
    if (s_change_batch_depth == 0U && s_change_pending)
    {
        s_change_pending = false;
        notify_change(nullptr, nullptr);
    }
}

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
    if (ok)
    {
        notify_change(ns, key);
    }
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
    if (ok)
    {
        notify_change(ns, key);
    }
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
    if (ok)
    {
        notify_change(ns, key);
    }
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
        if (ok)
        {
            notify_change(ns, key);
        }
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
         ::platform::ui::settings::diagnostic_value(key, value),
         bool_label(ok));
    if (ok)
    {
        notify_change(ns, key);
    }
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
        if (ok)
        {
            notify_change(ns, key);
        }
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
    if (ok)
    {
        notify_change(ns, key);
    }
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

#if defined(TRAIL_MATE_VERBOSE_RUNTIME_LOGS) && TRAIL_MATE_VERBOSE_RUNTIME_LOGS
    logf("[CfgStore][READ] ns=%s key=%s storage_key=%s type=int source=%s value=%d default=%d\n",
         safe_label(ns),
         safe_label(key),
         safe_label(storage_key),
         exists ? "stored" : "default",
         static_cast<int>(value),
         default_value);
#endif
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
         ::platform::ui::settings::diagnostic_value(key, out.c_str()));
    return true;
}

bool get_string_into(const char* ns,
                     const char* key,
                     char* out,
                     std::size_t capacity,
                     std::size_t* out_len)
{
    using namespace ::platform::esp::common::settings_store_detail;

    if (out_len)
    {
        *out_len = 0U;
    }
    if (!key || !out || capacity == 0U)
    {
        return false;
    }
    out[0] = '\0';
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
    std::size_t stored_size = 0U;
    if (nvs_get_str(handle, storage_key, nullptr, &stored_size) != ESP_OK || stored_size == 0U ||
        stored_size > capacity)
    {
        nvs_close(handle);
        return false;
    }
    std::size_t read_size = stored_size;
    const bool ok = nvs_get_str(handle, storage_key, out, &read_size) == ESP_OK &&
                    read_size == stored_size && out[stored_size - 1U] == '\0';
    nvs_close(handle);
    if (!ok)
    {
        out[0] = '\0';
        return false;
    }
    if (out_len)
    {
        *out_len = stored_size - 1U;
    }
    logf("[CfgStore][READ] ns=%s key=%s storage_key=%s type=string source=stored len=%lu ok=true\n",
         safe_label(ns),
         safe_label(key),
         safe_label(storage_key),
         static_cast<unsigned long>(stored_size - 1U));
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

bool get_blob_into(const char* ns,
                   const char* key,
                   void* out,
                   std::size_t capacity,
                   std::size_t* out_len)
{
    using namespace ::platform::esp::common::settings_store_detail;

    if (out_len)
    {
        *out_len = 0;
    }
    if (!key || (!out && capacity != 0))
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

    if (out_len)
    {
        *out_len = len;
    }
    if (len > capacity)
    {
        nvs_close(handle);
        return false;
    }

    std::size_t read = len;
    const bool ok = nvs_get_blob(handle, storage_key, out, &read) == ESP_OK && read == len;
    nvs_close(handle);

    logf("[CfgStore][READ] ns=%s key=%s storage_key=%s type=blob source=stored len=%lu ok=%s\n",
         safe_label(ns),
         safe_label(key),
         safe_label(storage_key),
         static_cast<unsigned long>(len),
         bool_label(ok));
    return ok;
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
        else
        {
            notify_change(ns, nullptr);
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
    if (ok)
    {
        notify_change(ns, nullptr);
    }
}

} // namespace platform::ui::settings_store
