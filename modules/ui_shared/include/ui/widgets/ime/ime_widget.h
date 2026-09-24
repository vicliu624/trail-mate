/**
 * @file ime_widget.h
 * @brief IME UI widget (toggle + buffer + candidates)
 */

#pragma once

#include <array>
#include <string>

#include "lvgl.h"
#include "ui/widgets/ime/pinyin_ime.h"

#ifndef UI_SHARED_TOUCH_IME_ENABLED
#if defined(TRAIL_MATE_ESP_BOARD_TAB5) || defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
#define UI_SHARED_TOUCH_IME_ENABLED 1
#else
#define UI_SHARED_TOUCH_IME_ENABLED 0
#endif
#endif

namespace ui
{
namespace widgets
{

// Transient UI recovery metadata. Text storage belongs to the caller; this is
// not a persistent format and does not retain widgets or candidate collections.
struct ImeEditState
{
    uint32_t cursor = 0;
    int script_index = 0;
    int candidate_index = 0;
    int candidate_window = 0;
    char composition[9]{};
    uint8_t mode = 0;
    bool shift = false;
};
static_assert(sizeof(ImeEditState) <= 32, "IME recovery metadata must remain bounded");

class ImeWidget
{
  public:
    void init(lv_obj_t* parent, lv_obj_t* textarea, bool force_touch_keyboard = false);
    void detach();
    void activate();

    enum class Mode
    {
        EN,
        SCRIPT,
        CN = SCRIPT,
        NUM
    };

    void setMode(Mode mode);
    Mode mode() const;
    void cycleMode();

    bool handle_key(lv_event_t* e);
    void setText(const char* text);
    bool captureEditState(char* text, std::size_t capacity, ImeEditState& out) const;
    bool restoreEditState(const char* text, const ImeEditState& state);

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
    bool handle_text_token(const char* token);
    bool commit_candidate(int candidate_index);
    bool pinyin_mode() const;
    bool direct_keyboard_mode() const;

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
    bool editor_keyboard_ = false;
    bool touch_shift_ = false;
    int candidate_window_start_ = 0;
    int script_input_index_ = 0;
};

} // namespace widgets
} // namespace ui
