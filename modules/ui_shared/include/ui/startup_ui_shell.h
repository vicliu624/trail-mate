#pragma once

#include <cstdint>

#include "ui/app_catalog.h"

namespace ui::startup_ui_shell
{

struct Hooks
{
    AppCatalog apps{};
    const char* ux_pack_id = nullptr;
    bool (*lock_ui)(uint32_t timeout_ms) = nullptr;
    void (*unlock_ui)() = nullptr;
    uint32_t lock_timeout_ms = 1000;
    uint32_t status_refresh_interval_ms = 60000;
};

bool beginBootUi(const Hooks& hooks, bool waking_from_sleep, const char* initial_line);
void prepareBootResources();
bool initializeMenuSkeleton(const Hooks& hooks);
bool finalizeStartup(const Hooks& hooks, bool waking_from_sleep);

} // namespace ui::startup_ui_shell
