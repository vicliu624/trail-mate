/**
 * @file ime_widget.cpp
 * @brief IME UI widget (toggle + buffer + candidates)
 */

#include "ui/widgets/ime/ime_widget.h"

#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

namespace ui
{
namespace widgets
{
namespace
{

ImeWidget* s_active_ime = nullptr;
static constexpr int kCandidatesPerPage = 12;

bool script_input_available()
{
    return ::ui::i18n::active_locale_supports_script_input();
}

#if UI_SHARED_TOUCH_IME_ENABLED
static const char* kTouchEnMap[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "Bksp", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "Enter", "\n",
    "z", "x", "c", "v", "b", "n", "m", ",", ".", "?", "\n",
    "Space", ""};

static const char* kTouchNumMap[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "Bksp", "\n",
    "-", "/", ":", ";", "(", ")", "$", "&", "@", "Enter", "\n",
    ".", ",", "?", "!", "'", "\"", "%", "+", "\n",
    "Space", ""};
#endif
std::string make_candidates_text(const std::vector<std::string>& candidates, int active_idx)
{
    std::string out;
    int max_show = kCandidatesPerPage;
    int total = static_cast<int>(candidates.size());
    if (total <= 0) return out;
    if (active_idx < 0) active_idx = 0;
    if (active_idx >= total) active_idx = total - 1;

    int start = 0;
    if (total > max_show)
    {
        start = active_idx - (max_show / 2);
        if (start < 0) start = 0;
        if (start + max_show > total) start = total - max_show;
    }

    for (int i = start; i < total && i < start + max_show; ++i)
    {
        if (!out.empty()) out += "  ";
        if (i == active_idx)
        {
            out += '[';
            out += candidates[i];
            out += ']';
        }
        else
        {
            out += candidates[i];
        }
    }
    return out;
}

void erase_last_utf8_char(std::string& text)
{
    if (text.empty())
    {
        return;
    }
    size_t pos = text.size() - 1;
    while (pos > 0)
    {
        uint8_t c = static_cast<uint8_t>(text[pos]);
        if ((c & 0xC0) != 0x80)
        {
            break;
        }
        --pos;
    }
    text.erase(pos);
}

#if UI_SHARED_TOUCH_IME_ENABLED
void set_button_label(lv_obj_t* button, const char* text)
{
    if (!button)
    {
        return;
    }
    lv_obj_t* label = lv_obj_get_child(button, 0);
    if (!label)
    {
        label = lv_label_create(button);
    }
    lv_obj_set_style_text_font(label, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x3A2A1A), 0);
    lv_label_set_text(label, text ? text : "");
    lv_obj_center(label);
}

void apply_candidate_button_style(lv_obj_t* button, bool active)
{
    if (!button)
    {
        return;
    }
    lv_obj_set_style_bg_color(button, active ? lv_color_hex(0xEBA341) : lv_color_hex(0xFFF7E9), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, active ? lv_color_hex(0xC98118) : lv_color_hex(0xD9B06A), LV_PART_MAIN);
    lv_obj_set_style_radius(button, 8, LV_PART_MAIN);
    lv_obj_set_style_outline_width(button, 0, static_cast<lv_style_selector_t>(static_cast<unsigned>(LV_PART_MAIN) | static_cast<unsigned>(LV_STATE_FOCUSED)));
}

bool translate_touch_token(const char* token, uint32_t& key)
{
    if (!token || !token[0])
    {
        return false;
    }
    if (std::strcmp(token, "Bksp") == 0)
    {
        key = LV_KEY_BACKSPACE;
        return true;
    }
    if (std::strcmp(token, "Enter") == 0)
    {
        key = LV_KEY_ENTER;
        return true;
    }
    if (std::strcmp(token, "Space") == 0)
    {
        key = ' ';
        return true;
    }
    if (token[1] == '\0')
    {
        key = static_cast<uint8_t>(token[0]);
        return true;
    }
    return false;
}

bool resolve_touch_button_id(lv_event_t* e, lv_obj_t* matrix, uint32_t& button_id)
{
    if (!matrix)
    {
        return false;
    }

    const auto* event_button_id = static_cast<const uint32_t*>(lv_event_get_param(e));
    if (event_button_id)
    {
        button_id = *event_button_id;
    }
    else
    {
        button_id = lv_btnmatrix_get_selected_btn(matrix);
    }

    return button_id != LV_BUTTONMATRIX_BUTTON_NONE;
}
#endif

} // namespace

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

void ImeWidget::init(lv_obj_t* parent, lv_obj_t* textarea)
{
    textarea_ = textarea;
    ime_.setEnabled(false);
    mode_ = Mode::EN;
    committed_text_.clear();
    if (textarea_)
    {
        const char* cur = lv_textarea_get_text(textarea_);
        committed_text_ = cur ? std::string(cur) : std::string();
    }
    s_active_ime = this;

    const auto& profile = ::ui::page_profile::current();
    candidate_window_start_ = 0;

#if UI_SHARED_TOUCH_IME_ENABLED
    touch_keyboard_enabled_ = profile.large_touch_hitbox && profile.ime_keyboard_height > 0;
    if (touch_keyboard_enabled_)
    {
        init_touch_ui(parent);
    }
    else
#endif
    {
        touch_keyboard_enabled_ = false;
        init_compact_ui(parent);
    }

    refresh_labels();
}

void ImeWidget::init_compact_ui(lv_obj_t* parent)
{
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
    lv_obj_set_style_text_font(toggle_label_, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()), 0);
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
    lv_obj_set_style_text_font(candidates_label_, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()), 0);
    lv_obj_set_style_text_color(candidates_label_, lv_color_hex(0x3A2A1A), 0);
    lv_obj_set_flex_grow(candidates_label_, 1);
    lv_obj_set_style_text_align(candidates_label_, LV_TEXT_ALIGN_RIGHT, 0);
}

