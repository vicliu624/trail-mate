#pragma once

#include <cstddef>
#include <cstdio>
#include <cstdint>

#include "lvgl.h"
#include "ui/assets/fonts/fonts.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"

#if !defined(LV_FONT_MONTSERRAT_16) || !LV_FONT_MONTSERRAT_16
#define lv_font_montserrat_16 lv_font_montserrat_14
#endif
#if !defined(LV_FONT_MONTSERRAT_18) || !LV_FONT_MONTSERRAT_18
#define lv_font_montserrat_18 lv_font_montserrat_14
#endif

#ifndef UI_I18N_ROUTE_LOG_ENABLE
#define UI_I18N_ROUTE_LOG_ENABLE 0
#endif

namespace ui::fonts
{

enum class FontScope : uint8_t
{
    Ui = 0,
    Content,
};

inline const char* font_scope_name(FontScope scope)
{
    return scope == FontScope::Content ? "content" : "ui";
}

struct LocalizedFontBinding
{
    const lv_font_t* base = nullptr;
    const lv_font_t* explicit_fallback = nullptr;
    FontScope scope = FontScope::Ui;
    lv_font_t composed{};
    bool used = false;
};

LocalizedFontBinding* localized_font_binding_storage();
std::size_t localized_font_binding_storage_size();

inline bool utf8_has_non_ascii(const char* text)
{
    if (!text)
    {
        return false;
    }

    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p != 0; ++p)
    {
        if (*p >= 0x80)
        {
            return true;
        }
    }
    return false;
}

inline const lv_font_t* ui_chrome_font()
{
    return ::ui::page_profile::current().large_touch_hitbox
               ? &lv_font_montserrat_18
               : ::ui::page_profile::resolve_title_font();
}

inline const lv_font_t* unwrap_localized_font(const lv_font_t* font)
{
    if (!font)
    {
        return nullptr;
    }

    LocalizedFontBinding* bindings = localized_font_binding_storage();
    const std::size_t binding_count = localized_font_binding_storage_size();
    for (std::size_t index = 0; index < binding_count; ++index)
    {
        const LocalizedFontBinding& binding = bindings[index];
        if (binding.used && font == &binding.composed)
        {
            return binding.base;
        }
    }

    return font;
}

inline void sync_localized_font_binding(LocalizedFontBinding& binding)
{
    if (!binding.base)
    {
        return;
    }

    binding.composed = *binding.base;
    const lv_font_t* fallback = binding.explicit_fallback
                                    ? binding.explicit_fallback
                                    : (binding.scope == FontScope::Content
                                           ? ::ui::i18n::active_content_font_fallback()
                                           : ::ui::i18n::active_ui_font_fallback());
    binding.composed.fallback = (fallback && fallback != binding.base) ? fallback : nullptr;
}

inline LocalizedFontBinding* find_localized_font_binding(const lv_font_t* base_font,
                                                         FontScope scope,
                                                         const lv_font_t* explicit_fallback = nullptr)
{
    LocalizedFontBinding* bindings = localized_font_binding_storage();
    const std::size_t binding_count = localized_font_binding_storage_size();
    for (std::size_t index = 0; index < binding_count; ++index)
    {
        LocalizedFontBinding& binding = bindings[index];
        if (binding.used && binding.base == base_font && binding.scope == scope &&
            binding.explicit_fallback == explicit_fallback)
        {
            return &binding;
        }
    }
    return nullptr;
}

inline LocalizedFontBinding* acquire_localized_font_binding(const lv_font_t* base_font,
                                                            FontScope scope,
                                                            const lv_font_t* explicit_fallback = nullptr)
{
    LocalizedFontBinding* bindings = localized_font_binding_storage();
    const std::size_t binding_count = localized_font_binding_storage_size();
    for (std::size_t index = 0; index < binding_count; ++index)
    {
        LocalizedFontBinding& binding = bindings[index];
        if (!binding.used)
        {
            binding.used = true;
            binding.base = base_font;
            binding.explicit_fallback = explicit_fallback;
            binding.scope = scope;
            sync_localized_font_binding(binding);
            return &binding;
        }
    }
    return nullptr;
}

inline void clear_locale_font_bindings()
{
    LocalizedFontBinding* bindings = localized_font_binding_storage();
    const std::size_t binding_count = localized_font_binding_storage_size();
    for (std::size_t index = 0; index < binding_count; ++index)
    {
        LocalizedFontBinding& binding = bindings[index];
        if (!binding.used)
        {
            continue;
        }
        binding.composed = *binding.base;
        binding.composed.fallback = nullptr;
    }
}

inline void refresh_locale_font_bindings()
{
    LocalizedFontBinding* bindings = localized_font_binding_storage();
    const std::size_t binding_count = localized_font_binding_storage_size();
    for (std::size_t index = 0; index < binding_count; ++index)
    {
        LocalizedFontBinding& binding = bindings[index];
        if (!binding.used)
        {
            continue;
        }
        sync_localized_font_binding(binding);
    }
}

