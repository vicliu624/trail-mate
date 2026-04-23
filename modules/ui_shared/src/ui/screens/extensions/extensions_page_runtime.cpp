#include "ui/screens/extensions/extensions_page_runtime.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "platform/ui/device_runtime.h"
#include "platform/ui/wifi_runtime.h"
#include "ui/app_runtime.h"
#include "ui/components/info_card.h"
#include "ui/components/two_pane_styles.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/presentation/presentation_registry.h"
#include "ui/presentation/directory_browser_layout.h"
#include "ui/runtime/pack_repository.h"
#include "ui/theme/theme_registry.h"
#include "ui/ui_theme.h"
#include "ui/widgets/busy_overlay.h"
#include "ui/widgets/system_notification.h"
#include "ui/widgets/top_bar.h"

#if !defined(LV_FONT_MONTSERRAT_16) || !LV_FONT_MONTSERRAT_16
#define lv_font_montserrat_16 lv_font_montserrat_14
#endif
#if !defined(LV_FONT_MONTSERRAT_14) || !LV_FONT_MONTSERRAT_14
#define lv_font_montserrat_14 lv_font_montserrat_16
#endif

namespace
{

using Host = extensions::ui::shell::Host;
namespace info_card = ::ui::components::info_card;
namespace directory_browser_layout = ::ui::presentation::directory_browser_layout;
namespace two_pane_styles = ::ui::components::two_pane_styles;
namespace packs = ::ui::runtime::packs;

constexpr std::size_t kInvalidIndex = static_cast<std::size_t>(-1);

enum class MainView : uint8_t
{
    List = 0,
    Detail,
};

enum class PackageFilter : uint8_t
{
    All = 0,
    Language,
    Theme,
    Presentation,
};

struct RuntimeState
{
    const Host* host = nullptr;
    lv_group_t* previous_group = nullptr;
    lv_obj_t* root = nullptr;
    lv_obj_t* content = nullptr;
    lv_obj_t* filter_panel = nullptr;
    lv_obj_t* filter_all_btn = nullptr;
    lv_obj_t* filter_language_btn = nullptr;
    lv_obj_t* filter_theme_btn = nullptr;
    lv_obj_t* filter_presentation_btn = nullptr;
    lv_obj_t* main_panel = nullptr;
    lv_obj_t* header_card = nullptr;
    lv_obj_t* title_label = nullptr;
    lv_obj_t* status_label = nullptr;
    lv_obj_t* body_panel = nullptr;
    lv_obj_t* detail_back_btn = nullptr;
    lv_obj_t* primary_action_btn = nullptr;
    lv_obj_t* uninstall_btn = nullptr;
    ::ui::widgets::TopBar top_bar;
    MainView view = MainView::List;
    PackageFilter selected_filter = PackageFilter::All;
    std::string selected_package_id;
    std::vector<packs::PackageRecord> packages;
    std::vector<std::size_t> filtered_indices;
    std::vector<lv_obj_t*> list_buttons;
};

RuntimeState s_runtime{};

class ScopedBusyOverlay
{
  public:
    ScopedBusyOverlay(const char* title, const char* detail = nullptr)
    {
        ::ui::widgets::busy_overlay::show(title, detail);
    }

    ~ScopedBusyOverlay()
    {
        ::ui::widgets::busy_overlay::hide();
    }

    ScopedBusyOverlay(const ScopedBusyOverlay&) = delete;
    ScopedBusyOverlay& operator=(const ScopedBusyOverlay&) = delete;
};

void request_exit()
{
    if (s_runtime.host)
    {
        ::ui::page::request_exit(s_runtime.host);
        return;
    }
    ui_request_exit_to_menu();
}

void on_back(void*)
{
    request_exit();
}

void on_focus_scroll(lv_event_t* event)
{
    if (lv_event_get_code(event) != LV_EVENT_FOCUSED)
    {
        return;
    }

    lv_obj_t* target = lv_event_get_target_obj(event);
    if (target && lv_obj_is_valid(target))
    {
        lv_obj_scroll_to_view(target, LV_ANIM_ON);
    }
}

std::string format_size(std::size_t bytes)
{
    char buffer[32];
    if (bytes >= (1024U * 1024U))
    {
        const unsigned whole = static_cast<unsigned>(bytes / (1024U * 1024U));
        const unsigned tenth =
            static_cast<unsigned>((bytes % (1024U * 1024U)) * 10U / (1024U * 1024U));
        std::snprintf(buffer, sizeof(buffer), "%u.%u MB", whole, tenth);
    }
    else
    {
        std::snprintf(buffer,
                      sizeof(buffer),
                      "%lu KB",
                      static_cast<unsigned long>((bytes + 1023U) / 1024U));
    }
    return buffer;
}

std::string join_values(const std::vector<std::string>& values)
{
    if (values.empty())
    {
        return ::ui::i18n::tr("None");
    }

    std::string joined;
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (i > 0)
        {
            joined += ", ";
        }
        joined += values[i];
    }
    return joined;
}

std::string join_lines(const std::vector<std::string>& values)
{
    if (values.empty())
    {
        return "";
    }

    std::string joined;
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (i > 0)
        {
            joined += "\n";
        }
        joined += values[i];
    }
    return joined;
}

bool contains_value(const std::vector<std::string>& values, const char* value)
{
    if (value == nullptr || value[0] == '\0')
    {
        return false;
    }

    return std::find(values.begin(), values.end(), value) != values.end();
}

