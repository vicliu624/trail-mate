#pragma once
#include "app/text_input.h"
#include "lvgl.h"
#include <algorithm>
#include <cstring>

namespace trailmate::cardputer_zero::linux_ui
{
// UI-thread adapter. Composition is a visual overlay, never saved draft text.
class LvglTextInput
{
  public:
    ~LvglTextInput() { reset(); }
    void reset()
    {
        if (field_) lv_obj_remove_event_cb_with_user_data(field_, deleted, this);
        if (preedit_) lv_obj_delete(preedit_);
        field_ = preedit_ = nullptr;
        ++focus_;
    }
    app::TextInputState state()
    {
        auto* group = lv_group_get_default();
        auto* field = group ? lv_group_get_focused(group) : nullptr;
        if (!field || !lv_obj_check_type(field, &lv_textarea_class) || lv_obj_has_state(field, LV_STATE_DISABLED)) field = nullptr;
        if (field != field_)
        {
            reset();
            field_ = field;
            if (field_) lv_obj_add_event_cb(field_, deleted, LV_EVENT_DELETE, this);
        }
        app::TextInputState out;
        out.focus = focus_;
        if (!field_) return out;
        out.enabled = true;
        out.password = lv_textarea_get_password_mode(field_);
        out.multiline = !lv_textarea_get_one_line(field_);
        const std::string text = lv_textarea_get_text(field_);
        const auto cursor = app::utf8ByteAt(text, lv_textarea_get_cursor_pos(field_));
        size_t start = cursor > 2000 ? cursor - 2000 : 0;
        while (!app::utf8Boundary(text, start)) ++start;
        size_t end = std::min(text.size(), start + 4000);
        while (!app::utf8Boundary(text, end)) --end;
        if (!out.password)
        {
            out.surrounding = text.substr(start, end - start);
            out.cursor = static_cast<int32_t>(cursor - start);
        }
        lv_area_t area;
        auto* label = lv_textarea_get_label(field_);
        lv_obj_get_coords(label, &area);
        lv_point_t caret{};
        lv_label_get_letter_pos(label, lv_textarea_get_cursor_pos(field_), &caret);
        out.x = area.x1 + caret.x;
        out.y = area.y1 + caret.y;
        out.width = 1;
        out.height = lv_font_get_line_height(lv_obj_get_style_text_font(label, LV_PART_MAIN));
        return out;
    }
    bool apply(const app::TextEdit& edit)
    {
        const auto current = state();
        if (!current.enabled || edit.focus != current.focus || edit.commit.size() > 4000 || edit.preedit.size() > 4000 ||
            !app::validUtf8(edit.commit) || !app::validUtf8(edit.preedit)) return false;
        const bool hidden_cursor = edit.preedit_begin == -1 && edit.preedit_end == -1;
        if (!hidden_cursor && (edit.preedit_begin < 0 || edit.preedit_end < 0 ||
                               !app::utf8Boundary(edit.preedit, edit.preedit_begin) || !app::utf8Boundary(edit.preedit, edit.preedit_end))) return false;
        const std::string text = lv_textarea_get_text(field_);
        const auto position = lv_textarea_get_cursor_pos(field_);
        const size_t cursor = app::utf8ByteAt(text, position);
        if (edit.before > cursor || edit.after > text.size() - cursor ||
            !app::utf8Boundary(text, cursor - edit.before) || !app::utf8Boundary(text, cursor + edit.after)) return false;
        if (preedit_)
        {
            lv_obj_delete(preedit_);
            preedit_ = nullptr;
        }
        const auto before = text.substr(cursor - edit.before, edit.before);
        const auto after = text.substr(cursor, edit.after);
        const auto before_chars = app::utf8Length(before);
        const auto after_chars = app::utf8Length(after);
        for (uint32_t i = 0; field_ && i < before_chars; ++i) lv_textarea_delete_char(field_);
        for (uint32_t i = 0; field_ && i < after_chars; ++i) lv_textarea_delete_char_forward(field_);
        if (field_ && !edit.commit.empty()) lv_textarea_add_text(field_, edit.commit.c_str());
        if (field_ && !edit.preedit.empty())
        {
            preedit_ = lv_label_create(field_);
            lv_obj_add_flag(preedit_, LV_OBJ_FLAG_FLOATING);
            lv_obj_remove_flag(preedit_, LV_OBJ_FLAG_CLICKABLE);
            const std::string display = current.password ? std::string(app::utf8Length(edit.preedit), '*') : edit.preedit;
            lv_label_set_text(preedit_, display.c_str());
            lv_obj_set_style_text_decor(preedit_, LV_TEXT_DECOR_UNDERLINE, 0);
            lv_obj_set_style_bg_opa(preedit_, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(preedit_, lv_obj_get_style_bg_color(field_, LV_PART_MAIN), 0);
            lv_point_t point{};
            lv_label_get_letter_pos(lv_textarea_get_label(field_), lv_textarea_get_cursor_pos(field_), &point);
            lv_obj_set_pos(preedit_, point.x, point.y);
            if (!hidden_cursor)
            {
                const auto start = app::utf8Length(std::string_view(edit.preedit).substr(0, edit.preedit_begin));
                const auto end = app::utf8Length(std::string_view(edit.preedit).substr(0, edit.preedit_end));
                if (start != end)
                {
                    lv_label_set_text_selection_start(preedit_, std::min(start, end));
                    lv_label_set_text_selection_end(preedit_, std::max(start, end));
                }
                else
                {
                    auto* caret = lv_label_create(preedit_);
                    lv_label_set_text(caret, "|");
                    lv_point_t caret_position{};
                    lv_label_get_letter_pos(preedit_, start, &caret_position);
                    lv_obj_set_pos(caret, caret_position.x, caret_position.y);
                    lv_obj_add_flag(preedit_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
                }
            }
        }
        return true;
    }

  private:
    static void deleted(lv_event_t* event)
    {
        auto* self = static_cast<LvglTextInput*>(lv_event_get_user_data(event));
        self->field_ = self->preedit_ = nullptr;
        ++self->focus_;
    }
    lv_obj_t* field_ = nullptr;
    lv_obj_t* preedit_ = nullptr;
    uint64_t focus_ = 1;
};
} // namespace trailmate::cardputer_zero::linux_ui
