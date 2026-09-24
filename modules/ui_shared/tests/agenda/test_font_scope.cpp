#include "ui/assets/fonts/font_utils.h"
#include "ui/page/page_profile.h"
#include "ui/screens/agenda/agenda_page_components.h"

#include <cassert>
#include <cstring>

namespace agenda_test_fonts
{
extern unsigned content_preparations;
}

void testAgendaFontScope()
{
    const auto pager = ui::page_profile::make_pager_profile();
    const auto deck = ui::page_profile::make_tdeck_profile();
    for (const auto* profile : {&pager, &deck})
    {
        ui::page_profile::set_active_profile(profile);
        auto* display = lv_display_create(480, 240);
        for (bool caption : {false, true})
        {
            const auto* base = caption ? ui::page_profile::resolve_caption_font()
                                       : ui::page_profile::resolve_body_font();
            const unsigned before = agenda_test_fonts::content_preparations;
            auto* chrome = ui::agenda::page::components::addLabel(lv_screen_active(), "Today", true, caption);
            assert(agenda_test_fonts::content_preparations == before);
            auto* ui_binding = ui::fonts::find_localized_font_binding(base, ui::fonts::FontScope::Ui);
            assert(ui_binding && lv_obj_get_style_text_font(chrome, LV_PART_MAIN) == &ui_binding->composed);

            // A user title must not inherit the interface-language font scope.
            // Escapes keep this test independent of the Windows source codepage.
            const char* title = "\xE9\x9B\x86\xE5\x90\x88";
            auto* content = ui::agenda::page::components::addLabel(lv_screen_active(), title, false, caption);
            assert(agenda_test_fonts::content_preparations == before + 1);
            auto* content_binding = ui::fonts::find_localized_font_binding(base, ui::fonts::FontScope::Content);
            assert(content_binding && lv_obj_get_style_text_font(content, LV_PART_MAIN) == &content_binding->composed);
            assert(content_binding != ui_binding);
            assert(std::strcmp(lv_label_get_text(content), title) == 0);
        }
        lv_display_delete(display);
    }
    ui::page_profile::set_active_profile(nullptr);
}
