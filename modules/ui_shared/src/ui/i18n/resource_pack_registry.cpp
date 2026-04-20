#include "ui/localization.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "platform/ui/settings_store.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/runtime/memory_profile.h"
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

enum class FontPackUsage : uint8_t
{
    UiOnly = 0,
    ContentOnly,
    Both,
};

struct CodepointRange
{
    uint32_t first = 0;
    uint32_t last = 0;
};

struct FontPackRecord
{
    std::string id;
    std::string display_name;
    FontPackUsage usage = FontPackUsage::Both;
    bool builtin = false;
    const lv_font_t* builtin_font = nullptr;
    lv_font_t* owned_font = nullptr;
    std::string source_path;
    std::size_t estimated_ram_bytes = 0;
    std::vector<CodepointRange> coverage;
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
    std::string ui_font_pack_id;
    std::string content_font_pack_id;
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

struct FontChainState
{
    std::vector<FontPackRecord*> packs;
    std::vector<lv_font_t> composed_fonts;
    const lv_font_t* head = nullptr;
    std::string desc;
};

std::vector<FontPackRecord> s_font_packs;
std::vector<ImePackRecord> s_ime_packs;
std::vector<LocalePackRecord> s_locale_packs;
std::vector<LocaleInfo> s_locale_views;

LocalePackRecord* s_active_locale = nullptr;
FontPackRecord* s_active_ui_font_pack = nullptr;
FontPackRecord* s_active_content_font_pack = nullptr;
ImePackRecord* s_active_ime_pack = nullptr;

FontChainState s_ui_font_chain;
FontChainState s_content_font_chain;
std::vector<FontPackRecord*> s_content_supplement_packs;

bool s_registry_ready = false;

const char* safe_text(const char* value)
{
    return value ? value : "<null>";
}

const char* usage_name(FontPackUsage usage)
{
    switch (usage)
    {
    case FontPackUsage::UiOnly:
        return "ui";
    case FontPackUsage::ContentOnly:
        return "content";
    case FontPackUsage::Both:
    default:
        return "both";
    }
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

std::size_t parse_size_value(const char* value)
{
    if (!value || value[0] == '\0')
    {
        return 0;
    }

    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (end == value || (end && *end != '\0'))
    {
        return 0;
    }

    return static_cast<std::size_t>(parsed);
}

bool parse_codepoint_token(const std::string& token, uint32_t& out_codepoint)
{
    if (token.empty())
    {
        return false;
    }

    char* end = nullptr;
    const unsigned long value = std::strtoul(token.c_str(), &end, 0);
    if (end == token.c_str() || (end && *end != '\0') || value > 0x10FFFFUL)
    {
        return false;
    }

    out_codepoint = static_cast<uint32_t>(value);
    return true;
}

void normalize_ranges(std::vector<CodepointRange>& ranges)
{
    if (ranges.empty())
    {
        return;
    }

    std::sort(ranges.begin(), ranges.end(),
              [](const CodepointRange& lhs, const CodepointRange& rhs)
              {
                  if (lhs.first != rhs.first)
                  {
                      return lhs.first < rhs.first;
                  }
                  return lhs.last < rhs.last;
              });

    std::vector<CodepointRange> merged;
    merged.reserve(ranges.size());
    for (const CodepointRange& range : ranges)
    {
        if (merged.empty() || range.first > (merged.back().last + 1U))
        {
            merged.push_back(range);
            continue;
        }

        if (range.last > merged.back().last)
        {
            merged.back().last = range.last;
        }
    }

    ranges.swap(merged);
}

bool parse_ranges_file(const std::string& path, std::vector<CodepointRange>& out)
{
    out.clear();

    std::string contents;
    if (!::ui::fs::read_text_file(path.c_str(), contents))
    {
        return false;
    }

    std::size_t token_start = 0;
    while (token_start <= contents.size())
    {
        const std::size_t token_end = contents.find_first_of(",\r\n", token_start);
        std::string token = contents.substr(
            token_start,
            token_end == std::string::npos ? std::string::npos : (token_end - token_start));
        trim_in_place(token);
        if (!token.empty())
        {
            const std::size_t dash = token.find('-');
            uint32_t first = 0;
            uint32_t last = 0;
            if (dash == std::string::npos)
            {
                if (parse_codepoint_token(token, first))
                {
                    last = first;
                    CodepointRange range;
                    range.first = first;
                    range.last = last;
                    out.push_back(range);
                }
            }
            else
            {
                const std::string lhs = trim_copy(token.substr(0, dash));
                const std::string rhs = trim_copy(token.substr(dash + 1));
                if (parse_codepoint_token(lhs, first) && parse_codepoint_token(rhs, last) &&
                    first <= last)
                {
                    CodepointRange range;
                    range.first = first;
                    range.last = last;
                    out.push_back(range);
                }
            }
        }

        if (token_end == std::string::npos)
        {
            break;
        }
        token_start = token_end + 1U;
    }

    normalize_ranges(out);
    return !out.empty();
}

FontPackUsage parse_font_usage(const char* value)
{
    const std::string normalized = lowercase_ascii(value ? value : "");
    if (normalized == "ui")
    {
        return FontPackUsage::UiOnly;
    }
    if (normalized == "content")
    {
        return FontPackUsage::ContentOnly;
    }
    return FontPackUsage::Both;
}

bool font_pack_supports_ui(const FontPackRecord& pack)
{
    return pack.usage == FontPackUsage::UiOnly || pack.usage == FontPackUsage::Both;
}

bool font_pack_supports_content(const FontPackRecord& pack)
{
    return pack.usage == FontPackUsage::ContentOnly || pack.usage == FontPackUsage::Both;
}

std::size_t estimated_pack_ram_bytes(const FontPackRecord* pack)
{
    if (!pack || pack->builtin)
    {
        return 0;
    }

    return pack->estimated_ram_bytes;
}

std::size_t active_locale_font_ram_bytes(const FontPackRecord* ui_pack, const FontPackRecord* content_pack)
{
    std::size_t total = 0;
    total += estimated_pack_ram_bytes(ui_pack);
    if (content_pack != ui_pack)
    {
        total += estimated_pack_ram_bytes(content_pack);
    }
    return total;
}

bool locale_font_budget_allows(const FontPackRecord* ui_pack, const FontPackRecord* content_pack)
{
    const std::size_t total = active_locale_font_ram_bytes(ui_pack, content_pack);
    if (total == 0)
    {
        return true;
    }

    return total <= ::ui::runtime::current_memory_profile().max_locale_font_ram_bytes;
}

const lv_font_t* resolved_font(const FontPackRecord* pack)
{
    if (!pack)
    {
        return nullptr;
    }

    return pack->builtin ? pack->builtin_font : pack->owned_font;
}

bool is_font_runtime_loaded(const FontPackRecord& pack)
{
    return resolved_font(&pack) != nullptr;
}

bool font_pack_covers_codepoint(const FontPackRecord& pack, uint32_t codepoint)
{
    if (pack.coverage.empty())
    {
        return false;
    }

    std::size_t left = 0;
    std::size_t right = pack.coverage.size();
    while (left < right)
    {
        const std::size_t mid = left + ((right - left) / 2U);
        const CodepointRange& range = pack.coverage[mid];
        if (codepoint < range.first)
        {
            right = mid;
        }
        else if (codepoint > range.last)
        {
            left = mid + 1U;
        }
        else
        {
            return true;
        }
    }

    return false;
}

bool decode_next_utf8(const unsigned char*& ptr, uint32_t& out_codepoint)
{
    if (!ptr || *ptr == 0)
    {
        return false;
    }

    const unsigned char lead = *ptr++;
    if ((lead & 0x80U) == 0)
    {
        out_codepoint = lead;
        return true;
    }

    int extra_bytes = 0;
    uint32_t codepoint = 0;
    if ((lead & 0xE0U) == 0xC0U)
    {
        extra_bytes = 1;
        codepoint = static_cast<uint32_t>(lead & 0x1FU);
    }
    else if ((lead & 0xF0U) == 0xE0U)
    {
        extra_bytes = 2;
        codepoint = static_cast<uint32_t>(lead & 0x0FU);
    }
    else if ((lead & 0xF8U) == 0xF0U)
    {
        extra_bytes = 3;
        codepoint = static_cast<uint32_t>(lead & 0x07U);
    }
    else
    {
        out_codepoint = 0xFFFD;
        return true;
    }

    for (int i = 0; i < extra_bytes; ++i)
    {
        const unsigned char next = *ptr;
        if ((next & 0xC0U) != 0x80U)
        {
            out_codepoint = 0xFFFD;
            return true;
        }
        ++ptr;
        codepoint = (codepoint << 6U) | static_cast<uint32_t>(next & 0x3FU);
    }

    out_codepoint = codepoint;
    return true;
}

void collect_non_ascii_codepoints(const char* text, std::vector<uint32_t>& out)
{
    out.clear();
    if (!text || text[0] == '\0')
    {
        return;
    }

    const unsigned char* ptr = reinterpret_cast<const unsigned char*>(text);
    while (*ptr != 0)
    {
        uint32_t codepoint = 0;
        if (!decode_next_utf8(ptr, codepoint))
        {
            break;
        }
        if (codepoint >= 0x80U)
        {
            out.push_back(codepoint);
        }
    }

    if (out.empty())
    {
        return;
    }

    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

std::size_t coverage_hit_count(const FontPackRecord& pack, const std::vector<uint32_t>& missing)
{
    std::size_t hits = 0;
    for (uint32_t codepoint : missing)
    {
        if (font_pack_covers_codepoint(pack, codepoint))
        {
            ++hits;
        }
    }
    return hits;
}

void append_unique_pack(std::vector<FontPackRecord*>& packs, FontPackRecord* pack)
{
    if (!pack)
    {
        return;
    }

    for (FontPackRecord* existing : packs)
    {
        if (existing == pack)
        {
            return;
        }
    }

    packs.push_back(pack);
}

void reset_font_chain(FontChainState& chain)
{
    chain.packs.clear();
    chain.composed_fonts.clear();
    chain.head = nullptr;
    chain.desc.clear();
}

void rebuild_font_chain(FontChainState& chain, const std::vector<FontPackRecord*>& requested_packs)
{
    reset_font_chain(chain);

    std::vector<FontPackRecord*> usable_packs;
    usable_packs.reserve(requested_packs.size());
    for (FontPackRecord* pack : requested_packs)
    {
        if (!pack || resolved_font(pack) == nullptr)
        {
            continue;
        }

        bool duplicate = false;
        for (FontPackRecord* existing : usable_packs)
        {
            if (existing == pack)
            {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
        {
            usable_packs.push_back(pack);
        }
    }

    chain.packs = usable_packs;
    chain.composed_fonts.resize(usable_packs.size());

    const lv_font_t* next_fallback = nullptr;
    for (std::size_t index = usable_packs.size(); index > 0; --index)
    {
        FontPackRecord* pack = usable_packs[index - 1U];
        chain.composed_fonts[index - 1U] = *resolved_font(pack);
        chain.composed_fonts[index - 1U].fallback = next_fallback;
        next_fallback = &chain.composed_fonts[index - 1U];
    }

    chain.head = next_fallback;
    for (std::size_t index = 0; index < usable_packs.size(); ++index)
    {
        if (!chain.desc.empty())
        {
            chain.desc += ",";
        }
        chain.desc += usable_packs[index]->id;
    }
}

void unload_external_font_pack(FontPackRecord& pack)
{
#if UI_I18N_HAVE_BINFONT
    if (pack.owned_font != nullptr)
    {
        std::printf("%s font unload id=%s\n", kLogTag, pack.id.c_str());
        lv_binfont_destroy(pack.owned_font);
        pack.owned_font = nullptr;
    }
#else
    pack.owned_font = nullptr;
#endif
}

void release_runtime_fonts()
{
    for (auto& pack : s_font_packs)
    {
        if (!pack.builtin)
        {
            unload_external_font_pack(pack);
        }
    }

    s_content_supplement_packs.clear();
    reset_font_chain(s_ui_font_chain);
    reset_font_chain(s_content_font_chain);
}

bool load_font_pack(FontPackRecord& pack)
{
    if (pack.builtin)
    {
        return true;
    }
    if (pack.owned_font != nullptr)
    {
        return true;
    }
    if (pack.source_path.empty())
    {
        return false;
    }

#if UI_I18N_HAVE_BINFONT
    pack.owned_font = lv_binfont_create(pack.source_path.c_str());
    if (pack.owned_font == nullptr)
    {
        std::printf("%s font load failed id=%s source=%s\n",
                    kLogTag,
                    pack.id.c_str(),
                    pack.source_path.c_str());
        return false;
    }

    std::printf("%s font load id=%s source=%s ram=%lu\n",
                kLogTag,
                pack.id.c_str(),
                pack.source_path.c_str(),
                static_cast<unsigned long>(pack.estimated_ram_bytes));
    return true;
#else
    std::printf("%s font load skipped id=%s reason=binfont_unavailable\n",
                kLogTag,
                pack.id.c_str());
    return false;
#endif
}

bool ensure_font_pack_loaded(FontPackRecord* pack)
{
    if (!pack)
    {
        return false;
    }
    if (pack->builtin)
    {
        return true;
    }
    return load_font_pack(*pack);
}

std::vector<FontPackRecord*> current_content_pack_sequence()
{
    std::vector<FontPackRecord*> packs;
    packs.reserve(2U + s_content_supplement_packs.size());
    append_unique_pack(packs, s_active_content_font_pack);
    append_unique_pack(packs, s_active_ui_font_pack);
    for (FontPackRecord* pack : s_content_supplement_packs)
    {
        append_unique_pack(packs, pack);
    }
    return packs;
}

void rebuild_runtime_font_chains()
{
    std::vector<FontPackRecord*> ui_packs;
    append_unique_pack(ui_packs, s_active_ui_font_pack);
    rebuild_font_chain(s_ui_font_chain, ui_packs);
    rebuild_font_chain(s_content_font_chain, current_content_pack_sequence());
    ::ui::fonts::refresh_locale_font_bindings();
}

bool ensure_active_content_pack_loaded()
{
    bool changed = false;

    if (s_active_content_font_pack && !is_font_runtime_loaded(*s_active_content_font_pack))
    {
        if (ensure_font_pack_loaded(s_active_content_font_pack))
        {
            changed = true;
        }
    }

    if (s_active_ui_font_pack && !is_font_runtime_loaded(*s_active_ui_font_pack))
    {
        if (ensure_font_pack_loaded(s_active_ui_font_pack))
        {
            changed = true;
        }
    }

    if (changed)
    {
        rebuild_runtime_font_chains();
    }

    return true;
}

bool codepoint_covered_by_loaded_packs(const std::vector<FontPackRecord*>& packs, uint32_t codepoint)
{
    if (codepoint < 0x80U)
    {
        return true;
    }

    for (FontPackRecord* pack : packs)
    {
        if (!pack || !is_font_runtime_loaded(*pack))
        {
            continue;
        }
        if (font_pack_covers_codepoint(*pack, codepoint))
        {
            return true;
        }
    }

    return false;
}

std::vector<uint32_t> missing_content_codepoints(const char* text)
{
    std::vector<uint32_t> codepoints;
    collect_non_ascii_codepoints(text, codepoints);
    if (codepoints.empty())
    {
        return {};
    }

    const std::vector<FontPackRecord*> packs = current_content_pack_sequence();
    std::vector<uint32_t> missing;
    missing.reserve(codepoints.size());
    for (uint32_t codepoint : codepoints)
    {
        if (!codepoint_covered_by_loaded_packs(packs, codepoint))
        {
            missing.push_back(codepoint);
        }
    }
    return missing;
}

std::size_t content_supplement_ram_bytes()
{
    std::size_t total = 0;
    for (const FontPackRecord* pack : s_content_supplement_packs)
    {
        if (pack)
        {
            total += pack->estimated_ram_bytes;
        }
    }
    return total;
}

bool can_add_content_supplement(const FontPackRecord& pack)
{
    const auto& profile = ::ui::runtime::current_memory_profile();
    if (profile.max_content_supplement_packs == 0 || profile.max_content_supplement_ram_bytes == 0)
    {
        return false;
    }

    for (const FontPackRecord* existing : s_content_supplement_packs)
    {
        if (existing == &pack)
        {
            return true;
        }
    }

    if (s_content_supplement_packs.size() >= profile.max_content_supplement_packs)
    {
        return false;
    }

    if (pack.estimated_ram_bytes == 0)
    {
        return true;
    }

    return (content_supplement_ram_bytes() + pack.estimated_ram_bytes) <=
           profile.max_content_supplement_ram_bytes;
}

FontPackRecord* choose_content_supplement(const std::vector<uint32_t>& missing)
{
    FontPackRecord* best = nullptr;
    std::size_t best_hits = 0;
    std::size_t best_bytes = 0;

    for (auto& pack : s_font_packs)
    {
        if (&pack == s_active_content_font_pack || &pack == s_active_ui_font_pack)
        {
            continue;
        }
        if (!font_pack_supports_content(pack))
        {
            continue;
        }
        if (pack.coverage.empty())
        {
            continue;
        }
        if (!can_add_content_supplement(pack))
        {
            continue;
        }

        const std::size_t hits = coverage_hit_count(pack, missing);
        if (hits == 0)
        {
            continue;
        }

        const std::size_t bytes = pack.estimated_ram_bytes;
        if (best == nullptr || hits > best_hits || (hits == best_hits && bytes < best_bytes))
        {
            best = &pack;
            best_hits = hits;
            best_bytes = bytes;
        }
    }

    return best;
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
        view.ui_font_pack_id = pack.ui_font_pack_id.c_str();
        view.content_font_pack_id = pack.content_font_pack_id.c_str();
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
    latin_pack.usage = FontPackUsage::Both;
    latin_pack.builtin = true;
    latin_pack.builtin_font = nullptr;
    latin_pack.estimated_ram_bytes = 0;
    s_font_packs.push_back(std::move(latin_pack));
}

void add_builtin_locale_packs()
{
    LocalePackRecord english_pack{};
    english_pack.id = kDefaultLocaleId;
    english_pack.display_name = kBuiltinEnglishName;
    english_pack.native_name = kBuiltinEnglishName;
    english_pack.ui_font_pack_id = kBuiltinLatinFontPackId;
    english_pack.content_font_pack_id = kBuiltinLatinFontPackId;
    english_pack.builtin = true;
    s_locale_packs.push_back(std::move(english_pack));
}

std::string resolve_pack_file_path(const std::string& pack_dir, const char* path_value)
{
    if (!path_value || path_value[0] == '\0')
    {
        return {};
    }

    return join_path(pack_dir, path_value);
}

bool catalog_external_font_pack(const std::string& pack_dir)
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
    pack.usage = parse_font_usage(manifest_value(manifest, "usage"));
    pack.estimated_ram_bytes = parse_size_value(manifest_value(manifest, "estimated_ram_bytes"));

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

        pack.builtin = true;
        pack.builtin_font = builtin_pack->builtin_font;
        pack.source_path = builtin_pack->id;
    }
    else
    {
        const char* file_value = manifest_value(manifest, "file");
        const std::string font_path = resolve_pack_file_path(pack_dir, file_value);
        if (font_path.empty())
        {
            std::printf("%s skip font pack id=%s reason=missing_file\n", kLogTag, pack.id.c_str());
            return false;
        }
        pack.source_path = ::ui::fs::normalize_path(font_path.c_str());
    }

    std::string ranges_path;
    if (const char* ranges_value = manifest_value(manifest, "ranges"))
    {
        ranges_path = resolve_pack_file_path(pack_dir, ranges_value);
    }
    else
    {
        const std::string fallback_ranges_path = join_path(pack_dir, "ranges.txt");
        if (::ui::fs::file_exists(fallback_ranges_path.c_str()))
        {
            ranges_path = fallback_ranges_path;
        }
    }

    if (!ranges_path.empty())
    {
        (void)parse_ranges_file(ranges_path, pack.coverage);
    }

    std::printf("%s font pack catalog id=%s builtin=%d usage=%s ram=%lu source=%s\n",
                kLogTag,
                pack.id.c_str(),
                pack.builtin ? 1 : 0,
                usage_name(pack.usage),
                static_cast<unsigned long>(pack.estimated_ram_bytes),
                pack.source_path.empty() ? "<none>" : pack.source_path.c_str());
    s_font_packs.push_back(std::move(pack));
    return true;
}

bool catalog_external_ime_pack(const std::string& pack_dir)
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

    std::printf("%s ime pack catalog id=%s backend=%s\n",
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

bool catalog_external_locale_pack(const std::string& pack_dir)
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

    if (const char* ui_font_pack = manifest_value(manifest, "ui_font_pack"))
    {
        pack.ui_font_pack_id = ui_font_pack;
    }
    if (const char* content_font_pack = manifest_value(manifest, "content_font_pack"))
    {
        pack.content_font_pack_id = content_font_pack;
    }
    if (pack.content_font_pack_id.empty())
    {
        pack.content_font_pack_id = pack.ui_font_pack_id;
    }
    if (pack.ui_font_pack_id.empty())
    {
        pack.ui_font_pack_id = kBuiltinLatinFontPackId;
    }
    if (pack.content_font_pack_id.empty())
    {
        pack.content_font_pack_id = pack.ui_font_pack_id;
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

    std::printf("%s locale pack catalog id=%s ui_font=%s content_font=%s ime=%s strings=%lu\n",
                kLogTag,
                pack.id.c_str(),
                pack.ui_font_pack_id.c_str(),
                pack.content_font_pack_id.c_str(),
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

void catalog_external_packs()
{
    if (!external_pack_scan_enabled() || !::ui::fs::dir_exists(kPackRoot))
    {
        return;
    }

    scan_pack_group(kFontPackRoot, catalog_external_font_pack);
    scan_pack_group(kImePackRoot, catalog_external_ime_pack);
    scan_pack_group(kLocalePackRoot, catalog_external_locale_pack);
}

bool locale_dependencies_resolved(const LocalePackRecord& locale)
{
    const FontPackRecord* ui_pack = find_pack_by_id(s_font_packs, locale.ui_font_pack_id.c_str());
    const FontPackRecord* content_pack =
        find_pack_by_id(s_font_packs, locale.content_font_pack_id.c_str());

    if (ui_pack == nullptr)
    {
        std::printf("%s skip locale id=%s reason=missing_ui_font_pack font=%s\n",
                    kLogTag,
                    locale.id.c_str(),
                    locale.ui_font_pack_id.c_str());
        return false;
    }
    if (content_pack == nullptr)
    {
        std::printf("%s skip locale id=%s reason=missing_content_font_pack font=%s\n",
                    kLogTag,
                    locale.id.c_str(),
                    locale.content_font_pack_id.c_str());
        return false;
    }
    if (!font_pack_supports_ui(*ui_pack))
    {
        std::printf("%s skip locale id=%s reason=ui_font_usage font=%s usage=%s\n",
                    kLogTag,
                    locale.id.c_str(),
                    ui_pack->id.c_str(),
                    usage_name(ui_pack->usage));
        return false;
    }
    if (!font_pack_supports_content(*content_pack))
    {
        std::printf("%s skip locale id=%s reason=content_font_usage font=%s usage=%s\n",
                    kLogTag,
                    locale.id.c_str(),
                    content_pack->id.c_str(),
                    usage_name(content_pack->usage));
        return false;
    }
    const std::size_t locale_font_ram = active_locale_font_ram_bytes(ui_pack, content_pack);
    if (!locale_font_budget_allows(ui_pack, content_pack))
    {
        std::printf(
            "%s skip locale id=%s reason=memory_profile profile=%s ui_font=%s content_font=%s locale_font_ram=%lu budget=%lu\n",
            kLogTag,
            locale.id.c_str(),
            ::ui::runtime::current_memory_profile().name,
            ui_pack->id.c_str(),
            content_pack->id.c_str(),
            static_cast<unsigned long>(locale_font_ram),
            static_cast<unsigned long>(
                ::ui::runtime::current_memory_profile().max_locale_font_ram_bytes));
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

void clear_registry()
{
    ::ui::fonts::clear_locale_font_bindings();
    release_runtime_fonts();

    s_font_packs.clear();
    s_ime_packs.clear();
    s_locale_packs.clear();
    s_locale_views.clear();

    s_active_locale = nullptr;
    s_active_ui_font_pack = nullptr;
    s_active_content_font_pack = nullptr;
    s_active_ime_pack = nullptr;

    s_registry_ready = false;
}

bool activate_locale(LocalePackRecord* locale)
{
    release_runtime_fonts();

    s_active_locale = locale;
    s_active_ui_font_pack = locale ? find_pack_by_id(s_font_packs, locale->ui_font_pack_id.c_str()) : nullptr;
    s_active_content_font_pack =
        locale ? find_pack_by_id(s_font_packs, locale->content_font_pack_id.c_str()) : nullptr;
    s_active_ime_pack = (locale && !locale->ime_pack_id.empty())
                            ? find_pack_by_id(s_ime_packs, locale->ime_pack_id.c_str())
                            : nullptr;

    if (s_active_ui_font_pack != nullptr && !ensure_font_pack_loaded(s_active_ui_font_pack))
    {
        if (locale != nullptr && locale->id != kDefaultLocaleId)
        {
            LocalePackRecord* fallback = resolve_active_locale(kDefaultLocaleId);
            if (fallback && fallback != locale)
            {
                return activate_locale(fallback);
            }
        }
    }

    rebuild_runtime_font_chains();

    std::printf("%s active locale=%s ui_font=%s content_font=%s ime=%s ui_chain=%s content_chain=%s\n",
                kLogTag,
                s_active_locale ? s_active_locale->id.c_str() : "<none>",
                s_active_ui_font_pack ? s_active_ui_font_pack->id.c_str() : "<none>",
                s_active_content_font_pack ? s_active_content_font_pack->id.c_str() : "<none>",
                s_active_ime_pack ? s_active_ime_pack->id.c_str() : "<none>",
                s_ui_font_chain.desc.empty() ? "<none>" : s_ui_font_chain.desc.c_str(),
                s_content_font_chain.desc.empty() ? "<none>" : s_content_font_chain.desc.c_str());
    return true;
}

void rebuild_registry()
{
    clear_registry();
    add_builtin_font_packs();
    add_builtin_locale_packs();
    catalog_external_packs();
    prune_unresolved_locales();
    rebuild_locale_views();
    s_registry_ready = true;

    const std::string preferred_locale = migrate_legacy_locale_if_needed();
    (void)activate_locale(resolve_active_locale(preferred_locale));
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

    const LocalePackRecord* previous_locale = s_active_locale;
    (void)activate_locale(next_locale);

    if (persist && s_active_locale != nullptr)
    {
        (void)::platform::ui::settings_store::put_string(
            kSettingsNamespace, kDisplayLocaleKey, s_active_locale->id.c_str());
        remove_legacy_locale_key();
    }

    return previous_locale != s_active_locale;
}

bool set_locale_by_index(std::size_t index, bool persist)
{
    const LocaleInfo* locale = locale_at(index);
    return locale != nullptr && set_locale(locale->id, persist);
}

const lv_font_t* active_ui_font_fallback()
{
    ensure_registry();
    return s_ui_font_chain.head;
}

const lv_font_t* active_content_font_fallback()
{
    ensure_registry();
    return s_content_font_chain.head;
}

bool ensure_content_font_for_text(const char* text)
{
    ensure_registry();
    if (!text || text[0] == '\0')
    {
        return true;
    }

    bool changed = false;
    changed |= ensure_active_content_pack_loaded();

    std::vector<uint32_t> missing = missing_content_codepoints(text);
    while (!missing.empty())
    {
        FontPackRecord* candidate = choose_content_supplement(missing);
        if (candidate == nullptr)
        {
            break;
        }
        if (!ensure_font_pack_loaded(candidate))
        {
            break;
        }

        append_unique_pack(s_content_supplement_packs, candidate);
        changed = true;
        rebuild_runtime_font_chains();
        missing = missing_content_codepoints(text);
    }

    if (changed)
    {
        rebuild_runtime_font_chains();
    }

    return missing.empty();
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
        const char* value = text ? text : "";
        lv_label_set_text(label, value);
        ::ui::fonts::apply_localized_font(label, value, lv_obj_get_style_text_font(label, LV_PART_MAIN));
    }
}

void set_content_label_text(lv_obj_t* label, const char* english)
{
    const char* localized = tr(english);
    if (label)
    {
        lv_label_set_text(label, localized);
        ::ui::fonts::apply_content_font(label, localized, lv_obj_get_style_text_font(label, LV_PART_MAIN));
    }
}

void set_content_label_text_raw(lv_obj_t* label, const char* text)
{
    if (label)
    {
        const char* value = text ? text : "";
        lv_label_set_text(label, value);
        ::ui::fonts::apply_content_font(label, value, lv_obj_get_style_text_font(label, LV_PART_MAIN));
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
