/**
 * @file page_host.h
 * @brief Generic shared page host hooks
 */

#pragma once

namespace ui::page
{

struct Host
{
    void* user_data = nullptr;
    void (*request_exit)(void* user_data) = nullptr;
};

inline void request_exit(const Host* host)
{
    if (host && host->request_exit)
    {
        host->request_exit(host->user_data);
    }
}

} // namespace ui::page
