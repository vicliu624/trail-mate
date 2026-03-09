#include "platform/ui/settings_store.h"

#include "esp_err.h"
#include "nvs.h"

namespace
{

bool open_namespace(const char* ns, bool read_only, nvs_handle_t* handle)
{
    if (!ns || !handle)
    {
        return false;
    }
    return nvs_open(ns, read_only ? NVS_READONLY : NVS_READWRITE, handle) == ESP_OK;
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
    nvs_handle_t handle = 0;
    if (!open_namespace(ns, false, &handle))
    {
        return;
    }
    nvs_set_i32(handle, key, static_cast<int32_t>(value));
    nvs_commit(handle);
    nvs_close(handle);
}

void put_bool(const char* ns, const char* key, bool value)
{
    if (!key)
    {
        return;
    }
    nvs_handle_t handle = 0;
    if (!open_namespace(ns, false, &handle))
    {
        return;
    }
    nvs_set_u8(handle, key, value ? 1 : 0);
    nvs_commit(handle);
    nvs_close(handle);
}

void put_uint(const char* ns, const char* key, uint32_t value)
{
    if (!key)
    {
        return;
    }
    nvs_handle_t handle = 0;
    if (!open_namespace(ns, false, &handle))
    {
        return;
    }
    nvs_set_u32(handle, key, value);
    nvs_commit(handle);
    nvs_close(handle);
}

bool put_blob(const char* ns, const char* key, const void* data, std::size_t len)
{
    if (!key || (!data && len != 0))
    {
        return false;
    }
    nvs_handle_t handle = 0;
    if (!open_namespace(ns, false, &handle))
    {
        return false;
    }
    const esp_err_t err = nvs_set_blob(handle, key, data, len);
    if (err == ESP_OK)
    {
        nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
}

int get_int(const char* ns, const char* key, int default_value)
{
    if (!key)
    {
        return default_value;
    }
    nvs_handle_t handle = 0;
    if (!open_namespace(ns, true, &handle))
    {
        return default_value;
    }
    int32_t value = static_cast<int32_t>(default_value);
    if (nvs_get_i32(handle, key, &value) != ESP_OK)
    {
        value = static_cast<int32_t>(default_value);
    }
    nvs_close(handle);
    return static_cast<int>(value);
}

bool get_bool(const char* ns, const char* key, bool default_value)
{
    if (!key)
    {
        return default_value;
    }
    nvs_handle_t handle = 0;
    if (!open_namespace(ns, true, &handle))
    {
        return default_value;
    }
    uint8_t value = default_value ? 1 : 0;
    if (nvs_get_u8(handle, key, &value) != ESP_OK)
    {
        value = default_value ? 1 : 0;
    }
    nvs_close(handle);
    return value != 0;
}

uint32_t get_uint(const char* ns, const char* key, uint32_t default_value)
{
    if (!key)
    {
        return default_value;
    }
    nvs_handle_t handle = 0;
    if (!open_namespace(ns, true, &handle))
    {
        return default_value;
    }
    uint32_t value = default_value;
    if (nvs_get_u32(handle, key, &value) != ESP_OK)
    {
        value = default_value;
    }
    nvs_close(handle);
    return value;
}

bool get_blob(const char* ns, const char* key, std::vector<uint8_t>& out)
{
    out.clear();
    if (!key)
    {
        return false;
    }
    nvs_handle_t handle = 0;
    if (!open_namespace(ns, true, &handle))
    {
        return false;
    }
    std::size_t len = 0;
    esp_err_t err = nvs_get_blob(handle, key, nullptr, &len);
    if (err != ESP_OK || len == 0)
    {
        nvs_close(handle);
        return false;
    }
    out.resize(len);
    err = nvs_get_blob(handle, key, out.data(), &len);
    if (err != ESP_OK)
    {
        out.clear();
        nvs_close(handle);
        return false;
    }
    if (len != out.size())
    {
        out.resize(len);
    }
    nvs_close(handle);
    return true;
}

void remove_keys(const char* ns, const char* const* keys, std::size_t key_count)
{
    if (!keys)
    {
        return;
    }
    nvs_handle_t handle = 0;
    if (!open_namespace(ns, false, &handle))
    {
        return;
    }
    bool changed = false;
    for (std::size_t i = 0; i < key_count; ++i)
    {
        if (keys[i] && nvs_erase_key(handle, keys[i]) == ESP_OK)
        {
            changed = true;
        }
    }
    if (changed)
    {
        nvs_commit(handle);
    }
    nvs_close(handle);
}

void clear_namespace(const char* ns)
{
    nvs_handle_t handle = 0;
    if (!open_namespace(ns, false, &handle))
    {
        return;
    }
    if (nvs_erase_all(handle) == ESP_OK)
    {
        nvs_commit(handle);
    }
    nvs_close(handle);
}

} // namespace platform::ui::settings_store