inline void log_localized_font_route(FontScope scope,
                                     const char* text,
                                     const lv_font_t* base_font,
                                     const lv_font_t* resolved_font)
{
#if UI_I18N_ROUTE_LOG_ENABLE
    if (!utf8_has_non_ascii(text))
    {
        return;
    }

    static unsigned ui_logs = 0;
    static unsigned content_logs = 0;
    unsigned& logs = scope == FontScope::Content ? content_logs : ui_logs;
    constexpr unsigned kMaxRouteLogs = 80;
    if (logs >= kMaxRouteLogs)
    {
        return;
    }
    ++logs;

    const lv_font_t* fallback = scope == FontScope::Content
                                    ? ::ui::i18n::active_content_font_fallback()
                                    : ::ui::i18n::active_ui_font_fallback();
    std::printf("[I18N][route] route=%s via=localized_font base=%p fallback=%p resolved=%p text='%.32s'\n",
                font_scope_name(scope),
                static_cast<const void*>(base_font),
                static_cast<const void*>(fallback),
                static_cast<const void*>(resolved_font),
                text ? text : "");
#else
    (void)scope;
    (void)text;
    (void)base_font;
    (void)resolved_font;
#endif
}

inline const lv_font_t* localized_font(FontScope scope,
                                       const char* text,
                                       const lv_font_t* ascii_font = nullptr)
{
    if (scope == FontScope::Content)
    {
        (void)::ui::i18n::ensure_content_font_for_text(text);
    }

    const lv_font_t* base_font = unwrap_localized_font(ascii_font ? ascii_font : ui_chrome_font());
    if (!base_font)
    {
        base_font = ui_chrome_font();
    }

    if (LocalizedFontBinding* existing = find_localized_font_binding(base_font, scope))
    {
        sync_localized_font_binding(*existing);
        log_localized_font_route(scope, text, base_font, &existing->composed);
        return &existing->composed;
    }

    if (LocalizedFontBinding* created = acquire_localized_font_binding(base_font, scope))
    {
        log_localized_font_route(scope, text, base_font, &created->composed);
        return &created->composed;
    }

    log_localized_font_route(scope, text, base_font, base_font);
    return base_font;
}

inline const lv_font_t* localized_font(const lv_font_t* ascii_font = nullptr)
{
    return localized_font(FontScope::Ui, nullptr, ascii_font);
}

inline const lv_font_t* composed_font_with_fallback(const lv_font_t* ascii_font,
                                                    const lv_font_t* fallback_font,
                                                    FontScope scope = FontScope::Ui)
{
    const lv_font_t* base_font = unwrap_localized_font(ascii_font ? ascii_font : ui_chrome_font());
    const lv_font_t* fallback = unwrap_localized_font(fallback_font);
    if (!base_font)
    {
        base_font = ui_chrome_font();
    }
    if (!fallback || fallback == base_font)
    {
        return base_font;
    }

    if (LocalizedFontBinding* existing = find_localized_font_binding(base_font, scope, fallback))
    {
        sync_localized_font_binding(*existing);
        return &existing->composed;
    }

    if (LocalizedFontBinding* created = acquire_localized_font_binding(base_font, scope, fallback))
    {
        return &created->composed;
    }

    return base_font;
}

inline const lv_font_t* localized_font(const char* text, const lv_font_t* ascii_font)
{
    return localized_font(FontScope::Ui, text, ascii_font);
}

inline const lv_font_t* content_font(const char* text, const lv_font_t* ascii_font = nullptr)
{
    return localized_font(FontScope::Content, text, ascii_font ? ascii_font : ui_chrome_font());
}

inline const lv_font_t* chat_content_font(const char* text)
{
    return content_font(text, ui_chrome_font());
}

inline void apply_font(lv_obj_t* label, const lv_font_t* font)
{
    if (label && font)
    {
        lv_obj_set_style_text_font(label, font, 0);
        lv_obj_invalidate(label);
    }
}

inline void apply_ui_chrome_font(lv_obj_t* label)
{
    apply_font(label, localized_font(ui_chrome_font()));
}

inline void apply_content_font(lv_obj_t* label, const char* text, const lv_font_t* ascii_font = nullptr)
{
    apply_font(label, content_font(text, ascii_font));
}

inline void apply_chat_content_font(lv_obj_t* label, const char* text)
{
    apply_content_font(label, text, ui_chrome_font());
}

inline void apply_localized_font(lv_obj_t* label, const char* text, const lv_font_t* ascii_font)
{
    apply_font(label, localized_font(FontScope::Ui, text, ascii_font));
}

} // namespace ui::fonts
