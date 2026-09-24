#pragma once
#include "ui/screens/agenda/agenda_state.h"

namespace ui::agenda::page::editor
{
enum class Transition
{
    Unhandled,
    Render,
    KeepWidgets
};
Transition handle(Action action, uint8_t value);
bool captureText();
void render(lv_coord_t width, lv_coord_t height);
void showStatus();
void updateDiscard();
const char* title();
// Stable localization keys shared by editor fields and event detail.
const char* reminderText(const ::agenda::EventRecord& event);
const char* repeatText(::agenda::Repeat repeat);
} // namespace ui::agenda::page::editor
