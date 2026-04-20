#include "ui/localization.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "platform/ui/settings_store.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/support/lvgl_fs_utils.h"

#if __has_include("lv_binfont_loader.h")
#include "lv_binfont_loader.h"
#define UI_I18N_HAVE_BINFONT 1
#elif __has_include("src/font/lv_binfont_loader.h")
#include "src/font/lv_binfont_loader.h"
#define UI_I18N_HAVE_BINFONT 1
#else
#define UI_I18N_HAVE_BINFONT 0
#endif

namespace ui::i18n
{
namespace
{

constexpr const char* kLogTag = "[I18N]";
constexpr const char* kSettingsNamespace = "settings";
constexpr const char* kDisplayLocaleKey = "display_locale";
constexpr const char* kLegacyDisplayLanguageKey = "display_language";
constexpr const char* kDefaultLocaleId = "en";
constexpr const char* kBuiltinEnglishName = "English";
constexpr const char* kBuiltinLatinFontPackId = "builtin-latin-ui";
constexpr const char* kPackRoot = "/trailmate/packs";
constexpr const char* kFontPackRoot = "/trailmate/packs/fonts";
constexpr const char* kLocalePackRoot = "/trailmate/packs/locales";
constexpr const char* kImePackRoot = "/trailmate/packs/ime";

struct FontPackRecord
{
    std::string id;
    std::string display_name;
    bool builtin = false;
    const lv_font_t* font = nullptr;
    lv_font_t* owned_font = nullptr;
    std::string source_path;
    lv_font_t composed_font{};
    bool composed_font_ready = false;
};

struct ImePackRecord
{
    std::string id;
    std::string display_name;
    std::string backend;
    bool builtin = false;
};

struct LocalePackRecord
{
    std::string id;
    std::string display_name;
    std::string native_name;
    std::string font_pack_id;
    std::string ime_pack_id;
    bool builtin = false;
    std::vector<std::pair<std::string, std::string>> translations;
};

struct ManifestEntry
{
    std::string key;
    std::string value;
};

using Manifest = std::vector<ManifestEntry>;

std::vector<FontPackRecord> s_font_packs;
std::vector<ImePackRecord> s_ime_packs;
std::vector<LocalePackRecord> s_locale_packs;
std::vector<LocaleInfo> s_locale_views;

LocalePackRecord* s_active_locale = nullptr;
FontPackRecord* s_active_font_pack = nullptr;
ImePackRecord* s_active_ime_pack = nullptr;
const lv_font_t* s_active_font_chain = nullptr;
std::string s_active_font_chain_desc;
bool s_registry_ready = false;

const char* safe_text(const char* value)
{
    return value ? value : "<null>";
}

bool external_pack_scan_enabled()
{
#if defined(LV_USE_FS_POSIX) && LV_USE_FS_POSIX
    return true;
#else
    return false;
#endif
}

void trim_in_place(std::string& value)
{
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0)
    {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
    {
        --end;
    }

    if (start == 0 && end == value.size())
    {
        return;
    }

    value = value.substr(start, end - start);
}

std::string trim_copy(std::string value)
{
    trim_in_place(value);
    return value;
}

std::string lowercase_ascii(std::string value)
{
    for (char& ch : value)
    {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string join_path(const std::string& base, const std::string& child)
{
    if (base.empty())
    {
        return child;
    }
    if (child.empty())
    {
        return base;
    }
    if (child.size() >= 2 && child[1] == ':')
    {
        return child;
    }
    if (child.front() == '/')
    {
        return child;
    }
    if (base.back() == '/')
    {
        return base + child;
    }
    return base + "/" + child;
}

std::string normalize_dir_entry(std::string name)
{
    while (!name.empty() && (name.front() == '/' || name.front() == '\\'))
    {
        name.erase(name.begin());
    }
    while (!name.empty() && (name.back() == '/' || name.back() == '\\'))
    {
        name.pop_back();
    }
    return name;
}

std::string decode_escapes(const std::string& text)
{
    std::string out;
    out.reserve(text.size());

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const char ch = text[i];
        if (ch != '\\' || (i + 1U) >= text.size())
        {
            out.push_back(ch);
            continue;
        }

        const char next = text[++i];
        switch (next)
        {
        case 'n':
            out.push_back('\n');
            break;
        case 't':
            out.push_back('\t');
            break;
        case 'r':
            out.push_back('\r');
            break;
        case '\\':
            out.push_back('\\');
            break;
        default:
            out.push_back(next);
            break;
        }
    }

    return out;
}

const char* manifest_value(const Manifest& manifest, const char* key)
{
    if (!key)
    {
        return nullptr;
    }

    const std::string wanted = lowercase_ascii(key);
    for (const auto& entry : manifest)
    {
        if (entry.key == wanted)
        {
            return entry.value.c_str();
        }
    }

    return nullptr;
}

bool parse_manifest_file(const std::string& path, Manifest& out)
{
    out.clear();

    std::string contents;
    if (!::ui::fs::read_text_file(path.c_str(), contents))
    {
        return false;
    }

    std::size_t line_start = 0;
    while (line_start <= contents.size())
    {
        const std::size_t line_end = contents.find('\n', line_start);
        std::string line = contents.substr(
            line_start,
            line_end == std::string::npos ? std::string::npos : (line_end - line_start));
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        trim_in_place(line);

        if (!line.empty() && line[0] != '#' && line[0] != ';')
        {
            const std::size_t sep = line.find('=');
            if (sep != std::string::npos && sep > 0)
            {
                std::string key = lowercase_ascii(trim_copy(line.substr(0, sep)));
                std::string value = trim_copy(line.substr(sep + 1));
                if (!key.empty())
                {
                    out.push_back({std::move(key), std::move(value)});
                }
            }
        }

        if (line_end == std::string::npos)
        {
            break;
        }
        line_start = line_end + 1U;
    }

    return !out.empty();
}

bool list_directory_entries(const char* path, std::vector<std::string>& out)
{
    out.clear();
    if (!external_pack_scan_enabled())
    {
        return false;
    }

#if defined(LV_USE_FS_POSIX) && LV_USE_FS_POSIX
    const std::string normalized = ::ui::fs::normalize_path(path);
    if (normalized.empty())
    {
        return false;
    }

    lv_fs_dir_t dir;
    if (lv_fs_dir_open(&dir, normalized.c_str()) != LV_FS_RES_OK)
    {
        return false;
    }

    char entry_name[256];
    while (true)
    {
        entry_name[0] = '\0';
        if (lv_fs_dir_read(&dir, entry_name, sizeof(entry_name)) != LV_FS_RES_OK)
        {
            break;
        }
        if (entry_name[0] == '\0')
        {
            break;
        }

        std::string normalized_name = normalize_dir_entry(entry_name);
        if (normalized_name.empty() || normalized_name == "." || normalized_name == "..")
        {
            continue;
        }

        out.push_back(std::move(normalized_name));
    }

    lv_fs_dir_close(&dir);
    std::sort(out.begin(), out.end());
    return true;
#else
    (void)path;
    return false;
#endif
}

template <typename TPack>
TPack* find_pack_by_id(std::vector<TPack>& packs, const char* id)
{
    if (!id || id[0] == '\0')
    {
        return nullptr;
    }

    for (auto& pack : packs)
    {
        if (pack.id == id)
        {
            return &pack;
        }
    }

    return nullptr;
}

template <typename TPack>
const TPack* find_pack_by_id(const std::vector<TPack>& packs, const char* id)
{
    if (!id || id[0] == '\0')
    {
        return nullptr;
    }

    for (const auto& pack : packs)
    {
        if (pack.id == id)
        {
            return &pack;
        }
    }

    return nullptr;
}

void destroy_owned_fonts()
{
    for (auto& pack : s_font_packs)
    {
#if UI_I18N_HAVE_BINFONT
        if (pack.owned_font != nullptr)
        {
            lv_binfont_destroy(pack.owned_font);
            pack.owned_font = nullptr;
        }
#else
        pack.owned_font = nullptr;
#endif
    }
}

void clear_registry()
{
    ::ui::fonts::clear_locale_font_bindings();
    destroy_owned_fonts();

    s_font_packs.clear();
    s_ime_packs.clear();
    s_locale_packs.clear();
    s_locale_views.clear();
    s_active_locale = nullptr;
    s_active_font_pack = nullptr;
    s_active_ime_pack = nullptr;
    s_active_font_chain = nullptr;
    s_active_font_chain_desc.clear();
    s_registry_ready = false;
}

void rebuild_locale_views()
{
    s_locale_views.clear();
    s_locale_views.reserve(s_locale_packs.size());

    for (const auto& pack : s_locale_packs)
    {
        LocaleInfo view{};
        view.id = pack.id.c_str();
        view.display_name = pack.display_name.c_str();
        view.native_name = pack.native_name.empty() ? pack.display_name.c_str() : pack.native_name.c_str();
        view.font_pack_id = pack.font_pack_id.c_str();
        view.ime_pack_id = pack.ime_pack_id.empty() ? nullptr : pack.ime_pack_id.c_str();
        view.builtin = pack.builtin;
        s_locale_views.push_back(view);
    }
}

void add_builtin_font_packs()
{
    FontPackRecord latin_pack{};
    latin_pack.id = kBuiltinLatinFontPackId;
    latin_pack.display_name = "Latin UI";
    latin_pack.builtin = true;
    s_font_packs.push_back(latin_pack);
}

void add_builtin_locale_packs()
{
    LocalePackRecord english_pack{};
    english_pack.id = kDefaultLocaleId;
    english_pack.display_name = kBuiltinEnglishName;
    english_pack.native_name = kBuiltinEnglishName;
    english_pack.font_pack_id = kBuiltinLatinFontPackId;
    english_pack.builtin = true;
    s_locale_packs.push_back(english_pack);
}

std::string resolve_pack_file_path(const std::string& pack_dir, const char* path_value)
{
    if (!path_value || path_value[0] == '\0')
    {
        return {};
    }

    return join_path(pack_dir, path_value);
}

bool load_external_font_pack(const std::string& pack_dir)
{
    const std::string manifest_path = join_path(pack_dir, "manifest.ini");
    Manifest manifest;
    if (!parse_manifest_file(manifest_path, manifest))
    {
        return false;
    }

    const char* kind = manifest_value(manifest, "kind");
    if (!kind || std::strcmp(kind, "font") != 0)
    {
        return false;
    }

    const char* id = manifest_value(manifest, "id");
    if (!id || id[0] == '\0' || find_pack_by_id(s_font_packs, id) != nullptr)
    {
        return false;
    }

    FontPackRecord pack{};
    pack.id = id;
    if (const char* display_name = manifest_value(manifest, "display_name"))
    {
        pack.display_name = display_name;
    }
    if (pack.display_name.empty())
    {
        pack.display_name = pack.id;
    }

    const char* source = manifest_value(manifest, "source");
    const std::string source_kind = lowercase_ascii(source ? source : "binfont");
    if (source_kind == "builtin")
    {
        const char* builtin_id = manifest_value(manifest, "builtin_id");
        const FontPackRecord* builtin_pack = find_pack_by_id(s_font_packs, builtin_id);
        if (!builtin_pack)
        {
            std::printf("%s skip font pack id=%s reason=missing_builtin builtin_id=%s\n",
                        kLogTag,
                        pack.id.c_str(),
                        safe_text(builtin_id));
            return false;
        }

        pack.font = builtin_pack->font;
        pack.source_path = builtin_pack->id;
    }
    else
    {
#if UI_I18N_HAVE_BINFONT
        const char* file_value = manifest_value(manifest, "file");
        const std::string font_path = resolve_pack_file_path(pack_dir, file_value);
        if (font_path.empty())
        {
            std::printf("%s skip font pack id=%s reason=missing_file\n", kLogTag, pack.id.c_str());
            return false;
        }

        const std::string normalized_path = ::ui::fs::normalize_path(font_path.c_str());
        lv_font_t* font = lv_binfont_create(normalized_path.c_str());
        if (font == nullptr)
        {
            std::printf("%s skip font pack id=%s reason=load_failed file=%s\n",
                        kLogTag,
                        pack.id.c_str(),
                        normalized_path.c_str());
            return false;
        }

        pack.font = font;
        pack.owned_font = font;
        pack.source_path = normalized_path;
#else
        std::printf("%s skip font pack id=%s reason=binfont_unavailable\n", kLogTag, pack.id.c_str());
        return false;
#endif
    }

    std::printf("%s font pack id=%s builtin=false source=%s\n",
                kLogTag,
                pack.id.c_str(),
                pack.source_path.empty() ? "<none>" : pack.source_path.c_str());
    s_font_packs.push_back(std::move(pack));
    return true;
}

bool load_external_ime_pack(const std::string& pack_dir)
{
    const std::string manifest_path = join_path(pack_dir, "manifest.ini");
    Manifest manifest;
    if (!parse_manifest_file(manifest_path, manifest))
    {
        return false;
    }

    const char* kind = manifest_value(manifest, "kind");
    if (!kind || std::strcmp(kind, "ime") != 0)
    {
        return false;
    }

    const char* id = manifest_value(manifest, "id");
    if (!id || id[0] == '\0' || find_pack_by_id(s_ime_packs, id) != nullptr)
    {
        return false;
    }

    ImePackRecord pack{};
    pack.id = id;
    if (const char* display_name = manifest_value(manifest, "display_name"))
    {
        pack.display_name = display_name;
    }
    if (pack.display_name.empty())
    {
        pack.display_name = pack.id;
    }
    if (const char* backend = manifest_value(manifest, "backend"))
    {
        pack.backend = backend;
    }
    if (pack.backend.empty())
    {
        pack.backend = "none";
    }

    std::printf("%s ime pack id=%s backend=%s\n",
                kLogTag,
                pack.id.c_str(),
                pack.backend.c_str());
    s_ime_packs.push_back(std::move(pack));
    return true;
}

bool parse_locale_strings(const std::string& path,
                          std::vector<std::pair<std::string, std::string>>& out)
{
    out.clear();

    std::string contents;
    if (!::ui::fs::read_text_file(path.c_str(), contents))
    {
        return false;
    }

    std::size_t line_start = 0;
    while (line_start <= contents.size())
    {
        const std::size_t line_end = contents.find('\n', line_start);
        std::string line = contents.substr(
            line_start,
            line_end == std::string::npos ? std::string::npos : (line_end - line_start));
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        if (!line.empty() && line[0] != '#')
        {
            const std::size_t sep = line.find('\t');
            if (sep != std::string::npos && sep > 0 && sep + 1U < line.size())
            {
                std::string english = decode_escapes(line.substr(0, sep));
                std::string localized = decode_escapes(line.substr(sep + 1U));
                if (!english.empty())
                {
                    out.emplace_back(std::move(english), std::move(localized));
                }
            }
        }

        if (line_end == std::string::npos)
        {
            break;
        }
        line_start = line_end + 1U;
    }

    return !out.empty();
}

bool load_external_locale_pack(const std::string& pack_dir)
{
    const std::string manifest_path = join_path(pack_dir, "manifest.ini");
    Manifest manifest;
    if (!parse_manifest_file(manifest_path, manifest))
    {
        return false;
    }

    const char* kind = manifest_value(manifest, "kind");
    if (!kind || std::strcmp(kind, "locale") != 0)
    {
        return false;
    }

    const char* id = manifest_value(manifest, "id");
    if (!id || id[0] == '\0' || find_pack_by_id(s_locale_packs, id) != nullptr)
    {
        return false;
    }

    LocalePackRecord pack{};
    pack.id = id;
    if (const char* display_name = manifest_value(manifest, "display_name"))
    {
        pack.display_name = display_name;
    }
    if (pack.display_name.empty())
    {
        pack.display_name = pack.id;
    }
    if (const char* native_name = manifest_value(manifest, "native_name"))
    {
        pack.native_name = native_name;
    }
    if (pack.native_name.empty())
    {
        pack.native_name = pack.display_name;
    }
    if (const char* font_pack = manifest_value(manifest, "font_pack"))
    {
        pack.font_pack_id = font_pack;
    }
    if (pack.font_pack_id.empty())
    {
        pack.font_pack_id = kBuiltinLatinFontPackId;
    }
    if (const char* ime_pack = manifest_value(manifest, "ime_pack"))
    {
        pack.ime_pack_id = ime_pack;
    }

    const char* strings_value = manifest_value(manifest, "strings");
    const std::string strings_path = resolve_pack_file_path(pack_dir, strings_value);
    if (strings_path.empty() || !parse_locale_strings(strings_path, pack.translations))
    {
        std::printf("%s skip locale pack id=%s reason=missing_strings path=%s\n",
                    kLogTag,
                    pack.id.c_str(),
                    strings_path.empty() ? "<none>" : strings_path.c_str());
        return false;
    }

    std::printf("%s locale pack id=%s font=%s ime=%s strings=%lu\n",
                kLogTag,
                pack.id.c_str(),
                pack.font_pack_id.c_str(),
                pack.ime_pack_id.empty() ? "<none>" : pack.ime_pack_id.c_str(),
                static_cast<unsigned long>(pack.translations.size()));
    s_locale_packs.push_back(std::move(pack));
    return true;
}

void scan_pack_group(const char* root_path, bool (*loader)(const std::string&))
{
    std::vector<std::string> entries;
    if (!list_directory_entries(root_path, entries))
    {
        return;
    }

    for (const auto& entry : entries)
    {
        const std::string pack_dir = join_path(root_path, entry);
        if (!::ui::fs::dir_exists(pack_dir.c_str()))
        {
            continue;
        }
        (void)loader(pack_dir);
    }
}

void load_external_packs()
{
    if (!external_pack_scan_enabled() || !::ui::fs::dir_exists(kPackRoot))
    {
        return;
    }

    scan_pack_group(kFontPackRoot, load_external_font_pack);
    scan_pack_group(kImePackRoot, load_external_ime_pack);
    scan_pack_group(kLocalePackRoot, load_external_locale_pack);
}

bool locale_dependencies_resolved(const LocalePackRecord& locale)
{
    const FontPackRecord* font_pack = find_pack_by_id(s_font_packs, locale.font_pack_id.c_str());
    if (font_pack == nullptr)
    {
        std::printf("%s skip locale id=%s reason=missing_font_pack font=%s\n",
                    kLogTag,
                    locale.id.c_str(),
                    locale.font_pack_id.c_str());
        return false;
    }

    if (!locale.ime_pack_id.empty() &&
        find_pack_by_id(s_ime_packs, locale.ime_pack_id.c_str()) == nullptr)
    {
        std::printf("%s skip locale id=%s reason=missing_ime_pack ime=%s\n",
                    kLogTag,
                    locale.id.c_str(),
                    locale.ime_pack_id.c_str());
        return false;
    }

    return true;
}

void prune_unresolved_locales()
{
    s_locale_packs.erase(
        std::remove_if(
            s_locale_packs.begin(),
            s_locale_packs.end(),
            [](const LocalePackRecord& locale)
            {
                return !locale_dependencies_resolved(locale);
            }),
        s_locale_packs.end());
}

void remove_legacy_locale_key()
{
    const char* keys[] = {kLegacyDisplayLanguageKey};
    ::platform::ui::settings_store::remove_keys(
        kSettingsNamespace, keys, sizeof(keys) / sizeof(keys[0]));
}

std::string migrate_legacy_locale_if_needed()
{
    std::string locale_id;
    if (::platform::ui::settings_store::get_string(kSettingsNamespace, kDisplayLocaleKey, locale_id) &&
        !locale_id.empty())
    {
        return locale_id;
    }

    const int legacy_value =
        ::platform::ui::settings_store::get_int(kSettingsNamespace, kLegacyDisplayLanguageKey, -1);
    if (legacy_value < 0)
    {
        return {};
    }

    locale_id = legacy_value == 1 ? "zh-Hans" : kDefaultLocaleId;
    (void)::platform::ui::settings_store::put_string(
        kSettingsNamespace, kDisplayLocaleKey, locale_id.c_str());
    remove_legacy_locale_key();

    std::printf("%s migrate legacy display_language=%d -> display_locale=%s\n",
                kLogTag,
                legacy_value,
                locale_id.c_str());
    return locale_id;
}

LocalePackRecord* resolve_active_locale(const std::string& preferred_id)
{
    if (!preferred_id.empty())
    {
        if (LocalePackRecord* preferred = find_pack_by_id(s_locale_packs, preferred_id.c_str()))
        {
            return preferred;
        }
    }

    if (LocalePackRecord* english = find_pack_by_id(s_locale_packs, kDefaultLocaleId))
    {
        return english;
    }

    return s_locale_packs.empty() ? nullptr : &s_locale_packs.front();
}

void rebuild_font_fallback_chain()
{
    s_active_font_chain = nullptr;
    s_active_font_chain_desc.clear();

    for (auto& pack : s_font_packs)
    {
        pack.composed_font = {};
        pack.composed_font_ready = false;
    }

    std::vector<FontPackRecord*> ordered_packs;
    std::vector<const lv_font_t*> seen_fonts;

    const auto append_pack = [&](FontPackRecord* pack)
    {
        if (pack == nullptr || pack->font == nullptr)
        {
            return;
        }

        for (const lv_font_t* seen : seen_fonts)
        {
            if (seen == pack->font)
            {
                return;
            }
        }

        seen_fonts.push_back(pack->font);
        ordered_packs.push_back(pack);
    };

    append_pack(s_active_font_pack);
    for (auto& pack : s_font_packs)
    {
        append_pack(&pack);
    }

    const lv_font_t* next_fallback = nullptr;
    for (auto it = ordered_packs.rbegin(); it != ordered_packs.rend(); ++it)
    {
        FontPackRecord* pack = *it;
        pack->composed_font = *pack->font;
        pack->composed_font.fallback = next_fallback;
        pack->composed_font_ready = true;
        next_fallback = &pack->composed_font;
    }

    s_active_font_chain = next_fallback;
    for (std::size_t index = 0; index < ordered_packs.size(); ++index)
    {
        if (!s_active_font_chain_desc.empty())
        {
            s_active_font_chain_desc += ",";
        }
        s_active_font_chain_desc += ordered_packs[index]->id;
    }
}

void apply_active_locale(LocalePackRecord* locale)
{
    s_active_locale = locale;
    s_active_font_pack =
        (locale != nullptr) ? find_pack_by_id(s_font_packs, locale->font_pack_id.c_str()) : nullptr;
    s_active_ime_pack =
        (locale != nullptr && !locale->ime_pack_id.empty())
            ? find_pack_by_id(s_ime_packs, locale->ime_pack_id.c_str())
            : nullptr;

    rebuild_font_fallback_chain();
    ::ui::fonts::refresh_locale_font_bindings();

    std::printf("%s active locale=%s font=%s ime=%s chain=%s\n",
                kLogTag,
                s_active_locale ? s_active_locale->id.c_str() : "<none>",
                s_active_font_pack ? s_active_font_pack->id.c_str() : "<none>",
                s_active_ime_pack ? s_active_ime_pack->id.c_str() : "<none>",
                s_active_font_chain_desc.empty() ? "<none>" : s_active_font_chain_desc.c_str());
}

void rebuild_registry()
{
    clear_registry();
    add_builtin_font_packs();
    add_builtin_locale_packs();
    load_external_packs();
    prune_unresolved_locales();
    rebuild_locale_views();
    s_registry_ready = true;

    const std::string preferred_locale = migrate_legacy_locale_if_needed();
    apply_active_locale(resolve_active_locale(preferred_locale));
}

void ensure_registry()
{
    if (!s_registry_ready)
    {
        rebuild_registry();
    }
}

const char* lookup_translation(const LocalePackRecord& locale, const char* english)
{
    if (!english || english[0] == '\0')
    {
        return "";
    }

    for (const auto& entry : locale.translations)
    {
        if (entry.first == english)
        {
            return entry.second.c_str();
        }
    }

    return english;
}

} // namespace

void reload_language()
{
    rebuild_registry();
}

std::size_t locale_count()
{
    ensure_registry();
    return s_locale_views.size();
}

const LocaleInfo* locale_at(std::size_t index)
{
    ensure_registry();
    if (index >= s_locale_views.size())
    {
        return nullptr;
    }
    return &s_locale_views[index];
}

int current_locale_index()
{
    ensure_registry();
    if (s_active_locale == nullptr)
    {
        return 0;
    }

    for (std::size_t index = 0; index < s_locale_packs.size(); ++index)
    {
        if (&s_locale_packs[index] == s_active_locale)
        {
            return static_cast<int>(index);
        }
    }

    return 0;
}

const char* current_locale_id()
{
    ensure_registry();
    return s_active_locale ? s_active_locale->id.c_str() : kDefaultLocaleId;
}

const char* current_locale_display_name()
{
    ensure_registry();
    if (s_active_locale == nullptr)
    {
        return kBuiltinEnglishName;
    }
    return s_active_locale->native_name.empty() ? s_active_locale->display_name.c_str()
                                                : s_active_locale->native_name.c_str();
}

bool set_locale(const char* locale_id, bool persist)
{
    ensure_registry();
    LocalePackRecord* next_locale = find_pack_by_id(s_locale_packs, locale_id);
    if (next_locale == nullptr)
    {
        return false;
    }

    const bool changed = s_active_locale != next_locale;
    apply_active_locale(next_locale);

    if (persist)
    {
        (void)::platform::ui::settings_store::put_string(
            kSettingsNamespace, kDisplayLocaleKey, next_locale->id.c_str());
        remove_legacy_locale_key();
    }

    return changed;
}

bool set_locale_by_index(std::size_t index, bool persist)
{
    const LocaleInfo* locale = locale_at(index);
    return locale != nullptr && set_locale(locale->id, persist);
}

const char* active_font_pack_id()
{
    ensure_registry();
    return s_active_font_pack ? s_active_font_pack->id.c_str() : nullptr;
}

const lv_font_t* active_font_fallback()
{
    ensure_registry();
    return s_active_font_chain;
}

const char* active_ime_pack_id()
{
    ensure_registry();
    return s_active_ime_pack ? s_active_ime_pack->id.c_str() : nullptr;
}

bool active_locale_supports_script_input()
{
    ensure_registry();
    return s_active_ime_pack != nullptr && !s_active_ime_pack->backend.empty() &&
           s_active_ime_pack->backend != "none";
}

const char* tr(const char* english)
{
    ensure_registry();
    if (!english)
    {
        return "";
    }
    if (s_active_locale == nullptr || s_active_locale->id == kDefaultLocaleId)
    {
        return english;
    }
    return lookup_translation(*s_active_locale, english);
}

std::string vformat(const char* english_fmt, va_list args)
{
    const char* localized_fmt = tr(english_fmt);
    if (localized_fmt == nullptr)
    {
        return {};
    }

    va_list size_args;
    va_copy(size_args, args);
    const int size = std::vsnprintf(nullptr, 0, localized_fmt, size_args);
    va_end(size_args);
    if (size <= 0)
    {
        return localized_fmt;
    }

    std::vector<char> buffer(static_cast<std::size_t>(size) + 1U, '\0');
    va_list write_args;
    va_copy(write_args, args);
    std::vsnprintf(buffer.data(), buffer.size(), localized_fmt, write_args);
    va_end(write_args);
    return std::string(buffer.data());
}

std::string format(const char* english_fmt, ...)
{
    va_list args;
    va_start(args, english_fmt);
    std::string out = vformat(english_fmt, args);
    va_end(args);
    return out;
}

void set_label_text(lv_obj_t* label, const char* english)
{
    const char* localized = tr(english);
    if (label)
    {
        lv_label_set_text(label, localized);
        ::ui::fonts::apply_localized_font(label, localized, lv_obj_get_style_text_font(label, LV_PART_MAIN));
    }
}

void set_label_text_raw(lv_obj_t* label, const char* text)
{
    if (label)
    {
        lv_label_set_text(label, text ? text : "");
        ::ui::fonts::apply_localized_font(
            label, text ? text : "", lv_obj_get_style_text_font(label, LV_PART_MAIN));
    }
}

void set_label_text_fmt(lv_obj_t* label, const char* english_fmt, ...)
{
    if (!label)
    {
        return;
    }

    va_list args;
    va_start(args, english_fmt);
    const std::string text = vformat(english_fmt, args);
    va_end(args);
    set_label_text_raw(label, text.c_str());
}

} // namespace ui::i18n
