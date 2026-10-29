#include "ui/lvgl_text_input.h"
#include <cassert>
#include <string>

using namespace trailmate::cardputer_zero;
int main()
{
    assert(app::validUtf8("A\xe4\xb8\xad\xf0\x9f\x99\x82"));
    for (const auto* invalid : {"\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe4\xb8", "\x80"}) assert(!app::validUtf8(invalid));
    lv_init();
    auto* display = lv_display_create(320, 170);
    auto* group = lv_group_create();
    lv_group_set_default(group);
    {
        linux_ui::LvglTextInput input;
        auto* field = lv_textarea_create(lv_screen_active());
        lv_group_add_obj(group, field);
        lv_group_focus_obj(field);
        lv_textarea_set_text(field, "A\xe4\xb8\xad"
                                    "B");
        lv_textarea_set_cursor_pos(field, 2);
        auto state = input.state();
        assert(state.enabled && state.cursor == 4);
        app::TextEdit edit;
        edit.focus = state.focus;
        edit.before = 3;
        edit.after = 1;
        edit.commit = "\xe6\x96\x87\xf0\x9f\x99\x82";
        edit.preedit = "\xe6\x8b\xbc";
        assert(input.apply(edit));
        assert(std::string(lv_textarea_get_text(field)) == "A" + edit.commit);
        const auto children = lv_obj_get_child_count(field);
        // Preedit must be visible without becoming persistent editor contents.
        edit.before = edit.after = 0;
        edit.commit.clear();
        edit.preedit.clear();
        assert(input.apply(edit));
        assert(lv_obj_get_child_count(field) + 1 == children);
        edit.before = 1; // A byte within the emoji is not a character boundary.
        assert(!input.apply(edit));
        edit.before = 0;
        edit.commit = "\xc0\xaf";
        assert(!input.apply(edit));
        edit.commit = "wrong editor";
        auto* next = lv_textarea_create(lv_screen_active());
        lv_group_add_obj(group, next);
        lv_group_focus_obj(next);
        assert(input.state().focus != edit.focus);
        assert(!input.apply(edit));
        assert(std::string(lv_textarea_get_text(next)).empty());
        lv_textarea_set_password_mode(next, true);
        lv_textarea_set_text(next, "secret");
        assert(input.state().password && input.state().surrounding.empty());
        lv_obj_delete(next);
        // Delete notifications invalidate the focus even if an address is reused.
        assert(!input.apply(edit));
        lv_obj_delete(field);
        assert(!input.state().enabled);
    }
    lv_group_delete(group);
    lv_display_delete(display);
    lv_deinit();
}