std::string installed_or_catalog_version(const packs::PackageRecord& package)
{
    if (package.installed && !package.installed_record.version.empty())
    {
        return package.installed_record.version;
    }
    return package.version;
}

std::string theme_display_name(const char* theme_id)
{
    if (theme_id == nullptr || theme_id[0] == '\0')
    {
        return ::ui::i18n::tr("Unknown");
    }

    ::ui::theme::ThemeInfo info{};
    if (::ui::theme::describe_theme(theme_id, info) && info.display_name && info.display_name[0] != '\0')
    {
        return info.display_name;
    }
    return theme_id;
}

std::string presentation_display_name(const char* presentation_id)
{
    if (presentation_id == nullptr || presentation_id[0] == '\0')
    {
        return ::ui::i18n::tr("Unknown");
    }

    ::ui::presentation::PresentationInfo info{};
    if (::ui::presentation::describe_presentation(presentation_id, info) && info.display_name &&
        info.display_name[0] != '\0')
    {
        return info.display_name;
    }
    return presentation_id;
}

const packs::PackageRecord* find_installed_package_providing_theme(const char* theme_id)
{
    if (theme_id == nullptr || theme_id[0] == '\0')
    {
        return nullptr;
    }

    for (const auto& package : s_runtime.packages)
    {
        if (package.installed && contains_value(package.provided_theme_ids, theme_id))
        {
            return &package;
        }
    }

    for (const auto& package : s_runtime.packages)
    {
        if (contains_value(package.provided_theme_ids, theme_id))
        {
            return &package;
        }
    }

    return nullptr;
}

const packs::PackageRecord* find_installed_package_providing_presentation(const char* presentation_id)
{
    if (presentation_id == nullptr || presentation_id[0] == '\0')
    {
        return nullptr;
    }

    for (const auto& package : s_runtime.packages)
    {
        if (package.installed && contains_value(package.provided_presentation_ids, presentation_id))
        {
            return &package;
        }
    }

    for (const auto& package : s_runtime.packages)
    {
        if (contains_value(package.provided_presentation_ids, presentation_id))
        {
            return &package;
        }
    }

    return nullptr;
}

bool package_provides_active_theme(const packs::PackageRecord& package)
{
    return contains_value(package.provided_theme_ids, ::ui::theme::active_theme_id());
}

bool package_provides_active_presentation(const packs::PackageRecord& package)
{
    return contains_value(package.provided_presentation_ids,
                          ::ui::presentation::active_presentation_id());
}

bool package_is_active_provider(const packs::PackageRecord& package)
{
    return package_provides_active_theme(package) || package_provides_active_presentation(package);
}

std::string active_theme_source_text()
{
    const char* active_id = ::ui::theme::active_theme_id();
    if (active_id == nullptr || active_id[0] == '\0')
    {
        return ::ui::i18n::tr("Current theme: unavailable");
    }

    const std::string active_name = theme_display_name(active_id);
    if (const packs::PackageRecord* package = find_installed_package_providing_theme(active_id))
    {
        return ::ui::i18n::format("Current theme: %s\nPackage: %s %s",
                                  active_name.c_str(),
                                  package->display_name.c_str(),
                                  installed_or_catalog_version(*package).c_str());
    }

    return ::ui::i18n::format("Current theme: %s\nSource: builtin fallback", active_name.c_str());
}

std::string active_presentation_source_text()
{
    const char* active_id = ::ui::presentation::active_presentation_id();
    if (active_id == nullptr || active_id[0] == '\0')
    {
        return ::ui::i18n::tr("Current presentation: unavailable");
    }

    const std::string active_name = presentation_display_name(active_id);
    if (const packs::PackageRecord* package = find_installed_package_providing_presentation(active_id))
    {
        return ::ui::i18n::format("Current presentation: %s\nPackage: %s %s",
                                  active_name.c_str(),
                                  package->display_name.c_str(),
                                  installed_or_catalog_version(*package).c_str());
    }

    return ::ui::i18n::format("Current presentation: %s\nSource: builtin fallback",
                              active_name.c_str());
}

std::string package_activation_text(const packs::PackageRecord& package)
{
    std::vector<std::string> lines;
    if (package_provides_active_theme(package))
    {
        lines.emplace_back(::ui::i18n::format("Active theme: %s",
                                              theme_display_name(::ui::theme::active_theme_id())
                                                  .c_str()));
    }
    if (package_provides_active_presentation(package))
    {
        lines.emplace_back(::ui::i18n::format(
            "Active presentation: %s",
            presentation_display_name(::ui::presentation::active_presentation_id()).c_str()));
    }
    if (!lines.empty())
    {
        lines.emplace_back(::ui::i18n::format("Installed version: %s",
                                              installed_or_catalog_version(package).c_str()));
    }
    return join_lines(lines);
}

bool is_language_package(const packs::PackageRecord& package)
{
    return package.package_type.empty() || package.package_type == "locale-bundle";
}

bool is_theme_package(const packs::PackageRecord& package)
{
    return package.package_type == "theme-pack";
}

bool is_presentation_package(const packs::PackageRecord& package)
{
    return package.package_type == "presentation-pack";
}

