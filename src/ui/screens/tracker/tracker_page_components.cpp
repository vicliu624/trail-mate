#include <Arduino.h>

#include "tracker_page_components.h"
#include "tracker_page_layout.h"
#include "tracker_state.h"
#include "tracker_page_input.h"
#include "../../ui_common.h"
#include "../../../gps/usecase/track_recorder.h"

#include <SD.h>

namespace tracker
{
namespace ui
{
namespace components
{

namespace
{
void on_back(void*)
{
    ui_request_exit_to_menu();
}

void refresh_track_list()
{
    auto& state = g_tracker_state;
    if (!state.list)
    {
        return;
    }
    lv_obj_clean(state.list);

    if (SD.cardType() == CARD_NONE)
    {
        lv_obj_t* label = lv_label_create(state.list);
        lv_label_set_text(label, "No SD Card");
        return;
    }

    constexpr size_t kMaxTracks = 32;
    String names[kMaxTracks];
    const size_t count = gps::TrackRecorder::getInstance().listTracks(names, kMaxTracks);

    if (count == 0)
    {
        lv_obj_t* label = lv_label_create(state.list);
        lv_label_set_text(label, "No tracks yet");
        return;
    }

    for (size_t i = 0; i < count; ++i)
    {
        lv_obj_t* btn = lv_list_add_btn(state.list, LV_SYMBOL_FILE, names[i].c_str());
        lv_obj_add_event_cb(btn, [](lv_event_t*) {}, LV_EVENT_CLICKED, nullptr);
    }
}

void update_start_stop_button()
{
    auto& state = g_tracker_state;
    const bool recording = gps::TrackRecorder::getInstance().isRecording();
    if (state.start_stop_label)
    {
        lv_label_set_text(state.start_stop_label, recording ? "Stop Recording" : "Start New Track");
    }
}

void on_start_stop_clicked(lv_event_t*)
{
    auto& recorder = gps::TrackRecorder::getInstance();
    if (recorder.isRecording())
    {
        recorder.stop();
    }
    else
    {
        recorder.start();
    }
    update_start_stop_button();
    refresh_track_list();
}
} // namespace

void refresh_page()
{
    update_start_stop_button();
    refresh_track_list();
    ui_update_top_bar_battery(g_tracker_state.top_bar);
}

void init_page(lv_obj_t* parent)
{
    auto& state = g_tracker_state;
    if (!parent)
    {
        return;
    }

    if (state.root)
    {
        cleanup_page();
    }

    state.root = layout::create_root(parent);
    state.header = layout::create_header(state.root);
    state.content = layout::create_content(state.root);
    state.start_stop_btn = layout::create_start_stop_button(state.content);
    state.start_stop_label = lv_label_create(state.start_stop_btn);
    lv_obj_center(state.start_stop_label);
    state.list = layout::create_list(state.content);

    ::ui::widgets::TopBarConfig cfg;
    cfg.height = ::ui::widgets::kTopBarHeight;
    ::ui::widgets::top_bar_init(state.top_bar, state.header, cfg);
    ::ui::widgets::top_bar_set_title(state.top_bar, "Tracker");
    ::ui::widgets::top_bar_set_back_callback(state.top_bar, on_back, nullptr);

    lv_obj_add_event_cb(state.start_stop_btn, on_start_stop_clicked, LV_EVENT_CLICKED, nullptr);

    input::bind_focus(app_g);
    refresh_page();
    state.initialized = true;
}

void cleanup_page()
{
    auto& state = g_tracker_state;
    if (state.root)
    {
        lv_obj_del(state.root);
    }
    state = TrackerPageState{};
}

} // namespace components
} // namespace ui
} // namespace tracker
