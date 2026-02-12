/**
 * @file ime_widget.h
 * @brief IME UI widget (toggle + buffer + candidates)
 */

#pragma once

#include "lvgl.h"
#include "pinyin_ime.h"

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
    void refresh_labels();
    void refresh_candidates();

    static void on_toggle_clicked(lv_event_t* e);

    PinyinIme ime_;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* toggle_btn_ = nullptr;
    lv_obj_t* toggle_label_ = nullptr;
    lv_obj_t* focus_proxy_ = nullptr;
    lv_obj_t* candidates_label_ = nullptr;
    lv_obj_t* textarea_ = nullptr;
    Mode mode_ = Mode::EN;
    std::string committed_text_;
};

} // namespace widgets
} // namespace ui
