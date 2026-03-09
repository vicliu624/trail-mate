#pragma once

#include <cstdint>

#include "ui/app_catalog.h"

namespace ui::startup_ui_shell
{

struct Hooks
{
    AppCatalog apps{};
    bool (*lock_ui)(uint32_t timeout_ms) = nullptr;
    void (*unlock_ui)() = nullptr;
    uint32_t lock_timeout_ms = 1000;
};

bool prepareBootUi(const Hooks& hooks, bool waking_from_sleep);
bool initializeMenuSkeleton(const Hooks& hooks);
bool finalizeStartup(const Hooks& hooks, bool waking_from_sleep);

} // namespace ui::startup_ui_shell
