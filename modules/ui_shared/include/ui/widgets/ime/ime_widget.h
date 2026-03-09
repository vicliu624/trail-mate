/**
 * @file ime_widget.h
 * @brief IME UI widget (toggle + buffer + candidates)
 */

#pragma once

#include <array>
#include <string>

#include "lvgl.h"
#include "ui/widgets/ime/pinyin_ime.h"

namespace ui
{
namespace widgets
{

class ImeWidget
{
  public:
    void init(lv_obj_t* parent, lv_obj_t* textarea);
    void detach();

    enum class Mode
    {
        EN,
        CN,
        NUM
    };

    void setMode(Mode mode);
    Mode mode() const;
    void cycleMode();

    bool handle_key(lv_event_t* e);
    void setText(const char* text);

    lv_obj_t* container() const { return container_; }
    lv_obj_t* toggle_btn() const { return toggle_btn_; }
    lv_obj_t* focus_obj() const { return focus_proxy_; }

  private:
    void init_compact_ui(lv_obj_t* parent);
    void init_touch_ui(lv_obj_t* parent);
    void refresh_labels();
    void refresh_candidates();
    void refresh_touch_keyboard();
    void refresh_touch_candidates();
    void sync_textarea();
    bool handle_key_code(uint32_t key);
    bool commit_candidate(int candidate_index);

    static void on_toggle_clicked(lv_event_t* e);
    static void on_touch_key_event(lv_event_t* e);
    static void on_candidate_clicked(lv_event_t* e);
    static void on_candidate_nav_clicked(lv_event_t* e);

    PinyinIme ime_;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* top_row_ = nullptr;
    lv_obj_t* toggle_btn_ = nullptr;
    lv_obj_t* toggle_label_ = nullptr;
    lv_obj_t* focus_proxy_ = nullptr;
    lv_obj_t* candidates_label_ = nullptr;
    lv_obj_t* candidate_row_ = nullptr;
    lv_obj_t* candidate_prev_btn_ = nullptr;
    std::array<lv_obj_t*, 4> candidate_btns_{};
    lv_obj_t* candidate_next_btn_ = nullptr;
    lv_obj_t* keyboard_matrix_ = nullptr;
    lv_obj_t* textarea_ = nullptr;
    Mode mode_ = Mode::EN;
    std::string committed_text_;
    bool touch_keyboard_enabled_ = false;
    int candidate_window_start_ = 0;
};

} // namespace widgets
} // namespace ui
