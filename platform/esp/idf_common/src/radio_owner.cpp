/**
 * @file radio_owner.cpp
 * @brief RadioSession implementation – logging-only in Phase 1.
 */

#include "platform/esp/idf_common/radio_owner.h"

#include <cstdio>

#include "esp_log.h"

namespace platform::esp::idf_common
{
namespace
{

constexpr const char* kTag = "radio-owner";

const char* owner_name(RadioOwner o)
{
    switch (o)
    {
    case RadioOwner::None:
        return "None";
    case RadioOwner::Mesh:
        return "Mesh";
    case RadioOwner::Walkie:
        return "Walkie";
    case RadioOwner::Sstv:
        return "SSTV";
    case RadioOwner::Diagnostics:
        return "Diag";
    case RadioOwner::Gps:
        return "GPS";
    case RadioOwner::Boot:
        return "Boot";
    }
    return "?";
}

} // namespace

RadioSession::RadioSession(RadioOwner owner,
                           const char* source_file,
                           int source_line)
    : owner_(owner), source_file_(source_file), source_line_(source_line)
{
    (void)enter();
}

RadioSession::~RadioSession()
{
    leave();
}

bool RadioSession::enter()
{
    if (active_)
    {
        return true;
    }

    ESP_LOGI(kTag, "[%s] enter  %s:%d",
             owner_name(owner_), source_file_, source_line_);
    active_ = true;
    return true;
}

void RadioSession::leave()
{
    if (!active_)
    {
        return;
    }

    ESP_LOGI(kTag, "[%s] leave  %s:%d",
             owner_name(owner_), source_file_, source_line_);
    active_ = false;
}

} // namespace platform::esp::idf_common
