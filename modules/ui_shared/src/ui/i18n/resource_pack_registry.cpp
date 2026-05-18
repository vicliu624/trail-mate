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
#include "ui/widgets/busy_overlay.h"

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
constexpr const char* kEnabledImeKey = "enabled_imes";
constexpr const char* kLegacyDisplayLanguageKey = "display_language";
constexpr const char* kDefaultLocaleId = "en";
constexpr const char* kBuiltinEnglishName = "English";
constexpr const char* kBuiltinLatinFontPackId = "builtin-latin-ui";
#if UI_FS_HAS_FLASH_PACK_STORAGE
constexpr const char* kFlashPackRoot = "/fs/trailmate/packs";
constexpr const char* kFlashFontPackRoot = "/fs/trailmate/packs/fonts";
constexpr const char* kFlashLocalePackRoot = "/fs/trailmate/packs/locales";
constexpr const char* kFlashImePackRoot = "/fs/trailmate/packs/ime";
#endif
constexpr const char* kPackRoot = "/trailmate/packs";
constexpr const char* kFontPackRoot = "/trailmate/packs/fonts";
constexpr const char* kLocalePackRoot = "/trailmate/packs/locales";
constexpr const char* kImePackRoot = "/trailmate/packs/ime";
constexpr const char* kDisabledImeSentinel = "__none__";
constexpr std::size_t kFontLoadOverlayThresholdBytes = 64U * 1024U;
constexpr unsigned kMaxMissingFontDiagnostics = 20;

#if defined(LV_USE_FS_POSIX) && LV_USE_FS_POSIX
constexpr bool kLvFsPosixEnabled = true;
#else
constexpr bool kLvFsPosixEnabled = false;
#endif

#if UI_FS_HAS_FLASH_PACK_STORAGE
constexpr bool kFlashPackStorageEnabled = true;
#else
constexpr bool kFlashPackStorageEnabled = false;
#endif

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
    std::vector<std::string> preferred_content_supplement_pack_ids;
    std::string ime_pack_id;
    std::string direction; // "ltr" (default) or "rtl"
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
std::vector<ImeInfo> s_ime_views;
std::vector<std::string> s_enabled_ime_ids;

LocalePackRecord* s_active_locale = nullptr;
FontPackRecord* s_active_ui_font_pack = nullptr;
FontPackRecord* s_active_content_font_pack = nullptr;
ImePackRecord* s_active_ime_pack = nullptr;

FontChainState s_ui_font_chain;
FontChainState s_content_font_chain;
std::vector<FontPackRecord*> s_content_supplement_packs;

bool s_registry_ready = false;
unsigned s_missing_content_font_diagnostics = 0;

class ScopedFontLoadOverlay
{
  public:
    explicit ScopedFontLoadOverlay(const FontPackRecord& pack)
        : active_(should_show(pack))
    {
        if (!active_)
        {
            return;
        }

        ::ui::widgets::busy_overlay::show("Loading font pack...", pack.id.c_str());
    }

    ~ScopedFontLoadOverlay()
    {
        if (active_)
        {
            ::ui::widgets::busy_overlay::hide();
        }
    }

    ScopedFontLoadOverlay(const ScopedFontLoadOverlay&) = delete;
    ScopedFontLoadOverlay& operator=(const ScopedFontLoadOverlay&) = delete;

  private:
    static bool should_show(const FontPackRecord& pack)
    {
        if (pack.builtin || pack.source_path.empty())
        {
            return false;
        }
        return pack.estimated_ram_bytes == 0U || pack.estimated_ram_bytes >= kFontLoadOverlayThresholdBytes;
    }

    bool active_ = false;
};

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
#if (defined(LV_USE_FS_POSIX) && LV_USE_FS_POSIX) || UI_FS_HAS_FLASH_PACK_STORAGE
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

std::vector<std::string> split_csv_strings(const char* value)
{
    std::vector<std::string> items;
    if (!value || value[0] == '\0')
    {
        return items;
    }

    std::size_t start = 0;
    const std::string text = value;
    while (start <= text.size())
    {
        const std::size_t end = text.find(',', start);
        std::string item = text.substr(start, end == std::string::npos ? std::string::npos : (end - start));
        trim_in_place(item);
        if (!item.empty())
        {
            items.push_back(std::move(item));
        }
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1U;
    }

    return items;
}

