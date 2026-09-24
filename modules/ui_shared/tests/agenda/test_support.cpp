#include "ui/assets/fonts/font_utils.h"
#include "ui/widgets/top_bar_power_presenter.h"

namespace agenda_test_fonts
{
unsigned content_preparations = 0;
ui::fonts::LocalizedFontBinding bindings[12]{};
} // namespace agenda_test_fonts

// Host adapters only: production TopBar, layout, fonts routing and Agenda page
// sources are compiled unchanged. External locale packs and board power are
// outside this Latin-font renderer/input test.
namespace ui::i18n
{
bool test_pinyin_enabled = false;
const char* active_ime_pack_id() { return test_pinyin_enabled ? "pinyin" : ""; }
std::size_t ime_count() { return test_pinyin_enabled ? 1 : 0; }
const ImeInfo* ime_at(std::size_t index)
{
    static const ImeInfo info{"pinyin", "Pinyin", "builtin-pinyin", nullptr, true};
    return index == 0 && test_pinyin_enabled ? &info : nullptr;
}
bool ime_enabled(const char*) { return test_pinyin_enabled; }
void set_content_label_text_raw(lv_obj_t* label, const char* text) { lv_label_set_text(label, text); }
std::string format(const char* fmt, ...)
{
    char buffer[256]{};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return buffer;
}
const char* tr(const char* text) { return text; }
void set_label_text(lv_obj_t* label, const char* text) { lv_label_set_text(label, text); }
const lv_font_t* active_ui_font_fallback() { return nullptr; }
const lv_font_t* active_content_font_fallback() { return nullptr; }
bool ensure_content_font_for_text(const char*)
{
    ++agenda_test_fonts::content_preparations;
    return true;
}
} // namespace ui::i18n
namespace ui::fonts
{
LocalizedFontBinding* localized_font_binding_storage() { return agenda_test_fonts::bindings; }
std::size_t localized_font_binding_storage_size() { return 12; }
} // namespace ui::fonts
namespace ui::widgets::top_bar_power
{
void bind(TopBar& bar) { lv_label_set_text(bar.right_label, "85%"); }
void unbind(TopBar&) {}
} // namespace ui::widgets::top_bar_power
void set_default_group(lv_group_t* group)
{
    lv_group_set_default(group);
    for (auto* device = lv_indev_get_next(nullptr); device; device = lv_indev_get_next(device))
        if (lv_indev_get_type(device) != LV_INDEV_TYPE_POINTER) lv_indev_set_group(device, group);
}