bool matches_filter(const packs::PackageRecord& package, PackageFilter filter)
{
    switch (filter)
    {
    case PackageFilter::All:
        return true;
    case PackageFilter::Language:
        return is_language_package(package);
    case PackageFilter::Theme:
        return is_theme_package(package);
    case PackageFilter::Presentation:
        return is_presentation_package(package);
    }
    return true;
}

const char* package_type_label(const packs::PackageRecord& package)
{
    if (is_theme_package(package))
    {
        return ::ui::i18n::tr("Theme Pack");
    }
    if (is_presentation_package(package))
    {
        return ::ui::i18n::tr("Presentation Pack");
    }
    if (is_language_package(package))
    {
        return ::ui::i18n::tr("Language Pack");
    }
    return package.package_type.empty() ? ::ui::i18n::tr("Package") : package.package_type.c_str();
}

const char* filter_label(PackageFilter filter)
{
    switch (filter)
    {
    case PackageFilter::All:
        return "All";
    case PackageFilter::Language:
        return "Language";
    case PackageFilter::Theme:
        return "Theme";
    case PackageFilter::Presentation:
        return "Presentation";
    }
    return "All";
}

bool can_install_or_update(const packs::PackageRecord& package)
{
    return package.compatible_firmware && package.compatible_memory_profile &&
           (!package.installed || package.update_available);
}

const char* primary_action_label(const packs::PackageRecord& package)
{
    if (!package.compatible_firmware || !package.compatible_memory_profile)
    {
        return "Incompatible";
    }
    if (package.update_available)
    {
        return "Update";
    }
    return "Install";
}

std::string compatibility_text(const packs::PackageRecord& package)
{
    if (package.compatible_firmware && package.compatible_memory_profile)
    {
        return ::ui::i18n::tr("Compatible with this device");
    }

    std::vector<std::string> issues;
    if (!package.compatible_memory_profile)
    {
        issues.emplace_back(::ui::i18n::tr("memory profile mismatch"));
    }
    if (!package.compatible_firmware)
    {
        issues.emplace_back(::ui::i18n::format("needs %s", package.min_firmware_version.c_str()));
    }
    return ::ui::i18n::format("Compatibility: %s", join_values(issues).c_str());
}

std::string state_text(const packs::PackageRecord& package)
{
    const std::string activation = package_activation_text(package);
    if (!activation.empty())
    {
        if (package.update_available)
        {
            return activation + "\n" +
                   ::ui::i18n::format("Update available: %s", package.version.c_str());
        }
        return activation;
    }

    if (!package.compatible_firmware || !package.compatible_memory_profile)
    {
        return ::ui::i18n::tr("Incompatible");
    }
    if (package.update_available)
    {
        return ::ui::i18n::format("Update available: %s", package.version.c_str());
    }
    if (package.installed)
    {
        return ::ui::i18n::format("Installed: %s", package.installed_record.version.c_str());
    }
    return ::ui::i18n::tr("Available to install");
}

std::string state_badge_text(const packs::PackageRecord& package)
{
    if (package_is_active_provider(package))
    {
        return ::ui::i18n::tr("Active");
    }
    if (!package.compatible_firmware || !package.compatible_memory_profile)
    {
        return ::ui::i18n::tr("Blocked");
    }
    if (package.update_available)
    {
        return ::ui::i18n::tr("Update");
    }
    if (package.installed)
    {
        return ::ui::i18n::tr("Installed");
    }
    return ::ui::i18n::tr("Ready");
}

lv_color_t state_color(const packs::PackageRecord& package)
{
    if (package_is_active_provider(package))
    {
        return ::ui::theme::accent_strong();
    }
    if (!package.compatible_firmware || !package.compatible_memory_profile)
    {
        return ::ui::theme::error();
    }
    if (package.update_available)
    {
        return ::ui::theme::status_blue();
    }
    if (package.installed)
    {
        return ::ui::theme::status_green();
    }
    return ::ui::theme::accent();
}

std::string package_counts_text(const packs::PackageRecord& package)
{
    if (is_theme_package(package))
    {
        return ::ui::i18n::format("%lu themes  %s",
                                  static_cast<unsigned long>(package.provided_theme_ids.size()),
                                  format_size(package.archive_size_bytes).c_str());
    }
    if (is_presentation_package(package))
    {
        return ::ui::i18n::format("%lu presentations  %s",
                                  static_cast<unsigned long>(package.provided_presentation_ids.size()),
                                  format_size(package.archive_size_bytes).c_str());
    }

    return ::ui::i18n::format("%lu locales  %lu fonts  %lu IME  %s",
                              static_cast<unsigned long>(package.provided_locale_ids.size()),
                              static_cast<unsigned long>(package.provided_font_ids.size()),
                              static_cast<unsigned long>(package.provided_ime_ids.size()),
                              format_size(package.archive_size_bytes).c_str());
}

void update_top_bar_status()
{
    const platform::ui::wifi::Status wifi = platform::ui::wifi::status();
    if (!wifi.supported)
    {
        ::ui::widgets::top_bar_set_right_text(s_runtime.top_bar, ::ui::i18n::tr("No Wi-Fi"));
        return;
    }
    if (!wifi.connected)
    {
        ::ui::widgets::top_bar_set_right_text(s_runtime.top_bar, ::ui::i18n::tr("Offline"));
        return;
    }
    ::ui::widgets::top_bar_set_right_text(s_runtime.top_bar, ::ui::i18n::tr("Online"));
}

