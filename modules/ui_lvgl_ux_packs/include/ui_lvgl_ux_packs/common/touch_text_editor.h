#pragma once

#include "lvgl.h"
#include <cstddef>

namespace ui::widgets
{
class ImeWidget;
struct ImeEditState;
enum class TouchEditorCapture
{
    Inactive,
    Captured,
    InsufficientCapacity,
};

// No-op unless the active page profile selects a compact touch-only keyboard.
// An existing IME owner receives committed text so its composition buffer
// stays synchronized. Plain text fields use the same editor with no owner.
void attach_touch_text_editor(lv_obj_t* textarea, ImeWidget* owner = nullptr);

// Capture without applying the modal draft to its source or retaining widgets.
// The caller must keep text/state alive only until restore returns.
TouchEditorCapture capture_touch_text_editor(lv_obj_t* source, char* text,
                                             std::size_t capacity, ImeEditState& state);
bool restore_touch_text_editor(lv_obj_t* source, const char* text,
                               const ImeEditState& state, ImeWidget* owner = nullptr);

// Streaming variants let bounded callers encode directly from the live draft
// and restore directly into its replacement without a full scratch string.
using TouchTextCapture = bool (*)(void*, const char*);
using TouchTextRestore = bool (*)(const void*, lv_obj_t*);
TouchEditorCapture capture_touch_text_editor(lv_obj_t* source, TouchTextCapture capture,
                                             void* context, ImeEditState& state);
bool restore_touch_text_editor(lv_obj_t* source, const ImeEditState& state,
                               TouchTextRestore restore, const void* context, ImeWidget* owner = nullptr);
} // namespace ui::widgets
