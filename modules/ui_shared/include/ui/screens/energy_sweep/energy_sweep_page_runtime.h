#pragma once

#include "ui/screens/energy_sweep/energy_sweep_page_shell.h"

#include <cstdint>

namespace energy_sweep::ui::runtime
{

bool is_available();
void enter(const shell::Host* host, lv_obj_t* parent);
void exit(lv_obj_t* parent);

// This is a radio-operation boundary for the T-Deck Pro text adapter. It is
// intentionally a snapshot/control API rather than a second visual page: the
// scan worker may run at radio cadence, but it never invalidates an LVGL
// object or asks the EPD to repaint.
struct TextSnapshot
{
    bool available = false;
    bool scanning = false;
    bool radio_error = false;
    bool applied = false;
    bool has_selection = false;
    std::uint32_t candidate_index = 0;
    std::uint32_t candidate_count = 0;
    std::uint32_t completed_passes = 0;
    std::uint32_t observation_count = 0;
    std::uint32_t evidence_count = 0;
    std::uint32_t crc_frame_count = 0;
    char status[48]{};
    char current_profile[40]{};
    char selected_profile[40]{};
};

bool text_start();
void text_stop();
bool text_snapshot(TextSnapshot& out);
bool text_select_observation_delta(int delta);
bool text_apply_selected();

} // namespace energy_sweep::ui::runtime
