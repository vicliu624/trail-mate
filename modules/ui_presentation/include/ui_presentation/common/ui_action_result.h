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
        UiActionResult result;
        result.ok = true;
        result.failure = UiActionFailure::None;
        return result;
    }

    static UiActionResult fail(UiActionFailure failure)
    {
        UiActionResult result;
        result.ok = false;
        result.failure = failure;
        return result;
    }
};

} // namespace ui
