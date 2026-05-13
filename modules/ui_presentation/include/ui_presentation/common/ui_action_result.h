#pragma once

#include <stdint.h>

namespace ui
{

enum class UiActionFailure : uint8_t
{
    None = 0,
    InvalidInput,
    Rejected,
    NotReady,
    Unsupported,
    Busy,
    StorageError,
};

struct UiActionResult
{
    bool ok = false;
    UiActionFailure failure = UiActionFailure::None;

    static UiActionResult success()
    {
        return UiActionResult{true, UiActionFailure::None};
    }

    static UiActionResult fail(UiActionFailure failure)
    {
        return UiActionResult{false, failure};
    }
};

} // namespace ui
