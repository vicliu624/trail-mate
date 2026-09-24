#pragma once

#include "agenda/domain/event.h"

namespace ui::agenda
{
enum class EditorField : uint8_t
{
    Title,
    Date,
    Time,
    Reminder,
    Location,
    Repeat,
    Note,
};

// Caller-owned draft survives reconstruction of the editor after map selection.
// No widget pointers, service ownership, or implicit writes while editing.
struct AgendaEditorModel
{
    ::agenda::EventRecord event;
    EditorField focus = EditorField::Title;
    bool dirty = false;
    bool editing_existing = false;
};
static_assert(sizeof(AgendaEditorModel) < 1024, "Editor draft budget exceeded");
} // namespace ui::agenda