void set_header_title(const char* text)
{
    if (s_runtime.title_label == nullptr)
    {
        return;
    }
    if (text == nullptr || text[0] == '\0')
    {
        lv_obj_add_flag(s_runtime.title_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_runtime.title_label, "");
        return;
    }

    lv_obj_clear_flag(s_runtime.title_label, LV_OBJ_FLAG_HIDDEN);
    ::ui::i18n::set_label_text(s_runtime.title_label, text);
}

void set_status_text(const char* text)
{
    if (s_runtime.status_label == nullptr)
    {
        return;
    }
    ::ui::i18n::set_content_label_text_raw(s_runtime.status_label, text ? text : "");
}

void set_status_text(const std::string& text)
{
    set_status_text(text.c_str());
}

void set_filter_checked(lv_obj_t* button, bool checked)
{
    if (!button)
    {
        return;
    }
    if (checked)
    {
        lv_obj_add_state(button, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_remove_state(button, LV_STATE_CHECKED);
    }
}

void refresh_filter_button_states()
{
    set_filter_checked(s_runtime.filter_all_btn, s_runtime.selected_filter == PackageFilter::All);
    set_filter_checked(s_runtime.filter_language_btn, s_runtime.selected_filter == PackageFilter::Language);
    set_filter_checked(s_runtime.filter_theme_btn, s_runtime.selected_filter == PackageFilter::Theme);
    set_filter_checked(s_runtime.filter_presentation_btn,
                       s_runtime.selected_filter == PackageFilter::Presentation);
}

void apply_root_style(lv_obj_t* obj)
{
    if (!obj)
    {
        return;
    }
    lv_obj_set_style_bg_color(obj, ::ui::theme::surface_alt(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
}

void apply_list_item_style(lv_obj_t* item)
{
    if (!item)
    {
        return;
    }
    if (info_card::use_tdeck_layout())
    {
        info_card::apply_item_style(item);
        return;
    }
    two_pane_styles::apply_list_item(item);
}

void style_detail_card(lv_obj_t* card)
{
    if (!card)
    {
        return;
    }

    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, ::ui::theme::surface_alt(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, ::ui::theme::border(), 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_pad_row(card, 5, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* create_wrapped_label(lv_obj_t* parent,
                               const std::string& text,
                               lv_color_t color,
                               bool use_title_font = false)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(label, color, 0);
    if (use_title_font)
    {
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    }
    ::ui::i18n::set_content_label_text_raw(label, text.c_str());
    return label;
}

lv_obj_t* create_section_card(lv_obj_t* parent,
                              const char* title,
                              const std::string& body,
                              lv_color_t body_color = ::ui::theme::text_muted())
{
    lv_obj_t* card = lv_obj_create(parent);
    style_detail_card(card);

    lv_obj_t* title_label = lv_label_create(card);
    lv_obj_set_width(title_label, LV_PCT(100));
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_WRAP);
    two_pane_styles::apply_label_primary(title_label);
    ::ui::i18n::set_label_text(title_label, title ? title : "");

    lv_obj_t* body_label = lv_label_create(card);
    lv_obj_set_width(body_label, LV_PCT(100));
    lv_label_set_long_mode(body_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(body_label, body_color, 0);
    ::ui::i18n::set_content_label_text_raw(body_label, body.c_str());
    return card;
}

void clear_body()
{
    s_runtime.detail_back_btn = nullptr;
    s_runtime.primary_action_btn = nullptr;
    s_runtime.uninstall_btn = nullptr;
    s_runtime.list_buttons.clear();

    if (s_runtime.body_panel != nullptr)
    {
        lv_obj_clean(s_runtime.body_panel);
    }
}

void sync_focus_group(lv_obj_t* preferred_focus = nullptr)
{
    if (app_g == nullptr)
    {
        return;
    }

    lv_group_remove_all_objs(app_g);

    if (s_runtime.top_bar.back_btn != nullptr)
    {
        lv_group_add_obj(app_g, s_runtime.top_bar.back_btn);
    }
    if (s_runtime.filter_all_btn != nullptr)
    {
        lv_group_add_obj(app_g, s_runtime.filter_all_btn);
    }
    if (s_runtime.filter_language_btn != nullptr)
    {
        lv_group_add_obj(app_g, s_runtime.filter_language_btn);
    }
    if (s_runtime.filter_theme_btn != nullptr)
    {
        lv_group_add_obj(app_g, s_runtime.filter_theme_btn);
    }
    if (s_runtime.filter_presentation_btn != nullptr)
    {
        lv_group_add_obj(app_g, s_runtime.filter_presentation_btn);
    }
    if (s_runtime.detail_back_btn != nullptr)
    {
        lv_group_add_obj(app_g, s_runtime.detail_back_btn);
    }
    for (lv_obj_t* btn : s_runtime.list_buttons)
    {
        if (btn != nullptr)
        {
            lv_group_add_obj(app_g, btn);
        }
    }
    if (s_runtime.primary_action_btn != nullptr)
    {
        lv_group_add_obj(app_g, s_runtime.primary_action_btn);
    }
    if (s_runtime.uninstall_btn != nullptr)
    {
        lv_group_add_obj(app_g, s_runtime.uninstall_btn);
    }

    if (preferred_focus != nullptr && lv_obj_is_valid(preferred_focus))
    {
        lv_group_focus_obj(preferred_focus);
    }
    else if (s_runtime.filter_all_btn != nullptr)
    {
        lv_group_focus_obj(s_runtime.filter_all_btn);
    }
    else if (s_runtime.top_bar.back_btn != nullptr)
    {
        lv_group_focus_obj(s_runtime.top_bar.back_btn);
    }

    set_default_group(app_g);
    lv_group_set_editing(app_g, false);
}

void rebuild_filtered_indices()
{
    s_runtime.filtered_indices.clear();
    for (std::size_t i = 0; i < s_runtime.packages.size(); ++i)
    {
        if (matches_filter(s_runtime.packages[i], s_runtime.selected_filter))
        {
            s_runtime.filtered_indices.push_back(i);
        }
    }
}

std::size_t find_package_index(const std::string& id)
{
    if (id.empty())
    {
        return kInvalidIndex;
    }

    for (std::size_t i = 0; i < s_runtime.packages.size(); ++i)
    {
        if (s_runtime.packages[i].id == id)
        {
            return i;
        }
    }
    return kInvalidIndex;
}

bool load_catalog(std::string& out_error)
{
    out_error.clear();
    s_runtime.packages.clear();

    return packs::fetch_catalog(s_runtime.packages, out_error);
}

void show_list_view();
void refresh_catalog_and_render();

void on_filter_clicked(lv_event_t* event)
{
    const uintptr_t raw_filter = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    s_runtime.selected_filter = static_cast<PackageFilter>(raw_filter);
    refresh_filter_button_states();
    show_list_view();
}

void on_package_clicked(lv_event_t* event)
{
    const uintptr_t raw_index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    if (raw_index == 0U)
    {
        return;
    }

    const std::size_t package_index = static_cast<std::size_t>(raw_index - 1U);
    if (package_index >= s_runtime.packages.size())
    {
        return;
    }

    s_runtime.selected_package_id = s_runtime.packages[package_index].id;
    s_runtime.view = MainView::Detail;
    refresh_catalog_and_render();
}

void on_detail_back_clicked(lv_event_t* event)
{
    (void)event;
    show_list_view();
}

void on_primary_action_clicked(lv_event_t* event)
{
    const uintptr_t raw_index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    if (raw_index == 0U)
    {
        return;
    }

    const std::size_t package_index = static_cast<std::size_t>(raw_index - 1U);
    if (package_index >= s_runtime.packages.size())
    {
        return;
    }

    const packs::PackageRecord& package = s_runtime.packages[package_index];
    const std::string package_id = package.id;
    std::string error;
    const char* installing_text = ::ui::i18n::tr("Installing package...");
    ScopedBusyOverlay overlay(installing_text, package_id.c_str());
    set_status_text(installing_text);
    if (!packs::install_package(package, error))
    {
        set_status_text(error);
        ::ui::SystemNotification::show(error.c_str(), 3000);
        return;
    }

    ::ui::SystemNotification::show(::ui::i18n::tr("Package installed"), 2000);
    s_runtime.selected_package_id = package_id;
    s_runtime.view = MainView::Detail;
    refresh_catalog_and_render();
}

void on_uninstall_clicked(lv_event_t* event)
{
    const uintptr_t raw_index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    if (raw_index == 0U)
    {
        return;
    }

    const std::size_t package_index = static_cast<std::size_t>(raw_index - 1U);
    if (package_index >= s_runtime.packages.size())
    {
        return;
    }

    const packs::PackageRecord& package = s_runtime.packages[package_index];
    std::string error;
    set_status_text(::ui::i18n::tr("Uninstalling package..."));
    if (!packs::uninstall_package(package, error))
    {
        set_status_text(error);
        ::ui::SystemNotification::show(error.c_str(), 3000);
        return;
    }

    ::ui::SystemNotification::show(::ui::i18n::tr("Package uninstalled"), 2000);
    s_runtime.selected_package_id = package.id;
    s_runtime.view = MainView::Detail;
    refresh_catalog_and_render();
}

lv_obj_t* create_action_button(lv_obj_t* parent,
                               const char* label,
                               lv_event_cb_t handler,
                               void* user_data,
                               bool enabled)
{
    lv_obj_t* button = lv_btn_create(parent);
    lv_obj_set_width(button, LV_PCT(100));
    lv_obj_set_height(button, ::ui::page_profile::resolve_control_button_height());
    directory_browser_layout::make_non_scrollable(button);
    two_pane_styles::apply_btn_basic(button);
    lv_obj_add_event_cb(button, handler, LV_EVENT_CLICKED, user_data);
    lv_obj_add_event_cb(button, on_focus_scroll, LV_EVENT_FOCUSED, nullptr);
    if (!enabled)
    {
        lv_obj_add_state(button, LV_STATE_DISABLED);
    }

    lv_obj_t* text = lv_label_create(button);
    ::ui::i18n::set_label_text(text, label ? label : "");
    lv_obj_center(text);
    return button;
}

void show_message_body(const char* text)
{
    clear_body();
    set_header_title(nullptr);

    lv_obj_t* card = lv_obj_create(s_runtime.body_panel);
    style_detail_card(card);

    lv_obj_t* label = lv_label_create(card);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, ::ui::theme::text_muted(), 0);
    ::ui::i18n::set_content_label_text_raw(label, text ? text : "");
}

void create_package_list_item(const packs::PackageRecord& package, std::size_t package_index)
{
    const auto& profile = ::ui::page_profile::current();

    lv_obj_t* item = lv_btn_create(s_runtime.body_panel);
    lv_obj_set_width(item, LV_PCT(100));
    lv_obj_add_event_cb(item,
                        on_package_clicked,
                        LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(package_index + 1U)));
    lv_obj_add_event_cb(item, on_focus_scroll, LV_EVENT_FOCUSED, nullptr);
    apply_list_item_style(item);

    if (info_card::use_tdeck_layout())
    {
        info_card::configure_item(item, profile.list_item_height);
        info_card::ContentOptions options{};
        options.header_meta = true;
        options.body_meta = true;
        const auto slots = info_card::create_content(item, options);

        ::ui::i18n::set_content_label_text_raw(slots.header_main_label, package.display_name.c_str());
        two_pane_styles::apply_label_primary(slots.header_main_label);

        ::ui::i18n::set_content_label_text_raw(slots.header_meta_label, state_badge_text(package).c_str());
        two_pane_styles::apply_label_primary(slots.header_meta_label);

        ::ui::i18n::set_content_label_text_raw(slots.body_main_label, package.summary.c_str());
        two_pane_styles::apply_label_muted(slots.body_main_label);

        const std::string detail = package_counts_text(package);
        ::ui::i18n::set_content_label_text_raw(slots.body_meta_label, detail.c_str());
        lv_obj_set_style_text_color(slots.body_meta_label, state_color(package), 0);
    }
    else
    {
        lv_obj_set_height(item, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(item, 8, 0);
        lv_obj_set_style_pad_row(item, 4, 0);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

        create_wrapped_label(item,
                             package.display_name,
                             ::ui::theme::text(),
                             true);
        create_wrapped_label(item, state_badge_text(package), state_color(package));
        create_wrapped_label(item,
                             package.summary,
                             ::ui::theme::text_muted());

        const std::string detail = package_counts_text(package);
        create_wrapped_label(item,
                             detail,
                             (!package.compatible_firmware || !package.compatible_memory_profile)
                                 ? ::ui::theme::error()
                                 : ::ui::theme::text_muted());
    }

    s_runtime.list_buttons.push_back(item);
}

void render_list_view()
{
    clear_body();
    set_header_title(nullptr);

    if (s_runtime.filtered_indices.empty())
    {
        const char* empty_text = nullptr;
        switch (s_runtime.selected_filter)
        {
        case PackageFilter::All:
            empty_text = ::ui::i18n::tr("No packages available");
            break;
        case PackageFilter::Language:
            empty_text = ::ui::i18n::tr("No language packs available");
            break;
        case PackageFilter::Theme:
            empty_text = ::ui::i18n::tr("No theme packs available");
            break;
        case PackageFilter::Presentation:
            empty_text = ::ui::i18n::tr("No presentation packs available");
            break;
        }
        set_status_text(empty_text);
        show_message_body(empty_text);
        sync_focus_group(s_runtime.filter_all_btn);
        return;
    }

    set_status_text(::ui::i18n::format("Available %s packages: %lu",
                                       filter_label(s_runtime.selected_filter),
                                       static_cast<unsigned long>(s_runtime.filtered_indices.size())));

    switch (s_runtime.selected_filter)
    {
    case PackageFilter::All:
        create_section_card(s_runtime.body_panel,
                            "Current Theme",
                            active_theme_source_text(),
                            ::ui::theme::text());
        create_section_card(s_runtime.body_panel,
                            "Current Presentation",
                            active_presentation_source_text(),
                            ::ui::theme::text());
        break;
    case PackageFilter::Theme:
        create_section_card(s_runtime.body_panel,
                            "Current Theme",
                            active_theme_source_text(),
                            ::ui::theme::text());
        break;
    case PackageFilter::Presentation:
        create_section_card(s_runtime.body_panel,
                            "Current Presentation",
                            active_presentation_source_text(),
                            ::ui::theme::text());
        break;
    case PackageFilter::Language:
        break;
    }

    for (std::size_t index : s_runtime.filtered_indices)
    {
        create_package_list_item(s_runtime.packages[index], index);
    }

    sync_focus_group(!s_runtime.list_buttons.empty() ? s_runtime.list_buttons.front() : s_runtime.filter_all_btn);
}

void render_detail_view(std::size_t package_index)
{
    if (package_index >= s_runtime.packages.size())
    {
        show_list_view();
        return;
    }

    const packs::PackageRecord& package = s_runtime.packages[package_index];
    clear_body();
    set_header_title(package_type_label(package));
    set_status_text(state_text(package));

    lv_obj_t* hero = lv_obj_create(s_runtime.body_panel);
    style_detail_card(hero);
    create_wrapped_label(hero,
                         ::ui::i18n::format("%s  %s",
                                            package.display_name.c_str(),
                                            package.version.c_str()),
                         ::ui::theme::text(),
                         true);
    create_wrapped_label(hero, state_text(package), state_color(package));
    create_wrapped_label(hero, package.summary, ::ui::theme::text_muted());

    lv_obj_t* actions = lv_obj_create(s_runtime.body_panel);
    style_detail_card(actions);
    s_runtime.detail_back_btn =
        create_action_button(actions, "Back to list", on_detail_back_clicked, nullptr, true);

    if (can_install_or_update(package) || !package.compatible_firmware ||
        !package.compatible_memory_profile)
    {
        s_runtime.primary_action_btn =
            create_action_button(actions,
                                 primary_action_label(package),
                                 on_primary_action_clicked,
                                 reinterpret_cast<void*>(static_cast<uintptr_t>(package_index + 1U)),
                                 can_install_or_update(package));
    }

    if (package.installed)
    {
        s_runtime.uninstall_btn =
            create_action_button(actions,
                                 "Uninstall",
                                 on_uninstall_clicked,
                                 reinterpret_cast<void*>(static_cast<uintptr_t>(package_index + 1U)),
                                 true);
    }

    if (!package.description.empty())
    {
        create_section_card(s_runtime.body_panel,
                            "About",
                            package.description,
                            ::ui::theme::text_muted());
    }

    std::string details = ::ui::i18n::format("Type: %s\nArchive: %s\nExtra font RAM: %s\nProfiles: %s\n%s",
                                             package_type_label(package),
                                             format_size(package.archive_size_bytes).c_str(),
                                             format_size(package.estimated_unique_font_ram_bytes).c_str(),
                                             join_values(package.supported_memory_profiles).c_str(),
                                             compatibility_text(package).c_str());
    create_section_card(s_runtime.body_panel,
                        "Package Info",
                        details,
                        package.compatible_firmware && package.compatible_memory_profile
                            ? ::ui::theme::text_muted()
                            : ::ui::theme::error());

    std::string contents = ::ui::i18n::format("Locales: %s\nFonts: %s\nIME: %s\nThemes: %s\nPresentations: %s",
                                              join_values(package.provided_locale_ids).c_str(),
                                              join_values(package.provided_font_ids).c_str(),
                                              join_values(package.provided_ime_ids).c_str(),
                                              join_values(package.provided_theme_ids).c_str(),
                                              join_values(package.provided_presentation_ids).c_str());
    create_section_card(s_runtime.body_panel, "Contents", contents);

    const std::string activation = package_activation_text(package);
    if (!activation.empty())
    {
        create_section_card(s_runtime.body_panel,
                            "Activation",
                            activation,
                            ::ui::theme::accent_strong());
    }

    std::string source = ::ui::i18n::format("Author: %s\nHomepage: %s",
                                            package.author.empty() ? ::ui::i18n::tr("Unknown")
                                                                   : package.author.c_str(),
                                            package.homepage.empty() ? "-" : package.homepage.c_str());
    create_section_card(s_runtime.body_panel, "Source", source);

    lv_obj_t* preferred = s_runtime.primary_action_btn ? s_runtime.primary_action_btn
                                                       : (s_runtime.uninstall_btn ? s_runtime.uninstall_btn
                                                                                  : s_runtime.detail_back_btn);
    sync_focus_group(preferred);
}

void render_current_view()
{
    update_top_bar_status();
    rebuild_filtered_indices();

    if (s_runtime.filtered_indices.empty())
    {
        s_runtime.view = MainView::List;
        render_list_view();
        return;
    }

    if (s_runtime.view == MainView::Detail)
    {
        const std::size_t selected_index = find_package_index(s_runtime.selected_package_id);
        if (selected_index != kInvalidIndex &&
            matches_filter(s_runtime.packages[selected_index], s_runtime.selected_filter))
        {
            render_detail_view(selected_index);
            return;
        }

        s_runtime.view = MainView::List;
        s_runtime.selected_package_id.clear();
    }

    render_list_view();
}

void refresh_catalog_and_render()
{
    update_top_bar_status();

    std::string error;
    if (!load_catalog(error))
    {
        set_status_text(error);
        show_message_body(error.c_str());
        sync_focus_group(s_runtime.filter_all_btn);
        return;
    }

    render_current_view();
}

void show_list_view()
{
    s_runtime.view = MainView::List;
    render_current_view();
}

void create_filter_panel(lv_obj_t* parent)
{
    const auto& profile = ::ui::page_profile::current();
    directory_browser_layout::SelectorPanelSpec panel_spec{};
    directory_browser_layout::SelectorControlsSpec controls_spec{};
    directory_browser_layout::SelectorButtonSpec button_spec{};
    panel_spec.width = profile.filter_panel_width;
    panel_spec.pad_row = profile.filter_panel_pad_row;
    panel_spec.margin_right = 0;

    s_runtime.filter_panel = directory_browser_layout::create_selector_panel(parent, panel_spec);
    two_pane_styles::apply_panel_side(s_runtime.filter_panel);
    controls_spec.pad_row = profile.filter_panel_pad_row;
    controls_spec.pad_column = profile.filter_panel_pad_row;
    lv_obj_t* controls =
        directory_browser_layout::create_selector_controls(s_runtime.filter_panel, controls_spec);
    button_spec.height = profile.filter_button_height;
    button_spec.stacked_min_width = std::max<lv_coord_t>(profile.filter_panel_width, 92);

    const auto create_filter_button = [&](PackageFilter filter, lv_obj_t*& out_button)
    {
        out_button = lv_btn_create(controls);
        lv_obj_add_flag(out_button, LV_OBJ_FLAG_CHECKABLE);
        directory_browser_layout::configure_selector_button(out_button, button_spec);
        two_pane_styles::apply_btn_filter(out_button);
        lv_obj_add_event_cb(out_button,
                            on_filter_clicked,
                            LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(static_cast<uintptr_t>(filter)));
        lv_obj_add_event_cb(out_button, on_focus_scroll, LV_EVENT_FOCUSED, nullptr);

        lv_obj_t* label = lv_label_create(out_button);
        ::ui::i18n::set_label_text(label, filter_label(filter));
        two_pane_styles::apply_label_primary(label);
        lv_obj_center(label);
    };

    create_filter_button(PackageFilter::All, s_runtime.filter_all_btn);
    create_filter_button(PackageFilter::Language, s_runtime.filter_language_btn);
    create_filter_button(PackageFilter::Theme, s_runtime.filter_theme_btn);
    create_filter_button(PackageFilter::Presentation, s_runtime.filter_presentation_btn);
    refresh_filter_button_states();
}

void create_main_panel(lv_obj_t* parent)
{
    const auto& profile = ::ui::page_profile::current();

    directory_browser_layout::ContentPanelSpec panel_spec{};
    panel_spec.pad_all = 0;
    panel_spec.pad_row = profile.list_panel_pad_row;
    panel_spec.pad_left = profile.list_panel_pad_left;
    panel_spec.pad_right = profile.list_panel_pad_right;
    panel_spec.pad_bottom = profile.list_panel_margin_bottom;
    panel_spec.scrollbar_mode = LV_SCROLLBAR_MODE_OFF;

    s_runtime.main_panel = directory_browser_layout::create_content_panel(parent, panel_spec);
    two_pane_styles::apply_panel_main(s_runtime.main_panel);

    s_runtime.header_card = lv_obj_create(s_runtime.main_panel);
    style_detail_card(s_runtime.header_card);
    lv_obj_set_style_pad_all(s_runtime.header_card, 6, 0);
    lv_obj_set_style_pad_row(s_runtime.header_card, 2, 0);

    s_runtime.title_label = lv_label_create(s_runtime.header_card);
    lv_obj_set_width(s_runtime.title_label, LV_PCT(100));
    lv_label_set_long_mode(s_runtime.title_label, LV_LABEL_LONG_WRAP);
    two_pane_styles::apply_label_muted(s_runtime.title_label);
    lv_obj_set_style_text_font(s_runtime.title_label, &lv_font_montserrat_14, 0);
    lv_obj_add_flag(s_runtime.title_label, LV_OBJ_FLAG_HIDDEN);

    s_runtime.status_label = lv_label_create(s_runtime.header_card);
    lv_obj_set_width(s_runtime.status_label, LV_PCT(100));
    lv_label_set_long_mode(s_runtime.status_label, LV_LABEL_LONG_WRAP);
    two_pane_styles::apply_label_muted(s_runtime.status_label);
    ::ui::i18n::set_label_text(s_runtime.status_label, "Loading...");

    s_runtime.body_panel = lv_obj_create(s_runtime.main_panel);
    lv_obj_set_width(s_runtime.body_panel, LV_PCT(100));
    lv_obj_set_height(s_runtime.body_panel, 0);
    lv_obj_set_flex_grow(s_runtime.body_panel, 1);
    lv_obj_set_style_bg_opa(s_runtime.body_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_runtime.body_panel, 0, 0);
    lv_obj_set_style_pad_all(s_runtime.body_panel, 0, 0);
    lv_obj_set_style_pad_row(s_runtime.body_panel, profile.list_panel_pad_row, 0);
    lv_obj_set_flex_flow(s_runtime.body_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(s_runtime.body_panel, LV_SCROLLBAR_MODE_AUTO);
}

} // namespace

namespace extensions::ui::runtime
{

bool is_available()
{
    return true;
}

void enter(const shell::Host* host, lv_obj_t* parent)
{
    s_runtime = RuntimeState{};
    s_runtime.host = host;
    s_runtime.previous_group = lv_group_get_default();
    set_default_group(nullptr);

    const auto& profile = ::ui::page_profile::current();
    directory_browser_layout::RootSpec root_spec{};
    root_spec.pad_row = profile.top_content_gap;
    s_runtime.root = directory_browser_layout::create_root(parent, root_spec);
    apply_root_style(s_runtime.root);

    ::ui::widgets::top_bar_init(s_runtime.top_bar, s_runtime.root);
    ::ui::widgets::top_bar_set_title(s_runtime.top_bar, ::ui::i18n::tr("Extensions"));
    ::ui::widgets::top_bar_set_back_callback(s_runtime.top_bar, on_back, nullptr);

    directory_browser_layout::BodySpec content_spec{};
    content_spec.pad_left = profile.content_pad_left;
    content_spec.pad_right = profile.content_pad_right;
    content_spec.pad_top = profile.content_pad_top;
    content_spec.pad_bottom = profile.content_pad_bottom;
    s_runtime.content = directory_browser_layout::create_body(s_runtime.root, content_spec);

    create_filter_panel(s_runtime.content);
    create_main_panel(s_runtime.content);
    refresh_catalog_and_render();

    if (app_g == nullptr && s_runtime.previous_group != nullptr)
    {
        set_default_group(s_runtime.previous_group);
    }
}

void exit(lv_obj_t* parent)
{
    (void)parent;
    if (s_runtime.root != nullptr)
    {
        lv_obj_del(s_runtime.root);
    }
    if (s_runtime.previous_group != nullptr)
    {
        set_default_group(s_runtime.previous_group);
    }
    s_runtime = RuntimeState{};
}

} // namespace extensions::ui::runtime