#if UI_SHARED_TOUCH_IME_ENABLED
void ImeWidget::init_touch_ui(lv_obj_t* parent)
{
    const auto& profile = ::ui::page_profile::current();
    const lv_coord_t nav_button_width = std::max<lv_coord_t>(48, ::ui::page_profile::resolve_compact_button_min_width());

    container_ = lv_obj_create(parent);
    lv_obj_set_size(container_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 8, 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    top_row_ = lv_obj_create(container_);
    lv_obj_set_size(top_row_, LV_PCT(100), profile.ime_bar_height);
    lv_obj_set_flex_flow(top_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_row_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(top_row_, 8, 0);
    lv_obj_set_style_pad_right(top_row_, 8, 0);
    lv_obj_set_style_pad_column(top_row_, 10, 0);
    lv_obj_set_style_bg_color(top_row_, lv_color_hex(0xFFF0D3), 0);
    lv_obj_set_style_bg_opa(top_row_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_row_, 1, 0);
    lv_obj_set_style_border_color(top_row_, lv_color_hex(0xE7C98F), 0);
    lv_obj_set_style_radius(top_row_, 10, 0);
    lv_obj_clear_flag(top_row_, LV_OBJ_FLAG_SCROLLABLE);

    toggle_btn_ = lv_btn_create(top_row_);
    lv_obj_set_size(toggle_btn_, profile.ime_toggle_width, std::max(profile.ime_toggle_height, profile.ime_bar_height - 8));
    lv_obj_set_style_radius(toggle_btn_, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(toggle_btn_, lv_color_hex(0xFFF7E9), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(toggle_btn_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(toggle_btn_, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(toggle_btn_, lv_color_hex(0xD9B06A), LV_PART_MAIN);
    lv_obj_add_event_cb(toggle_btn_, on_toggle_clicked, LV_EVENT_CLICKED, this);

    toggle_label_ = lv_label_create(toggle_btn_);
    lv_label_set_text(toggle_label_, "EN");
    lv_obj_set_style_text_font(toggle_label_, ::ui::fonts::ui_chrome_font(), 0);
    lv_obj_set_style_text_color(toggle_label_, lv_color_hex(0x3A2A1A), 0);
    lv_obj_center(toggle_label_);

    focus_proxy_ = lv_btn_create(top_row_);
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

    candidates_label_ = lv_label_create(top_row_);
    ::ui::i18n::set_label_text(candidates_label_, "English keyboard");
    lv_obj_set_flex_grow(candidates_label_, 1);
    lv_obj_set_style_text_align(candidates_label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_font(candidates_label_, ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()), 0);
    lv_obj_set_style_text_color(candidates_label_, lv_color_hex(0x6B4A1E), 0);

    candidate_row_ = lv_obj_create(container_);
    lv_obj_set_size(candidate_row_, LV_PCT(100), profile.ime_candidate_button_height);
    lv_obj_set_flex_flow(candidate_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(candidate_row_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(candidate_row_, 0, 0);
    lv_obj_set_style_pad_column(candidate_row_, 8, 0);
    lv_obj_set_style_bg_opa(candidate_row_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(candidate_row_, 0, 0);
    lv_obj_clear_flag(candidate_row_, LV_OBJ_FLAG_SCROLLABLE);

    candidate_prev_btn_ = lv_btn_create(candidate_row_);
    lv_obj_set_size(candidate_prev_btn_, nav_button_width, profile.ime_candidate_button_height);
    set_button_label(candidate_prev_btn_, "<");
    apply_candidate_button_style(candidate_prev_btn_, false);
    lv_obj_add_event_cb(candidate_prev_btn_, on_candidate_nav_clicked, LV_EVENT_CLICKED, this);

    for (size_t i = 0; i < candidate_btns_.size(); ++i)
    {
        candidate_btns_[i] = lv_btn_create(candidate_row_);
        lv_obj_set_height(candidate_btns_[i], profile.ime_candidate_button_height);
        lv_obj_set_flex_grow(candidate_btns_[i], 1);
        set_button_label(candidate_btns_[i], "");
        apply_candidate_button_style(candidate_btns_[i], false);
        lv_obj_add_event_cb(candidate_btns_[i], on_candidate_clicked, LV_EVENT_CLICKED, this);
        lv_obj_set_style_text_font(
            candidate_btns_[i], ::ui::fonts::localized_font(::ui::fonts::ui_chrome_font()), LV_PART_MAIN);
    }

    candidate_next_btn_ = lv_btn_create(candidate_row_);
    lv_obj_set_size(candidate_next_btn_, nav_button_width, profile.ime_candidate_button_height);
    set_button_label(candidate_next_btn_, ">");
    apply_candidate_button_style(candidate_next_btn_, false);
    lv_obj_add_event_cb(candidate_next_btn_, on_candidate_nav_clicked, LV_EVENT_CLICKED, this);

    keyboard_matrix_ = lv_btnmatrix_create(container_);
    lv_obj_set_size(keyboard_matrix_, LV_PCT(100), profile.ime_keyboard_height);
    lv_obj_set_style_radius(keyboard_matrix_, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(keyboard_matrix_, lv_color_hex(0xFFF7E9), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(keyboard_matrix_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(keyboard_matrix_, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(keyboard_matrix_, lv_color_hex(0xD9B06A), LV_PART_MAIN);
    lv_obj_set_style_pad_all(keyboard_matrix_, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(keyboard_matrix_, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_column(keyboard_matrix_, 6, LV_PART_ITEMS);
    lv_obj_set_style_text_font(keyboard_matrix_, ::ui::fonts::ui_chrome_font(), LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard_matrix_, lv_color_hex(0x3A2A1A), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard_matrix_, lv_color_hex(0xFFF7E9), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(keyboard_matrix_, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(keyboard_matrix_, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(keyboard_matrix_, lv_color_hex(0xD9B06A), LV_PART_ITEMS);
    lv_obj_set_style_radius(keyboard_matrix_, 8, LV_PART_ITEMS);
    lv_obj_clear_flag(keyboard_matrix_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(keyboard_matrix_, on_touch_key_event, LV_EVENT_VALUE_CHANGED, this);

    refresh_touch_keyboard();
    refresh_touch_candidates();
}
#endif

void ImeWidget::detach()
{
    container_ = nullptr;
    top_row_ = nullptr;
    toggle_btn_ = nullptr;
    toggle_label_ = nullptr;
    focus_proxy_ = nullptr;
    candidates_label_ = nullptr;
    candidate_row_ = nullptr;
    candidate_prev_btn_ = nullptr;
    candidate_btns_.fill(nullptr);
    candidate_next_btn_ = nullptr;
    keyboard_matrix_ = nullptr;
    textarea_ = nullptr;
    touch_keyboard_enabled_ = false;
    candidate_window_start_ = 0;
    if (s_active_ime == this)
    {
        s_active_ime = nullptr;
    }
}

void ImeWidget::setMode(Mode mode)
{
    if (mode == Mode::CN && !script_input_available())
    {
        mode = Mode::EN;
    }

    mode_ = mode;
    ime_.setEnabled(mode_ == Mode::CN);
    if (textarea_)
    {
        const char* cur = lv_textarea_get_text(textarea_);
        committed_text_ = cur ? std::string(cur) : std::string();
        if (mode_ == Mode::CN)
        {
            ime_.reset();
            lv_textarea_set_accepted_chars(textarea_, "");
        }
        else
        {
            lv_textarea_set_accepted_chars(textarea_, nullptr);
        }
    }
    candidate_window_start_ = 0;
    refresh_labels();
}

ImeWidget::Mode ImeWidget::mode() const
{
    return mode_;
}

void ImeWidget::cycleMode()
{
    if (!script_input_available())
    {
        setMode(mode_ == Mode::EN ? Mode::NUM : Mode::EN);
        return;
    }

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

bool ImeWidget::commit_candidate(int candidate_index)
{
    std::string out;
    if (!ime_.commitCandidate(candidate_index, out))
    {
        return false;
    }
    committed_text_ += out;
    candidate_window_start_ = 0;
    sync_textarea();
    refresh_labels();
    return true;
}

bool ImeWidget::handle_key_code(uint32_t key)
{
    if (!textarea_)
    {
        return false;
    }

    bool consumed = false;
    bool update_text = false;

    if (mode_ == Mode::CN)
    {
        if (key == LV_KEY_BACKSPACE)
        {
            if (ime_.hasBuffer())
            {
                ime_.backspace();
                consumed = true;
            }
            else
            {
                erase_last_utf8_char(committed_text_);
                update_text = true;
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
                    committed_text_ += out;
                    update_text = true;
                }
                consumed = true;
            }
            else
            {
                committed_text_ += "\n";
                update_text = true;
                consumed = true;
            }
        }
        else if (key == ' ')
        {
            if (ime_.hasBuffer())
            {
                std::string out;
                if (ime_.commitActive(out))
                {
                    committed_text_ += out;
                    update_text = true;
                }
                consumed = true;
            }
            else
            {
                committed_text_ += " ";
                update_text = true;
                consumed = true;
            }
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
                ime_.appendLetter(c);
                consumed = true;
            }
            else
            {
                if (ime_.hasBuffer())
                {
                    std::string out;
                    if (ime_.commitActive(out))
                    {
                        committed_text_ += out;
                    }
                }
                committed_text_.push_back(c);
                update_text = true;
                consumed = true;
            }
        }
    }
    else
    {
        if (key == LV_KEY_BACKSPACE)
        {
            erase_last_utf8_char(committed_text_);
            update_text = true;
            consumed = true;
        }
        else if (key == LV_KEY_ENTER)
        {
            committed_text_ += "\n";
            update_text = true;
            consumed = true;
        }
        else if (key == ' ')
        {
            committed_text_ += " ";
            update_text = true;
            consumed = true;
        }
        else if (key >= 32 && key <= 126)
        {
            committed_text_.push_back(static_cast<char>(key));
            update_text = true;
            consumed = true;
        }
    }

    if (consumed && update_text)
    {
        sync_textarea();
    }
    if (consumed)
    {
        refresh_labels();
    }
    return consumed;
}

bool ImeWidget::handle_key(lv_event_t* e)
{
    if (!textarea_ || !e)
    {
        return false;
    }

    const uint32_t key = lv_event_get_key(e);
    const bool consumed = handle_key_code(key);
    if (consumed)
    {
        lv_event_stop_processing(e);
    }
    return consumed;
}

void ImeWidget::sync_textarea()
{
    if (!textarea_)
    {
        return;
    }
    lv_textarea_set_text(textarea_, committed_text_.c_str());
    lv_textarea_set_cursor_pos(textarea_, LV_TEXTAREA_CURSOR_LAST);
    lv_obj_add_state(textarea_, LV_STATE_FOCUSED);
    if (lv_group_get_default())
    {
        lv_group_focus_obj(textarea_);
    }
    lv_obj_send_event(textarea_, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_invalidate(textarea_);
}

void ImeWidget::setText(const char* text)
{
    if (!textarea_) return;
    committed_text_ = text ? std::string(text) : std::string();
    sync_textarea();
    if (mode_ == Mode::CN)
    {
        ime_.reset();
    }
    refresh_labels();
}

#if UI_SHARED_TOUCH_IME_ENABLED
void ImeWidget::refresh_touch_keyboard()
{
    if (!touch_keyboard_enabled_ || !keyboard_matrix_)
    {
        return;
    }
    const char* const* map = (mode_ == Mode::NUM) ? kTouchNumMap : kTouchEnMap;
    lv_btnmatrix_set_map(keyboard_matrix_, map);
}

void ImeWidget::refresh_touch_candidates()
{
    if (!touch_keyboard_enabled_ || !candidate_row_)
    {
        return;
    }

    const bool show_candidates = mode_ == Mode::CN;
    if (!show_candidates)
    {
        lv_obj_add_flag(candidate_row_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(candidate_row_, LV_OBJ_FLAG_HIDDEN);

    const auto& candidates = ime_.candidates();
    const int total = static_cast<int>(candidates.size());
    const int active = ime_.candidateIndex();
    const int visible_count = static_cast<int>(candidate_btns_.size());

    if (total <= 0)
    {
        candidate_window_start_ = 0;
        for (auto* btn : candidate_btns_)
        {
            if (btn)
            {
                set_button_label(btn, "");
                lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
                apply_candidate_button_style(btn, false);
            }
        }
        if (candidate_prev_btn_)
        {
            lv_obj_add_state(candidate_prev_btn_, LV_STATE_DISABLED);
        }
        if (candidate_next_btn_)
        {
            lv_obj_add_state(candidate_next_btn_, LV_STATE_DISABLED);
        }
        return;
    }

    int start = active - 1;
    if (start < 0) start = 0;
    if (start + visible_count > total)
    {
        start = std::max(0, total - visible_count);
    }
    candidate_window_start_ = start;

    if (candidate_prev_btn_)
    {
        if (start > 0) lv_obj_clear_state(candidate_prev_btn_, LV_STATE_DISABLED);
        else lv_obj_add_state(candidate_prev_btn_, LV_STATE_DISABLED);
    }
    if (candidate_next_btn_)
    {
        if (start + visible_count < total) lv_obj_clear_state(candidate_next_btn_, LV_STATE_DISABLED);
        else lv_obj_add_state(candidate_next_btn_, LV_STATE_DISABLED);
    }

    for (int slot = 0; slot < visible_count; ++slot)
    {
        lv_obj_t* btn = candidate_btns_[slot];
        if (!btn)
        {
            continue;
        }
        const int index = start + slot;
        if (index < total)
        {
            set_button_label(btn, candidates[index].c_str());
            lv_obj_clear_flag(btn, LV_OBJ_FLAG_HIDDEN);
            apply_candidate_button_style(btn, index == active);
        }
        else
        {
            set_button_label(btn, "");
            lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
            apply_candidate_button_style(btn, false);
        }
    }
}
#endif

void ImeWidget::refresh_labels()
{
    if (!toggle_label_ || !candidates_label_)
    {
        return;
    }

    if (mode_ == Mode::CN && !script_input_available())
    {
        mode_ = Mode::EN;
        ime_.setEnabled(false);
    }

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
            }
        }
    }

    if (mode_ == Mode::EN)
    {
        lv_label_set_text(toggle_label_, "EN");
#if UI_SHARED_TOUCH_IME_ENABLED
        if (touch_keyboard_enabled_)
        {
            ::ui::i18n::set_label_text(candidates_label_, "English keyboard");
            refresh_touch_keyboard();
            refresh_touch_candidates();
            return;
        }
#endif
        lv_label_set_text(candidates_label_, "");
        return;
    }

    if (mode_ == Mode::NUM)
    {
        lv_label_set_text(toggle_label_, "123");
#if UI_SHARED_TOUCH_IME_ENABLED
        if (touch_keyboard_enabled_)
        {
            ::ui::i18n::set_label_text(candidates_label_, "Number keyboard");
            refresh_touch_keyboard();
            refresh_touch_candidates();
            return;
        }
#endif
        lv_label_set_text(candidates_label_, "");
        return;
    }

    lv_label_set_text(toggle_label_, "CN");
#if UI_SHARED_TOUCH_IME_ENABLED
    if (touch_keyboard_enabled_)
    {
        std::string hint = ime_.hasBuffer()
                               ? ::ui::i18n::format("Pinyin: %s", ime_.buffer().c_str())
                               : std::string(::ui::i18n::tr("Pinyin keyboard"));
        lv_label_set_text(candidates_label_, hint.c_str());
        refresh_touch_keyboard();
        refresh_touch_candidates();
        return;
    }
#endif

    refresh_candidates();
}

void ImeWidget::refresh_candidates()
{
    if (!candidates_label_)
    {
        return;
    }
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
            return;
        }
        self->cycleMode();
    }
}

#if UI_SHARED_TOUCH_IME_ENABLED
void ImeWidget::on_touch_key_event(lv_event_t* e)
{
    ImeWidget* self = static_cast<ImeWidget*>(lv_event_get_user_data(e));
    if (!self || !self->keyboard_matrix_)
    {
        return;
    }

    uint32_t button_id = LV_BUTTONMATRIX_BUTTON_NONE;
    if (!resolve_touch_button_id(e, self->keyboard_matrix_, button_id))
    {
        return;
    }

    const char* token = lv_btnmatrix_get_btn_text(self->keyboard_matrix_, button_id);
    uint32_t key = 0;
    if (!translate_touch_token(token, key))
    {
        return;
    }
    self->handle_key_code(key);
}

void ImeWidget::on_candidate_clicked(lv_event_t* e)
{
    ImeWidget* self = static_cast<ImeWidget*>(lv_event_get_user_data(e));
    if (!self)
    {
        return;
    }
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    for (size_t i = 0; i < self->candidate_btns_.size(); ++i)
    {
        if (self->candidate_btns_[i] == target)
        {
            const int candidate_index = self->candidate_window_start_ + static_cast<int>(i);
            self->commit_candidate(candidate_index);
            return;
        }
    }
}

void ImeWidget::on_candidate_nav_clicked(lv_event_t* e)
{
    ImeWidget* self = static_cast<ImeWidget*>(lv_event_get_user_data(e));
    if (!self)
    {
        return;
    }
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (target == self->candidate_prev_btn_)
    {
        self->handle_key_code(LV_KEY_LEFT);
    }
    else if (target == self->candidate_next_btn_)
    {
        self->handle_key_code(LV_KEY_RIGHT);
    }
}
#endif

} // namespace widgets
} // namespace ui
