#pragma once

#include "lvgl.h"

#include <string>

namespace ui::fs
{

inline std::string normalize_path(const char* path)
{
    if (path == nullptr || path[0] == '\0')
    {
        return {};
    }

    if (path[1] == ':')
    {
        return std::string(path);
    }

    if (path[0] == '/')
    {
        return std::string("A:") + path;
    }

    return std::string("A:/") + path;
}

inline bool file_exists(const char* path)
{
    const std::string normalized = normalize_path(path);
    if (normalized.empty())
    {
        return false;
    }

    lv_fs_file_t file;
    if (lv_fs_open(&file, normalized.c_str(), LV_FS_MODE_RD) != LV_FS_RES_OK)
    {
        return false;
    }
    lv_fs_close(&file);
    return true;
}

inline bool dir_exists(const char* path)
{
    const std::string normalized = normalize_path(path);
    if (normalized.empty())
    {
        return false;
    }

    lv_fs_dir_t dir;
    if (lv_fs_dir_open(&dir, normalized.c_str()) != LV_FS_RES_OK)
    {
        return false;
    }
    lv_fs_dir_close(&dir);
    return true;
}

inline bool read_text_file(const char* path, std::string& out)
{
    out.clear();

    const std::string normalized = normalize_path(path);
    if (normalized.empty())
    {
        return false;
    }

    lv_fs_file_t file;
    if (lv_fs_open(&file, normalized.c_str(), LV_FS_MODE_RD) != LV_FS_RES_OK)
    {
        return false;
    }

    char buffer[256];
    uint32_t bytes_read = 0;
    while (true)
    {
        if (lv_fs_read(&file, buffer, sizeof(buffer), &bytes_read) != LV_FS_RES_OK)
        {
            lv_fs_close(&file);
            out.clear();
            return false;
        }
        if (bytes_read == 0)
        {
            break;
        }
        out.append(buffer, bytes_read);
    }

    lv_fs_close(&file);
    return !out.empty();
}

} // namespace ui::fs