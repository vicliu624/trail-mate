#pragma once

#include <cstdarg>
#include <cstdint>
#include <string>

#include "lvgl.h"

namespace ui::i18n
{

enum class Language : uint8_t
{
    English = 0,
    Chinese = 1,
};

Language language_from_raw(int raw_value);
void reload_language();
Language current_language();
bool set_language(Language language, bool persist = true);
bool supports_chinese();
const char* tr(const char* english);
std::string vformat(const char* english_fmt, va_list args);
std::string format(const char* english_fmt, ...);

void set_label_text(lv_obj_t* label, const char* english);
void set_label_text_raw(lv_obj_t* label, const char* text);
void set_label_text_fmt(lv_obj_t* label, const char* english_fmt, ...);

} // namespace ui::i18n