std::string join_csv_strings(const std::vector<std::string>& items)
{
    std::string joined;
    for (std::size_t index = 0; index < items.size(); ++index)
    {
        if (items[index].empty())
        {
            continue;
        }
        if (!joined.empty())
        {
            joined += ',';
        }
        joined += items[index];
    }
    return joined;
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

[[maybe_unused]] std::string normalize_dir_entry(std::string name)
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

    const std::string normalized = ::ui::fs::normalize_path(path);
    if (normalized.empty())
    {
        std::printf("%s external pack scan skip path=%s reason=normalize_failed\n",
                    kLogTag,
                    safe_text(path));
        return false;
    }

    lv_fs_dir_t dir;
    if (lv_fs_dir_open(&dir, normalized.c_str()) != LV_FS_RES_OK)
    {
        std::printf("%s external pack scan skip path=%s normalized=%s reason=open_failed\n",
                    kLogTag,
                    safe_text(path),
                    normalized.c_str());
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
    std::printf("%s external pack scan path=%s normalized=%s entries=%lu\n",
                kLogTag,
                safe_text(path),
                normalized.c_str(),
                static_cast<unsigned long>(out.size()));
    return true;
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

bool string_list_contains(const std::vector<std::string>& values, const char* value)
{
    if (!value || value[0] == '\0')
    {
        return false;
    }

    return std::find(values.begin(), values.end(), value) != values.end();
}

void append_unique_string(std::vector<std::string>& values, const std::string& value)
{
    if (!value.empty() && !string_list_contains(values, value.c_str()))
    {
        values.push_back(value);
    }
}

bool ime_backend_supports_runtime_input(const ImePackRecord& pack)
{
    // The current widget has one real conversion engine and dictionary:
    // Simplified Chinese Pinyin. Other manifests may still be cataloged as
    // pack metadata, but exposing them would route users into the wrong script.
    return pack.id == "zh-hans-pinyin" && pack.backend == "builtin-pinyin";
}

bool ime_backend_can_be_enabled(const ImePackRecord& pack)
{
    return ime_backend_supports_runtime_input(pack);
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

    const char* start = token.c_str();
    int base = 16;
    if ((start[0] == 'U' || start[0] == 'u') && start[1] == '+')
    {
        start += 2;
    }

    char* end = nullptr;
    const unsigned long value = std::strtoul(start, &end, base);
    if (end == start || (end && *end != '\0') || value > 0x10FFFFUL)
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
        const std::size_t comment = token.find('#');
        if (comment != std::string::npos)
        {
            token.resize(comment);
        }
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
    return pack.builtin || resolved_font(&pack) != nullptr;
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

bool should_retain_loaded_external_pack(FontPackRecord* pack)
{
    return pack != nullptr && !pack->builtin && is_font_runtime_loaded(*pack);
}

bool is_retained_pack(const std::vector<FontPackRecord*>& retained_packs, const FontPackRecord* pack)
{
    if (pack == nullptr)
    {
        return false;
    }

    for (const FontPackRecord* retained : retained_packs)
    {
        if (retained == pack)
        {
            return true;
        }
    }

    return false;
}

void append_retained_external_pack(std::vector<FontPackRecord*>& retained_packs, FontPackRecord* pack)
{
    if (should_retain_loaded_external_pack(pack))
    {
        append_unique_pack(retained_packs, pack);
    }
}

bool can_preserve_content_pack(FontPackRecord* pack,
                               FontPackRecord* next_ui_pack,
                               FontPackRecord* next_content_pack)
{
    if (!should_retain_loaded_external_pack(pack) || !font_pack_supports_content(*pack))
    {
        return false;
    }
    if (pack == next_ui_pack || pack == next_content_pack)
    {
        return false;
    }

    const auto& profile = ::ui::runtime::current_memory_profile();
    if (profile.max_content_supplement_packs == 0 || profile.max_content_supplement_ram_bytes == 0)
    {
        return false;
    }

    return pack->estimated_ram_bytes == 0 ||
           pack->estimated_ram_bytes <= profile.max_content_supplement_ram_bytes;
}

void release_runtime_fonts_except(const std::vector<FontPackRecord*>& retained_packs)
{
    for (auto& pack : s_font_packs)
    {
        if (!pack.builtin && !is_retained_pack(retained_packs, &pack))
        {
            unload_external_font_pack(pack);
        }
    }

    s_content_supplement_packs.clear();
    reset_font_chain(s_ui_font_chain);
    reset_font_chain(s_content_font_chain);
}

void release_runtime_fonts()
{
    release_runtime_fonts_except({});
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
    std::printf("%s font load begin id=%s source=%s est_ram=%lu\n",
                kLogTag,
                pack.id.c_str(),
                pack.source_path.c_str(),
                static_cast<unsigned long>(pack.estimated_ram_bytes));
    ScopedFontLoadOverlay overlay(pack);
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

    static std::string s_last_logged_ui_pack;
    static std::string s_last_logged_content_pack;
    static bool s_last_logged_ui_loaded = false;
    static bool s_last_logged_content_loaded = false;
    static bool s_logged_once = false;

    const std::string ui_pack_id = s_active_ui_font_pack ? s_active_ui_font_pack->id : std::string{};
    const std::string content_pack_id =
        s_active_content_font_pack ? s_active_content_font_pack->id : std::string{};
    const bool ui_loaded =
        s_active_ui_font_pack && is_font_runtime_loaded(*s_active_ui_font_pack);
    const bool content_loaded =
        s_active_content_font_pack && is_font_runtime_loaded(*s_active_content_font_pack);
    const bool should_log = changed || !s_logged_once ||
                            ui_pack_id != s_last_logged_ui_pack ||
                            content_pack_id != s_last_logged_content_pack ||
                            ui_loaded != s_last_logged_ui_loaded ||
                            content_loaded != s_last_logged_content_loaded;

    if (should_log && (s_active_content_font_pack != nullptr || s_active_ui_font_pack != nullptr))
    {
        std::printf("%s ensure active fonts ui=%s loaded=%d content=%s loaded=%d changed=%d\n",
                    kLogTag,
                    ui_pack_id.empty() ? "<none>" : ui_pack_id.c_str(),
                    ui_loaded ? 1 : 0,
                    content_pack_id.empty() ? "<none>" : content_pack_id.c_str(),
                    content_loaded ? 1 : 0,
                    changed ? 1 : 0);
        s_last_logged_ui_pack = ui_pack_id;
        s_last_logged_content_pack = content_pack_id;
        s_last_logged_ui_loaded = ui_loaded;
        s_last_logged_content_loaded = content_loaded;
        s_logged_once = true;
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
    auto choose_best_candidate =
        [&](const std::vector<FontPackRecord*>& candidates) -> FontPackRecord*
    {
        FontPackRecord* best = nullptr;
        std::size_t best_hits = 0;
        std::size_t best_bytes = 0;

        for (FontPackRecord* pack : candidates)
        {
            if (!pack || pack == s_active_content_font_pack || pack == s_active_ui_font_pack)
            {
                continue;
            }
            if (!font_pack_supports_content(*pack))
            {
                continue;
            }
            if (pack->coverage.empty())
            {
                continue;
            }
            if (!can_add_content_supplement(*pack))
            {
                continue;
            }

            const std::size_t hits = coverage_hit_count(*pack, missing);
            if (hits == 0)
            {
                continue;
            }

            const std::size_t bytes = pack->estimated_ram_bytes;
            if (best == nullptr || hits > best_hits || (hits == best_hits && bytes < best_bytes))
            {
                best = pack;
                best_hits = hits;
                best_bytes = bytes;
            }
        }

        return best;
    };

    if (s_active_locale != nullptr && !s_active_locale->preferred_content_supplement_pack_ids.empty())
    {
        std::vector<FontPackRecord*> preferred_candidates;
        preferred_candidates.reserve(s_active_locale->preferred_content_supplement_pack_ids.size());
        for (const std::string& pack_id : s_active_locale->preferred_content_supplement_pack_ids)
        {
            if (FontPackRecord* pack = find_pack_by_id(s_font_packs, pack_id.c_str()))
            {
                append_unique_pack(preferred_candidates, pack);
            }
        }

        if (FontPackRecord* best = choose_best_candidate(preferred_candidates))
        {
            return best;
        }
    }

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
        view.direction = pack.direction.empty() ? nullptr : pack.direction.c_str();
        view.builtin = pack.builtin;
        s_locale_views.push_back(view);
    }
}

void rebuild_ime_views()
{
    s_ime_views.clear();
    s_ime_views.reserve(s_ime_packs.size());

    for (const auto& pack : s_ime_packs)
    {
        if (!ime_backend_can_be_enabled(pack))
        {
            continue;
        }

        ImeInfo view{};
        view.id = pack.id.c_str();
        view.display_name = pack.display_name.c_str();
        view.backend = pack.backend.c_str();
        view.builtin = pack.builtin;
        s_ime_views.push_back(view);
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

    std::sort(out.begin(),
              out.end(),
              [](const std::pair<std::string, std::string>& lhs,
                 const std::pair<std::string, std::string>& rhs)
              {
                  return lhs.first < rhs.first;
              });

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

    const char* translation_status = manifest_value(manifest, "translation_status");
    if (translation_status && translation_status[0] != '\0' &&
        lowercase_ascii(translation_status) != "release")
    {
        std::printf("%s skip locale pack id=%s reason=translation_status status=%s\n",
                    kLogTag,
                    id,
                    translation_status);
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
    if (const char* preferred_supplements =
            manifest_value(manifest, "preferred_content_supplement_packs"))
    {
        pack.preferred_content_supplement_pack_ids = split_csv_strings(preferred_supplements);
    }
    else if (const char* supplements = manifest_value(manifest, "content_supplement_packs"))
    {
        pack.preferred_content_supplement_pack_ids = split_csv_strings(supplements);
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

    if (const char* dir = manifest_value(manifest, "direction"))
    {
        pack.direction = dir;
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

void scan_pack_group(const char* root_path, const char* label, bool (*loader)(const std::string&))
{
    std::vector<std::string> entries;
    if (!list_directory_entries(root_path, entries))
    {
        std::printf("%s external pack group unavailable label=%s root=%s\n",
                    kLogTag,
                    safe_text(label),
                    safe_text(root_path));
        return;
    }

    std::size_t directories = 0;
    std::size_t loaded = 0;
    for (const auto& entry : entries)
    {
        const std::string pack_dir = join_path(root_path, entry);
        if (!::ui::fs::dir_exists(pack_dir.c_str()))
        {
            continue;
        }

        ++directories;
        if (loader(pack_dir))
        {
            ++loaded;
        }
    }

    std::printf("%s external pack group scan label=%s root=%s entries=%lu dirs=%lu loaded=%lu\n",
                kLogTag,
                safe_text(label),
                safe_text(root_path),
                static_cast<unsigned long>(entries.size()),
                static_cast<unsigned long>(directories),
                static_cast<unsigned long>(loaded));
}

void log_pack_root_probe(const char* pack_root,
                         const char* font_root,
                         const char* ime_root,
                         const char* locale_root)
{
    std::printf("%s external pack root probe pack=%s exists=%d fonts=%s exists=%d ime=%s exists=%d locales=%s exists=%d\n",
                kLogTag,
                safe_text(pack_root),
                pack_root && ::ui::fs::dir_exists(pack_root) ? 1 : 0,
                safe_text(font_root),
                font_root && ::ui::fs::dir_exists(font_root) ? 1 : 0,
                safe_text(ime_root),
                ime_root && ::ui::fs::dir_exists(ime_root) ? 1 : 0,
                safe_text(locale_root),
                locale_root && ::ui::fs::dir_exists(locale_root) ? 1 : 0);
}

void catalog_external_packs_from_root(const char* pack_root,
                                      const char* font_root,
                                      const char* ime_root,
                                      const char* locale_root)
{
    log_pack_root_probe(pack_root, font_root, ime_root, locale_root);
    if (!pack_root || !::ui::fs::dir_exists(pack_root))
    {
        std::printf("%s external pack root unavailable path=%s\n", kLogTag, safe_text(pack_root));
        return;
    }

    const std::size_t fonts_before = s_font_packs.size();
    const std::size_t imes_before = s_ime_packs.size();
    const std::size_t locales_before = s_locale_packs.size();

    std::printf("%s external pack root scan path=%s\n", kLogTag, pack_root);
    scan_pack_group(font_root, "fonts", catalog_external_font_pack);
    scan_pack_group(ime_root, "ime", catalog_external_ime_pack);
    scan_pack_group(locale_root, "locales", catalog_external_locale_pack);

    std::printf("%s external pack root result path=%s added_fonts=%lu added_ime=%lu added_locales=%lu\n",
                kLogTag,
                pack_root,
                static_cast<unsigned long>(s_font_packs.size() - fonts_before),
                static_cast<unsigned long>(s_ime_packs.size() - imes_before),
                static_cast<unsigned long>(s_locale_packs.size() - locales_before));
}

void catalog_external_packs()
{
    std::printf("%s external pack scan begin enabled=%d lv_fs_posix=%d flash_storage=%d\n",
                kLogTag,
                external_pack_scan_enabled() ? 1 : 0,
                kLvFsPosixEnabled ? 1 : 0,
                kFlashPackStorageEnabled ? 1 : 0);
    if (!external_pack_scan_enabled())
    {
        std::printf("%s external pack scan disabled\n", kLogTag);
        return;
    }

#if UI_FS_HAS_FLASH_PACK_STORAGE
    catalog_external_packs_from_root(
        kFlashPackRoot, kFlashFontPackRoot, kFlashImePackRoot, kFlashLocalePackRoot);
#endif
    catalog_external_packs_from_root(kPackRoot, kFontPackRoot, kImePackRoot, kLocalePackRoot);
    std::printf("%s external pack scan end fonts=%lu ime=%lu locales=%lu\n",
                kLogTag,
                static_cast<unsigned long>(s_font_packs.size()),
                static_cast<unsigned long>(s_ime_packs.size()),
                static_cast<unsigned long>(s_locale_packs.size()));
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
    for (const std::string& supplement_id : locale.preferred_content_supplement_pack_ids)
    {
        const FontPackRecord* supplement = find_pack_by_id(s_font_packs, supplement_id.c_str());
        if (supplement == nullptr)
        {
            std::printf("%s skip locale id=%s reason=missing_content_supplement_pack font=%s\n",
                        kLogTag,
                        locale.id.c_str(),
                        supplement_id.c_str());
            return false;
        }
        if (!font_pack_supports_content(*supplement))
        {
            std::printf(
                "%s skip locale id=%s reason=content_supplement_usage font=%s usage=%s\n",
                kLogTag,
                locale.id.c_str(),
                supplement->id.c_str(),
                usage_name(supplement->usage));
            return false;
        }
    }

    return true;
}

void prune_unresolved_locales()
{
    const std::size_t before = s_locale_packs.size();
    s_locale_packs.erase(
        std::remove_if(
            s_locale_packs.begin(),
            s_locale_packs.end(),
            [](const LocalePackRecord& locale)
            {
                return !locale_dependencies_resolved(locale);
            }),
        s_locale_packs.end());
    std::printf("%s locale dependency prune before=%lu after=%lu removed=%lu\n",
                kLogTag,
                static_cast<unsigned long>(before),
                static_cast<unsigned long>(s_locale_packs.size()),
                static_cast<unsigned long>(before - s_locale_packs.size()));
}

ImePackRecord* resolve_enabled_ime_pack()
{
    if (s_active_ime_pack != nullptr &&
        string_list_contains(s_enabled_ime_ids, s_active_ime_pack->id.c_str()) &&
        ime_backend_supports_runtime_input(*s_active_ime_pack))
    {
        return s_active_ime_pack;
    }

    for (const std::string& ime_id : s_enabled_ime_ids)
    {
        ImePackRecord* pack = find_pack_by_id(s_ime_packs, ime_id.c_str());
        if (pack != nullptr && ime_backend_supports_runtime_input(*pack))
        {
            return pack;
        }
    }

    return nullptr;
}

void enable_default_ime_packs()
{
    s_enabled_ime_ids.clear();
    for (const auto& pack : s_ime_packs)
    {
        if (ime_backend_can_be_enabled(pack))
        {
            s_enabled_ime_ids.push_back(pack.id);
        }
    }
}

void load_enabled_ime_packs()
{
    s_enabled_ime_ids.clear();

    std::string stored;
    if (!::platform::ui::settings_store::get_string(kSettingsNamespace, kEnabledImeKey, stored))
    {
        enable_default_ime_packs();
        std::printf("%s enabled ime packs source=default ids=%s\n",
                    kLogTag,
                    s_enabled_ime_ids.empty() ? "<none>" : join_csv_strings(s_enabled_ime_ids).c_str());
        return;
    }

    const std::vector<std::string> stored_ids = split_csv_strings(stored.c_str());
    if (stored_ids.size() == 1U && stored_ids.front() == kDisabledImeSentinel)
    {
        std::printf("%s enabled ime packs source=stored ids=<none>\n", kLogTag);
        return;
    }

    for (const std::string& ime_id : stored_ids)
    {
        const ImePackRecord* pack = find_pack_by_id(s_ime_packs, ime_id.c_str());
        if (pack != nullptr && ime_backend_can_be_enabled(*pack))
        {
            append_unique_string(s_enabled_ime_ids, pack->id);
        }
    }

    std::printf("%s enabled ime packs source=stored ids=%s\n",
                kLogTag,
                s_enabled_ime_ids.empty() ? "<none>" : join_csv_strings(s_enabled_ime_ids).c_str());
}

bool persist_enabled_ime_packs()
{
    const std::string stored_value =
        s_enabled_ime_ids.empty() ? std::string(kDisabledImeSentinel) : join_csv_strings(s_enabled_ime_ids);
    return ::platform::ui::settings_store::put_string(
        kSettingsNamespace, kEnabledImeKey, stored_value.c_str());
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
        std::printf("%s preferred locale source=settings display_locale=%s\n",
                    kLogTag,
                    locale_id.c_str());
        return locale_id;
    }

    const int legacy_value =
        ::platform::ui::settings_store::get_int(kSettingsNamespace, kLegacyDisplayLanguageKey, -1);
    if (legacy_value < 0)
    {
        std::printf("%s preferred locale source=default display_locale=<none>\n", kLogTag);
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
            std::printf("%s resolve active locale preferred=%s result=preferred\n",
                        kLogTag,
                        preferred_id.c_str());
            return preferred;
        }
        std::printf("%s resolve active locale preferred=%s result=missing fallback=en\n",
                    kLogTag,
                    preferred_id.c_str());
    }

    if (LocalePackRecord* english = find_pack_by_id(s_locale_packs, kDefaultLocaleId))
    {
        std::printf("%s resolve active locale result=en\n", kLogTag);
        return english;
    }

    std::printf("%s resolve active locale result=%s\n",
                kLogTag,
                s_locale_packs.empty() ? "<none>" : s_locale_packs.front().id.c_str());
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
    s_ime_views.clear();
    s_enabled_ime_ids.clear();

    s_active_locale = nullptr;
    s_active_ui_font_pack = nullptr;
    s_active_content_font_pack = nullptr;
    s_active_ime_pack = nullptr;

    s_registry_ready = false;
}

bool activate_locale_internal(LocalePackRecord* locale, FontPackRecord* preserved_content_pack)
{
    FontPackRecord* next_ui_font_pack =
        locale ? find_pack_by_id(s_font_packs, locale->ui_font_pack_id.c_str()) : nullptr;
    FontPackRecord* next_content_font_pack =
        locale ? find_pack_by_id(s_font_packs, locale->content_font_pack_id.c_str()) : nullptr;
    if (preserved_content_pack == nullptr &&
        can_preserve_content_pack(s_active_content_font_pack, next_ui_font_pack, next_content_font_pack))
    {
        preserved_content_pack = s_active_content_font_pack;
    }

    std::vector<FontPackRecord*> retained_packs;
    append_retained_external_pack(retained_packs, next_ui_font_pack);
    append_retained_external_pack(retained_packs, next_content_font_pack);
    append_retained_external_pack(retained_packs, preserved_content_pack);
    release_runtime_fonts_except(retained_packs);

    s_active_locale = locale;
    s_active_ui_font_pack = next_ui_font_pack;
    s_active_content_font_pack = next_content_font_pack;
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
                return activate_locale_internal(fallback, preserved_content_pack);
            }
        }
    }

    if (can_preserve_content_pack(preserved_content_pack, s_active_ui_font_pack, s_active_content_font_pack))
    {
        append_unique_pack(s_content_supplement_packs, preserved_content_pack);
        std::printf("%s font retain id=%s role=content_supplement\n",
                    kLogTag,
                    preserved_content_pack->id.c_str());
    }

    rebuild_runtime_font_chains();
    ImePackRecord* effective_ime_pack = resolve_enabled_ime_pack();

    std::printf("%s active locale=%s ui_font=%s content_font=%s locale_ime=%s effective_ime=%s ui_chain=%s content_chain=%s\n",
                kLogTag,
                s_active_locale ? s_active_locale->id.c_str() : "<none>",
                s_active_ui_font_pack ? s_active_ui_font_pack->id.c_str() : "<none>",
                s_active_content_font_pack ? s_active_content_font_pack->id.c_str() : "<none>",
                s_active_ime_pack ? s_active_ime_pack->id.c_str() : "<none>",
                effective_ime_pack ? effective_ime_pack->id.c_str() : "<none>",
                s_ui_font_chain.desc.empty() ? "<none>" : s_ui_font_chain.desc.c_str(),
                s_content_font_chain.desc.empty() ? "<none>" : s_content_font_chain.desc.c_str());
    return true;
}

bool activate_locale(LocalePackRecord* locale)
{
    return activate_locale_internal(locale, nullptr);
}

void rebuild_registry()
{
    std::printf("%s registry rebuild begin ready=%d\n", kLogTag, s_registry_ready ? 1 : 0);
    s_missing_content_font_diagnostics = 0;
    clear_registry();
    add_builtin_font_packs();
    add_builtin_locale_packs();
    catalog_external_packs();
    std::printf("%s registry cataloged before_prune fonts=%lu ime=%lu locales=%lu\n",
                kLogTag,
                static_cast<unsigned long>(s_font_packs.size()),
                static_cast<unsigned long>(s_ime_packs.size()),
                static_cast<unsigned long>(s_locale_packs.size()));
    prune_unresolved_locales();
    rebuild_locale_views();
    rebuild_ime_views();
    load_enabled_ime_packs();
    s_registry_ready = true;

    const std::string preferred_locale = migrate_legacy_locale_if_needed();
    (void)activate_locale(resolve_active_locale(preferred_locale));
    std::printf("%s registry rebuild end active_locale=%s locale_count=%lu ime_count=%lu enabled_ime=%lu active_ime=%s ui_chain=%s content_chain=%s\n",
                kLogTag,
                s_active_locale ? s_active_locale->id.c_str() : "<none>",
                static_cast<unsigned long>(s_locale_views.size()),
                static_cast<unsigned long>(s_ime_views.size()),
                static_cast<unsigned long>(s_enabled_ime_ids.size()),
                active_ime_pack_id() ? active_ime_pack_id() : "<none>",
                s_ui_font_chain.desc.empty() ? "<none>" : s_ui_font_chain.desc.c_str(),
                s_content_font_chain.desc.empty() ? "<none>" : s_content_font_chain.desc.c_str());
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

    const auto it = std::lower_bound(
        locale.translations.begin(),
        locale.translations.end(),
        english,
        [](const std::pair<std::string, std::string>& entry, const char* key)
        {
            return entry.first < key;
        });

    if (it != locale.translations.end() && it->first == english)
    {
        return it->second.c_str();
    }

    return english;
}

} // namespace

void reload_language()
{
    std::printf("%s reload_language requested\n", kLogTag);
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

const char* current_locale_direction()
{
    ensure_registry();
    if (s_active_locale == nullptr || s_active_locale->direction.empty())
    {
        return "ltr";
    }
    return s_active_locale->direction.c_str();
}

bool set_locale(const char* locale_id, bool persist)
{
    ensure_registry();
    LocalePackRecord* next_locale = find_pack_by_id(s_locale_packs, locale_id);
    if (next_locale == nullptr)
    {
        std::printf("%s set_locale failed requested=%s reason=not_cataloged\n",
                    kLogTag,
                    safe_text(locale_id));
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

    std::printf("%s set_locale requested=%s active=%s changed=%d persist=%d ui_chain=%s content_chain=%s\n",
                kLogTag,
                safe_text(locale_id),
                s_active_locale ? s_active_locale->id.c_str() : "<none>",
                previous_locale != s_active_locale ? 1 : 0,
                persist ? 1 : 0,
                s_ui_font_chain.desc.empty() ? "<none>" : s_ui_font_chain.desc.c_str(),
                s_content_font_chain.desc.empty() ? "<none>" : s_content_font_chain.desc.c_str());
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

const lv_font_t* locale_preview_font(const char* locale_id, const lv_font_t* ascii_font)
{
    ensure_registry();

    const lv_font_t* base_font = ascii_font ? ascii_font : ::ui::fonts::ui_chrome_font();
    if (!locale_id || locale_id[0] == '\0')
    {
        return base_font;
    }

    LocalePackRecord* locale = find_pack_by_id(s_locale_packs, locale_id);
    if (locale == nullptr)
    {
        return base_font;
    }

    FontPackRecord* ui_pack = find_pack_by_id(s_font_packs, locale->ui_font_pack_id.c_str());
    if (ui_pack == nullptr || !ensure_font_pack_loaded(ui_pack))
    {
        return base_font;
    }

    return ::ui::fonts::composed_font_with_fallback(base_font,
                                                    resolved_font(ui_pack),
                                                    ::ui::fonts::FontScope::Ui);
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

    if (!missing.empty() && s_missing_content_font_diagnostics < kMaxMissingFontDiagnostics)
    {
        std::printf("%s content font missing count=%lu first=U+%04lX active_locale=%s content_chain=%s text_sample='%.32s'\n",
                    kLogTag,
                    static_cast<unsigned long>(missing.size()),
                    static_cast<unsigned long>(missing.front()),
                    s_active_locale ? s_active_locale->id.c_str() : "<none>",
                    s_content_font_chain.desc.empty() ? "<none>" : s_content_font_chain.desc.c_str(),
                    text);
        ++s_missing_content_font_diagnostics;
    }

    return missing.empty();
}

std::size_t ime_count()
{
    ensure_registry();
    return s_ime_views.size();
}

const ImeInfo* ime_at(std::size_t index)
{
    ensure_registry();
    if (index >= s_ime_views.size())
    {
        return nullptr;
    }
    return &s_ime_views[index];
}

std::size_t enabled_ime_count()
{
    ensure_registry();
    return s_enabled_ime_ids.size();
}

bool ime_enabled(const char* ime_id)
{
    ensure_registry();
    return string_list_contains(s_enabled_ime_ids, ime_id);
}

bool set_ime_enabled(const char* ime_id, bool enabled, bool persist)
{
    ensure_registry();
    const ImePackRecord* pack = find_pack_by_id(s_ime_packs, ime_id);
    if (pack == nullptr || !ime_backend_can_be_enabled(*pack))
    {
        return false;
    }

    const std::vector<std::string> previous = s_enabled_ime_ids;
    if (enabled)
    {
        append_unique_string(s_enabled_ime_ids, pack->id);
    }
    else
    {
        s_enabled_ime_ids.erase(
            std::remove(s_enabled_ime_ids.begin(), s_enabled_ime_ids.end(), pack->id),
            s_enabled_ime_ids.end());
    }

    if (persist && previous != s_enabled_ime_ids && !persist_enabled_ime_packs())
    {
        s_enabled_ime_ids = previous;
        return false;
    }

    return true;
}

bool any_enabled_script_input()
{
    ensure_registry();
    return resolve_enabled_ime_pack() != nullptr;
}

const char* active_ime_pack_id()
{
    ensure_registry();
    ImePackRecord* pack = resolve_enabled_ime_pack();
    return pack ? pack->id.c_str() : nullptr;
}

bool active_locale_supports_script_input()
{
    ensure_registry();
    return s_active_ime_pack != nullptr &&
           string_list_contains(s_enabled_ime_ids, s_active_ime_pack->id.c_str()) &&
           ime_backend_supports_runtime_input(*s_active_ime_pack);
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
