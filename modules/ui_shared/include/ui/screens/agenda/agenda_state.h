#pragma once

#include "ui/screens/agenda/agenda_page_shell.h"
#include "ui/widgets/map/map_viewport.h"
#include "ui/widgets/top_bar.h"

namespace ui::agenda::page
{
enum class View : uint8_t
{
    Agenda,
    Detail,
    Editor,
    DatePicker,
    TimePicker,
    ReminderPicker,
    RepeatPicker,
    LocationPicker,
    WaypointPicker,
    WaypointName
};
enum class Action : uint8_t
{
    None,
    Back,
    Today,
    NextPage,
    OpenRow,
    Delete,
    ConfirmDelete,
    CancelDelete,
    NewEvent,
    EditEvent,
    Save,
    Discard,
    KeepEditing,
    PickDate,
    JumpToDate,
    PreviousMonth,
    NextMonth,
    SelectDay,
    PickTime,
    AcceptTime,
    PickReminder,
    SelectReminder,
    PickRepeat,
    SelectRepeat,
    PickLocation,
    CurrentLocation,
    ChooseOnMap,
    ClearLocation,
    Navigate,
    PickWaypoint,
    NextWaypoints,
    FirstWaypoints,
    SelectWaypoint,
    NameWaypoint,
    SaveWaypoint
};

struct DatePickerState
{
    ::agenda::CivilTime date{};
    MonthSnapshot month{};
    lv_calendar_date_t markers[31]{};
    const char* day_names[7]{};
    bool jump = false;
};
static_assert(sizeof(DatePickerState) < 256, "Month picker state exceeded budget");

struct EditorWidgets
{
    lv_obj_t* title = nullptr;
    lv_obj_t* note = nullptr;
    lv_obj_t* clock_fields[4]{};
    lv_obj_t* end_enabled = nullptr;
    lv_obj_t* status = nullptr;
    lv_obj_t* focus = nullptr;
    lv_obj_t* confirm_root = nullptr;
};

struct State
{
    const Host* host = nullptr;
    lv_obj_t* root = nullptr;
    lv_obj_t* body = nullptr;
    lv_group_t* group = nullptr;
    lv_group_t* previous_group = nullptr;
    lv_timer_t* timer = nullptr;
    ::ui::widgets::map::Runtime* detail_map = nullptr;
    lv_obj_t* map_notice = nullptr;
    ::ui::widgets::TopBar top_bar{};
    AgendaSnapshot snapshot{};
    AgendaRequest request{};
    AgendaEditorModel draft{};                // Only the selected record, never a table.
    ::agenda::Occurrence detail_occurrence{}; // Independent of rebuilt list rows.
    DatePickerState picker{};
    struct WaypointPage
    {
        uint32_t after = 0;
        uint32_t ids[5]{};
        uint8_t count = 0;
        bool has_more = false;
    } waypoint_page;
    EditorWidgets editor_widgets{};
    View view = View::Agenda;
    View editor_return = View::Agenda;
    Action pending = Action::None;
    uint8_t selected_row = 0;
    uint8_t action_value = 0;
    uint32_t pending_sequence = 0;
    bool awaiting_command = false;
    bool confirm_delete = false;
    bool confirm_discard = false;
    const char* error = nullptr; // Translation key, never a heap string.
};

State* state();
bool createState();
void destroyState();
} // namespace ui::agenda::page
