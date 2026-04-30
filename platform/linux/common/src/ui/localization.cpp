#include "ui/localization.h"

#include <cstdarg>
#include <cstdio>
#include <string>

#include "ui/assets/fonts/font_utils.h"

namespace ui::i18n
{
namespace
{

constexpr LocaleInfo kEnglishLocale{
    .id = "en",
    .display_name = "English",
    .native_name = "English",
    .ui_font_pack_id = nullptr,
    .content_font_pack_id = nullptr,
    .ime_pack_id = nullptr,
    .builtin = true,
};

} // namespace

void reload_language()
{
    ::ui::fonts::refresh_locale_font_bindings();
}

std::size_t locale_count()
{
    return 1;
}

const LocaleInfo* locale_at(std::size_t index)
{
    return index == 0 ? &kEnglishLocale : nullptr;
}

int current_locale_index()
{
    return 0;
}

const char* current_locale_id()
{
    return kEnglishLocale.id;
}

const char* current_locale_display_name()
{
    return kEnglishLocale.display_name;
}

bool set_locale(const char* locale_id, bool persist)
{
    (void)persist;
    return locale_id != nullptr && std::string(locale_id) == kEnglishLocale.id;
}

bool set_locale_by_index(std::size_t index, bool persist)
{
    (void)persist;
    return index == 0;
}

const lv_font_t* active_ui_font_fallback()
{
    return nullptr;
}

const lv_font_t* active_content_font_fallback()
{
    return nullptr;
}

const lv_font_t* locale_preview_font(const char* locale_id, const lv_font_t* ascii_font)
{
    (void)locale_id;
    return ascii_font;
}

bool ensure_content_font_for_text(const char* text)
{
    (void)text;
    return false;
}

std::size_t ime_count()
{
    return 0;
}

const ImeInfo* ime_at(std::size_t index)
{
    (void)index;
    return nullptr;
}

std::size_t enabled_ime_count()
{
    return 0;
}

bool ime_enabled(const char* ime_id)
{
    (void)ime_id;
    return false;
}

bool set_ime_enabled(const char* ime_id, bool enabled, bool persist)
{
    (void)ime_id;
    (void)enabled;
    (void)persist;
    return false;
}

bool any_enabled_script_input()
{
    return false;
}

const char* active_ime_pack_id()
{
    return nullptr;
}

bool active_locale_supports_script_input()
{
    return false;
}

const char* tr(const char* english)
{
    return english ? english : "";
}

std::string vformat(const char* english_fmt, va_list args)
{
    if (english_fmt == nullptr)
    {
        return {};
    }

    va_list args_copy;
    va_copy(args_copy, args);
    const int required = std::vsnprintf(nullptr, 0, english_fmt, args_copy);
    va_end(args_copy);

    if (required <= 0)
    {
        return {};
    }

    std::string formatted(static_cast<std::size_t>(required), '\0');
    std::vsnprintf(formatted.data(), formatted.size() + 1, english_fmt, args);
    return formatted;
}

std::string format(const char* english_fmt, ...)
{
    va_list args;
    va_start(args, english_fmt);
    std::string formatted = vformat(english_fmt, args);
    va_end(args);
    return formatted;
}

void set_label_text_raw(lv_obj_t* label, const char* text)
{
    if (label == nullptr)
    {
        return;
    }

    const char* safe_text = text ? text : "";
    lv_label_set_text(label, safe_text);
    ::ui::fonts::apply_localized_font(label, safe_text, nullptr);
}

void set_label_text(lv_obj_t* label, const char* english)
{
    set_label_text_raw(label, tr(english));
}

void set_content_label_text_raw(lv_obj_t* label, const char* text)
{
    if (label == nullptr)
    {
        return;
    }

    const char* safe_text = text ? text : "";
    lv_label_set_text(label, safe_text);
    ::ui::fonts::apply_content_font(label, safe_text, nullptr);
}

void set_content_label_text(lv_obj_t* label, const char* english)
{
    set_content_label_text_raw(label, tr(english));
}

void set_label_text_fmt(lv_obj_t* label, const char* english_fmt, ...)
{
    va_list args;
    va_start(args, english_fmt);
    const std::string text = vformat(english_fmt, args);
    va_end(args);
    set_label_text_raw(label, text.c_str());
}

} // namespace ui::i18n
