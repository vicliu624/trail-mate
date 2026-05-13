#pragma once

#include <stdint.h>

namespace ui
{

enum class UiActionKind : uint8_t
{
    None = 0,
    Refresh,
    Select,
    Apply,
    Send,
    Back,
};

struct UiAction
{
    UiActionKind kind = UiActionKind::None;
    uint32_t target = 0;
    uint32_t value = 0;
};

} // namespace ui
