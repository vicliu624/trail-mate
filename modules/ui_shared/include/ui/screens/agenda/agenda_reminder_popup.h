#pragma once

#include "ui_presentation/agenda/agenda_action_sink.h"
#include "ui_presentation/agenda/agenda_reminder_source.h"
#include "ui_presentation/agenda/agenda_source.h"

namespace ui::agenda::reminder_popup
{
struct Host
{
    IAgendaSource* source = nullptr;
    IAgendaActionSink* actions = nullptr;
    const IAgendaReminderSource* reminders = nullptr;
    void (*wake)() = nullptr;
    void* navigation_context = nullptr;
    // Prepare only reserves a bounded request; it must not switch UI trees.
    // nullptr = success, otherwise a localization key. Finish is called once
    // with acceptance, or cancellation on dismissal failure/interruption.
    const char* (*prepare_navigation)(void*, int32_t latitude_e7, int32_t longitude_e7) = nullptr;
    void (*finish_navigation)(void*, bool accepted) = nullptr;
};

// Called on the UI owner thread, outside LVGL input callbacks. No page,
// storage backend or platform clock is owned by this renderer.
bool visible();
bool canPresent();
void tick(const Host& host, bool may_present);
void close();
} // namespace ui::agenda::reminder_popup
