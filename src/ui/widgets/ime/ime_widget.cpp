/**
 * @file ime_widget.cpp
 * @brief IME UI widget (toggle + buffer + candidates)
 */

#include "ime_widget.h"
#include "../../assets/fonts/fonts.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace ui
{
namespace widgets
{

static ImeWidget* s_active_ime = nullptr;
static constexpr int kCandidatesPerPage = 12;

extern "C" void ui_ime_toggle_mode()
{
    if (s_active_ime)
    {
        s_active_ime->cycleMode();
    }
}

extern "C" bool ui_ime_is_active()
{
    return s_active_ime != nullptr;
}

static std::string make_candidates_text(const std::vector<std::string>& candidates, int active_idx)
{
    std::string out;
    int max_show = kCandidatesPerPage;
    int total = static_cast<int>(candidates.size());
    if (total <= 0) return out;
    if (active_idx < 0) active_idx = 0;
    if (active_idx >= total) active_idx = total - 1;
    int page_start = (active_idx / max_show) * max_show;
    int page_end = page_start + max_show;
    if (page_end > total) page_end = total;
    for (int i = page_start; i < page_end; ++i)
    {
        if (i == active_idx)
        {
            out += "[";
            out += candidates[i];
            out += "]";
        }
        else
        {
            out += candidates[i];
        }
        if (i + 1 < page_end)
        {
            out += " ";
        }
    }
    return out;
}

void ImeWidget::init(lv_obj_t* parent, lv_obj_t* textarea)
{
    textarea_ = textarea;
    ime_.setEnabled(false);
    mode_ = Mode::EN;
    committed_text_.clear();
    s_active_ime = this;

    container_ = lv_obj_create(parent);
    lv_obj_set_width(container_, LV_PCT(100));
    lv_obj_set_height(container_, 24);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(container_, 6, 0);
    lv_obj_set_style_pad_right(container_, 6, 0);
    lv_obj_set_style_pad_column(container_, 6, 0);
    lv_obj_set_style_bg_color(container_, lv_color_hex(0xFFF0D3), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    toggle_btn_ = lv_btn_create(container_);
    lv_obj_set_size(toggle_btn_, 44, 18);
    lv_obj_set_style_radius(toggle_btn_, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(toggle_btn_, lv_color_hex(0xFFF7E9), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(toggle_btn_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(toggle_btn_, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(toggle_btn_, lv_color_hex(0xD9B06A), LV_PART_MAIN);
    lv_obj_add_event_cb(toggle_btn_, on_toggle_clicked, LV_EVENT_CLICKED, this);

    toggle_label_ = lv_label_create(toggle_btn_);
    lv_label_set_text(toggle_label_, "EN");
    lv_obj_set_style_text_font(toggle_label_, &lv_font_noto_cjk_16_2bpp, 0);
    lv_obj_set_style_text_color(toggle_label_, lv_color_hex(0x3A2A1A), 0);
    lv_obj_center(toggle_label_);

    focus_proxy_ = lv_btn_create(container_);
    lv_obj_set_size(focus_proxy_, 1, 1);
    lv_obj_add_flag(focus_proxy_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(focus_proxy_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(focus_proxy_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(focus_proxy_, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(
        focus_proxy_, [](lv_event_t* e)
        {
        ImeWidget* self = static_cast<ImeWidget*>(lv_event_get_user_data(e));
        if (!self) return;
        self->handle_key(e); },
        LV_EVENT_KEY, this);

    candidates_label_ = lv_label_create(container_);
    lv_label_set_text(candidates_label_, "");
    lv_obj_set_style_text_font(candidates_label_, &lv_font_noto_cjk_16_2bpp, 0);
    lv_obj_set_style_text_color(candidates_label_, lv_color_hex(0x3A2A1A), 0);
    lv_obj_set_flex_grow(candidates_label_, 1);
    lv_obj_set_style_text_align(candidates_label_, LV_TEXT_ALIGN_RIGHT, 0);

    refresh_labels();
}

void ImeWidget::detach()
{
    container_ = nullptr;
    toggle_btn_ = nullptr;
    toggle_label_ = nullptr;
    focus_proxy_ = nullptr;
    candidates_label_ = nullptr;
    textarea_ = nullptr;
    if (s_active_ime == this)
    {
        s_active_ime = nullptr;
    }
}

void ImeWidget::setMode(Mode mode)
{
    mode_ = mode;
    ime_.setEnabled(mode_ == Mode::CN);
    if (textarea_)
    {
        if (mode_ == Mode::CN)
        {
            const char* cur = lv_textarea_get_text(textarea_);
            committed_text_ = cur ? std::string(cur) : std::string();
            lv_textarea_set_accepted_chars(textarea_, "");
        }
        else
        {
            lv_textarea_set_accepted_chars(textarea_, nullptr);
        }
    }
    refresh_labels();
}

ImeWidget::Mode ImeWidget::mode() const
{
    return mode_;
}

void ImeWidget::cycleMode()
{
    if (mode_ == Mode::EN)
    {
        setMode(Mode::CN);
    }
    else if (mode_ == Mode::CN)
    {
        setMode(Mode::NUM);
    }
    else
    {
        setMode(Mode::EN);
    }
}

bool ImeWidget::handle_key(lv_event_t* e)
{
    if (mode_ != Mode::CN || !textarea_) return false;

    uint32_t key = lv_event_get_key(e);
    bool consumed = false;
    bool committed = false;
    std::string committed_text;

    if (key == LV_KEY_BACKSPACE)
    {
        if (ime_.hasBuffer())
        {
            ime_.backspace();
            consumed = true;
        }
    }
    else if (key == LV_KEY_UP || key == LV_KEY_LEFT)
    {
        if (ime_.hasBuffer())
        {
            ime_.moveCandidate(-1);
            consumed = true;
        }
    }
    else if (key == LV_KEY_DOWN || key == LV_KEY_RIGHT)
    {
        if (ime_.hasBuffer())
        {
            ime_.moveCandidate(1);
            consumed = true;
        }
    }
    else if (key == LV_KEY_ENTER)
    {
        if (ime_.hasBuffer())
        {
            std::string out;
            if (ime_.commitActive(out))
            {
                committed = true;
                committed_text = out;
            }
            consumed = true;
        }
    }
    else if (key == ' ')
    {
        if (ime_.hasBuffer())
        {
            ime_.reset();
        }
        committed = true;
        committed_text = " ";
        consumed = true;
    }
    else if (key >= 32 && key <= 126)
    {
        char c = static_cast<char>(key);
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
        if (c >= 'a' && c <= 'z')
        {
            if (ime_.appendLetter(c))
            {
                consumed = true;
            }
        }
    }

    if (consumed)
    {
        if (committed)
        {
            committed_text_ += committed_text;
            lv_textarea_set_text(textarea_, committed_text_.c_str());
            lv_textarea_set_cursor_pos(textarea_, LV_TEXTAREA_CURSOR_LAST);
        }
        else
        {
            lv_textarea_set_text(textarea_, committed_text_.c_str());
            lv_textarea_set_cursor_pos(textarea_, LV_TEXTAREA_CURSOR_LAST);
        }
        refresh_labels();
        lv_event_stop_processing(e);
    }
    return consumed;
}

void ImeWidget::refresh_labels()
{
    if (!toggle_label_ || !candidates_label_) return;

    if (textarea_)
    {
        if (mode_ == Mode::CN && ime_.hasBuffer())
        {
            if (lv_group_t* g = lv_group_get_default())
            {
                if (focus_proxy_)
                {
                    lv_group_focus_obj(focus_proxy_);
                }
                lv_group_set_editing(g, true);
            }
        }
        else
        {
            if (lv_group_t* g = lv_group_get_default())
            {
                lv_group_set_editing(g, false);
                lv_group_focus_obj(textarea_);
            }
        }
    }

    if (mode_ == Mode::EN)
    {
        lv_label_set_text(toggle_label_, "EN");
        lv_label_set_text(candidates_label_, "");
        return;
    }
    if (mode_ == Mode::NUM)
    {
        lv_label_set_text(toggle_label_, "123");
        lv_label_set_text(candidates_label_, "");
        return;
    }

    lv_label_set_text(toggle_label_, "CN");
    refresh_candidates();
}

void ImeWidget::refresh_candidates()
{
    if (!candidates_label_) return;
    if (!ime_.hasBuffer())
    {
        lv_label_set_text(candidates_label_, "");
        return;
    }
    std::string text = make_candidates_text(ime_.candidates(), ime_.candidateIndex());
    lv_label_set_text(candidates_label_, text.c_str());
}

void ImeWidget::on_toggle_clicked(lv_event_t* e)
{
    ImeWidget* self = static_cast<ImeWidget*>(lv_event_get_user_data(e));
    if (!self) return;

    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED)
    {
        self->cycleMode();
        return;
    }
    if (code == LV_EVENT_KEY)
    {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_ENTER)
        {
            return; // Enter is reserved for candidate commit
        }
        self->cycleMode();
        return;
    }
}

} // namespace widgets
} // namespace ui
