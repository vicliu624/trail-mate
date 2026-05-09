#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <string>

#include "lvgl.h"

namespace ui::i18n
{

struct LocaleInfo
{
    const char* id = nullptr;
    const char* display_name = nullptr;
    const char* native_name = nullptr;
    const char* ui_font_pack_id = nullptr;
    const char* content_font_pack_id = nullptr;
    const char* ime_pack_id = nullptr;
    const char* direction = nullptr; // "ltr" (default) or "rtl"
    bool builtin = true;
};

struct ImeInfo
{
    const char* id = nullptr;
    const char* display_name = nullptr;
    const char* backend = nullptr;
    bool builtin = true;
};

void reload_language();
std::size_t locale_count();
const LocaleInfo* locale_at(std::size_t index);
int current_locale_index();
const char* current_locale_id();
const char* current_locale_display_name();
const char* current_locale_direction();
bool set_locale(const char* locale_id, bool persist = true);
bool set_locale_by_index(std::size_t index, bool persist = true);
const lv_font_t* active_ui_font_fallback();
const lv_font_t* active_content_font_fallback();
const lv_font_t* locale_preview_font(const char* locale_id, const lv_font_t* ascii_font = nullptr);
bool ensure_content_font_for_text(const char* text);
std::size_t ime_count();
const ImeInfo* ime_at(std::size_t index);
std::size_t enabled_ime_count();
bool ime_enabled(const char* ime_id);
bool set_ime_enabled(const char* ime_id, bool enabled, bool persist = true);
bool any_enabled_script_input();
const char* active_ime_pack_id();
bool active_locale_supports_script_input();
const char* tr(const char* english);
std::string vformat(const char* english_fmt, va_list args);
std::string format(const char* english_fmt, ...);

void set_label_text(lv_obj_t* label, const char* english);
void set_label_text_raw(lv_obj_t* label, const char* text);
void set_content_label_text(lv_obj_t* label, const char* english);
void set_content_label_text_raw(lv_obj_t* label, const char* text);
void set_label_text_fmt(lv_obj_t* label, const char* english_fmt, ...);

#if defined(__cpp_char8_t)
inline void set_label_text(lv_obj_t* label, const char8_t* english)
{
    set_label_text(label, reinterpret_cast<const char*>(english));
}

inline void set_label_text_raw(lv_obj_t* label, const char8_t* text)
{
    set_label_text_raw(label, reinterpret_cast<const char*>(text));
}

inline void set_content_label_text(lv_obj_t* label, const char8_t* english)
{
    set_content_label_text(label, reinterpret_cast<const char*>(english));
}

inline void set_content_label_text_raw(lv_obj_t* label, const char8_t* text)
{
    set_content_label_text_raw(label, reinterpret_cast<const char*>(text));
}
#endif

} // namespace ui::i18n
