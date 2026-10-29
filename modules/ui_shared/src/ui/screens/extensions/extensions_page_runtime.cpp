#include "ui/screens/extensions/extensions_page_runtime.h"
#include "platform/ui/tmap_creation_runtime.h"
#include "ui/screens/extensions/tmap_creator_page.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "platform/ui/device_runtime.h"
#include "platform/ui/network_runtime.h"
#include "platform/ui/pack_repository_runtime.h"
#include "platform/ui/wifi_runtime.h"
#include "ui/app_runtime.h"
#include "ui/components/bottom_bar.h"
#include "ui/components/info_card.h"
#include "ui/components/two_pane_layout.h"
#include "ui/components/two_pane_styles.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/runtime/ui_feedback.h"
#include "ui/ui_theme.h"
#include "ui/widgets/foreground_operation_overlay.h"
#include "ui/widgets/top_bar.h"

#if defined(ESP_PLATFORM)
#include "esp_log.h"
#endif

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
namespace two_pane_layout = ::ui::components::two_pane_layout;
namespace two_pane_styles = ::ui::components::two_pane_styles;
namespace packs = ::ui::runtime::packs;

constexpr std::size_t kInvalidIndex = static_cast<std::size_t>(-1);

#if defined(ESP_PLATFORM)
constexpr char kLogTag[] = "extensions";
#endif

enum class MainView : uint8_t
{
    List = 0,
    Detail,
    Creator,
};

enum class PackageFilter : uint8_t
{
    Installed = 0,
    Uninstalled,
};

struct RuntimeState
{
    const Host* host = nullptr;
    lv_group_t* previous_group = nullptr;
    lv_obj_t* root = nullptr;
    lv_obj_t* content = nullptr;
    lv_obj_t* filter_panel = nullptr;
    ::ui::components::bottom_bar::Menu menu{};
    lv_obj_t* installed_filter_btn = nullptr;
    lv_obj_t* uninstalled_filter_btn = nullptr;
    lv_obj_t* creator_btn = nullptr;
    lv_obj_t* main_panel = nullptr;
    lv_obj_t* header_card = nullptr;
    lv_obj_t* title_label = nullptr;
    lv_obj_t* status_label = nullptr;
    lv_obj_t* body_host = nullptr;
    lv_obj_t* body_panel = nullptr;
    lv_obj_t* visible_body_panel = nullptr;
    lv_obj_t* detail_back_btn = nullptr;
    lv_obj_t* primary_action_btn = nullptr;
    lv_obj_t* uninstall_btn = nullptr;
    lv_obj_t* connect_btn = nullptr;
    lv_timer_t* install_timer = nullptr;
    ::ui::widgets::TopBar top_bar;
    MainView view = MainView::List;
    PackageFilter filter = PackageFilter::Installed;
    bool remote_catalog_loaded = false;
    bool install_was_busy = false;
    std::uint32_t catalog_revision = 0;
    std::string catalog_error;
    std::string selected_package_id;
    std::vector<packs::PackageRecord> packages;
    std::vector<std::size_t> filtered_indices;
    std::vector<lv_obj_t*> list_buttons;
};

RuntimeState s_runtime{};

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

void show_list_view();

bool is_back_key(uint32_t key)
{
    return key == LV_KEY_ESC || key == LV_KEY_BACKSPACE;
}

void on_key(lv_event_t* event)
{
    if (lv_event_get_code(event) != LV_EVENT_KEY)
    {
        return;
    }
    const uint32_t key = lv_event_get_key(event);
    if ((key == 't' || key == 'T') && s_runtime.creator_btn &&
        ::platform::ui::tmap_creation::supported() && s_runtime.view != MainView::Creator)
    {
        lv_event_stop_processing(event);
        lv_event_stop_bubbling(event);
        lv_obj_send_event(s_runtime.creator_btn, LV_EVENT_CLICKED, nullptr);
        return;
    }
    if (key == 'm' || key == 'M')
    {
        lv_event_stop_processing(event);
        lv_event_stop_bubbling(event);
        s_runtime.menu.toggle();
        return;
    }
    if (!is_back_key(key))
    {
        return;
    }
    if (s_runtime.view == MainView::Detail)
    {
        show_list_view();
    }
    else
    {
        request_exit();
    }
    lv_event_stop_processing(event);
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

void add_focusable(lv_obj_t* obj)
{
    if (obj == nullptr || app_g == nullptr)
    {
        return;
    }
    lv_group_add_obj(app_g, obj);
    lv_obj_remove_event_cb(obj, on_key);
    lv_obj_add_event_cb(obj, on_key, LV_EVENT_KEY, nullptr);
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

bool is_extension_package(const packs::PackageRecord& package)
{
    return package.package_type.empty() ||
           package.package_type == "installed" ||
           package.package_type == "locale-bundle" ||
           package.package_type == "content-bundle" ||
           package.package_type == "input-bundle";
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
    return lv_color_hex(two_pane_styles::kAccent);
}

std::string package_counts_text(const packs::PackageRecord& package)
{
    return ::ui::i18n::format("%lu locales  %lu fonts  %lu IME  %s",
                              static_cast<unsigned long>(package.provided_locale_ids.size()),
                              static_cast<unsigned long>(package.provided_font_ids.size()),
                              static_cast<unsigned long>(package.provided_ime_ids.size()),
                              format_size(package.archive_size_bytes).c_str());
}

void update_top_bar_status()
{
    const auto network = platform::ui::network::status();
    if (!network.supported)
    {
        ::ui::widgets::top_bar_set_right_text(s_runtime.top_bar, ::ui::i18n::tr("No network"));
        return;
    }
    if (!network.connected)
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

void apply_root_style(lv_obj_t* obj)
{
    if (!obj)
    {
        return;
    }
    lv_obj_set_style_bg_color(obj, lv_color_hex(two_pane_styles::kSidePanelBg), 0);
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
    lv_obj_set_style_bg_color(card, lv_color_hex(two_pane_styles::kSidePanelBg), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(two_pane_styles::kBorder), 0);
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
                              lv_color_t body_color = lv_color_hex(two_pane_styles::kTextMuted))
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

lv_obj_t* create_staged_body_panel(lv_obj_t* parent)
{
    if (parent == nullptr)
    {
        return nullptr;
    }

    const auto& profile = ::ui::page_profile::current();
    lv_obj_t* panel = lv_obj_create(parent);
    if (panel == nullptr)
    {
        return nullptr;
    }
    lv_obj_set_width(panel, LV_PCT(100));
    lv_obj_set_height(panel, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, profile.list_panel_pad_row, LV_PART_MAIN);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

void commit_body_transaction()
{
    if (s_runtime.body_panel == nullptr ||
        s_runtime.body_panel == s_runtime.visible_body_panel)
    {
        return;
    }

    lv_obj_clear_flag(s_runtime.body_panel, LV_OBJ_FLAG_HIDDEN);
    if (s_runtime.visible_body_panel != nullptr)
    {
        lv_obj_del(s_runtime.visible_body_panel);
    }
    s_runtime.visible_body_panel = s_runtime.body_panel;
#if defined(ESP_PLATFORM)
    ESP_LOGI(kLogTag, "[UI][Txn] extensions kind=structure visible_clean=0 staging_commit=1");
#endif
}

void clear_body()
{
    s_runtime.detail_back_btn = nullptr;
    s_runtime.primary_action_btn = nullptr;
    s_runtime.uninstall_btn = nullptr;
    s_runtime.connect_btn = nullptr;
    s_runtime.list_buttons.clear();

    if (s_runtime.body_panel != nullptr &&
        s_runtime.body_panel != s_runtime.visible_body_panel)
    {
        // A nested render replaces only the not-yet-visible staging tree.
        // Keep the previous complete view alive until its successor is ready.
        lv_obj_del(s_runtime.body_panel);
        s_runtime.body_panel = s_runtime.visible_body_panel;
    }

    lv_obj_t* staged_panel = create_staged_body_panel(s_runtime.body_host);
    if (staged_panel == nullptr)
    {
#if defined(ESP_PLATFORM)
        ESP_LOGE(kLogTag, "[UI][Txn] extensions kind=structure staging_begin_failed");
#endif
        return;
    }
    lv_obj_add_flag(staged_panel, LV_OBJ_FLAG_HIDDEN);
    s_runtime.body_panel = staged_panel;
}

void sync_focus_group(lv_obj_t* preferred_focus = nullptr)
{
    commit_body_transaction();
    if (app_g == nullptr)
    {
        return;
    }

    lv_group_remove_all_objs(app_g);

    if (s_runtime.menu.is_open())
    {
        add_focusable(s_runtime.installed_filter_btn);
        add_focusable(s_runtime.uninstalled_filter_btn);
        add_focusable(s_runtime.creator_btn);
        lv_group_focus_obj(s_runtime.filter == PackageFilter::Installed ? s_runtime.installed_filter_btn : s_runtime.uninstalled_filter_btn);
        set_default_group(app_g);
        return;
    }

    if (s_runtime.top_bar.back_btn != nullptr)
    {
        add_focusable(s_runtime.top_bar.back_btn);
    }
    add_focusable(s_runtime.menu.button());
    add_focusable(s_runtime.creator_btn);
    if (s_runtime.detail_back_btn != nullptr)
    {
        add_focusable(s_runtime.detail_back_btn);
    }
    for (lv_obj_t* btn : s_runtime.list_buttons)
    {
        if (btn != nullptr)
        {
            add_focusable(btn);
        }
    }
    if (s_runtime.primary_action_btn != nullptr)
    {
        add_focusable(s_runtime.primary_action_btn);
    }
    if (s_runtime.uninstall_btn != nullptr)
    {
        add_focusable(s_runtime.uninstall_btn);
    }
    if (s_runtime.connect_btn != nullptr)
    {
        add_focusable(s_runtime.connect_btn);
    }

    if (preferred_focus != nullptr && lv_obj_is_valid(preferred_focus) &&
        preferred_focus != s_runtime.installed_filter_btn && preferred_focus != s_runtime.uninstalled_filter_btn)
    {
        lv_group_focus_obj(preferred_focus);
    }
    else if (!s_runtime.list_buttons.empty())
    {
        lv_group_focus_obj(s_runtime.list_buttons.front());
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
        const packs::PackageRecord& package = s_runtime.packages[i];
        if (!is_extension_package(package))
        {
            continue;
        }

        const bool include =
            s_runtime.filter == PackageFilter::Installed ? package.installed
                                                         : !package.installed;
        if (include)
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

packs::PackageRecord make_offline_installed_package(
    const packs::InstalledPackageRecord& installed)
{
    packs::PackageRecord package{};
    package.id = installed.id;
    package.package_type = "installed";
    package.version = installed.version;
    package.display_name = installed.id.empty() ? ::ui::i18n::tr("Installed package")
                                                : installed.id;
    package.summary = ::ui::i18n::tr("Installed locally");
    package.description =
        ::ui::i18n::tr("Remote catalog is unavailable. Connect Wi-Fi to view package details and updates.");
    package.installed = true;
    package.installed_record = installed;
    package.archive_sha256 = installed.archive_sha256;
    package.compatible_firmware = true;
    package.compatible_memory_profile = true;
    return package;
}

bool load_catalog(std::string& out_error)
{
    out_error.clear();
    s_runtime.packages.clear();
    s_runtime.remote_catalog_loaded = false;
    s_runtime.catalog_error.clear();

    std::vector<packs::InstalledPackageRecord> installed;
    std::string installed_error;
    const bool installed_loaded = packs::load_installed_packages(installed, installed_error);

    std::vector<packs::PackageRecord> remote_packages;
    std::string remote_error;
    if (packs::fetch_catalog(remote_packages, remote_error))
    {
        s_runtime.packages = std::move(remote_packages);
        s_runtime.remote_catalog_loaded = true;
        return true;
    }

    s_runtime.catalog_error = remote_error;
    if (!installed_loaded)
    {
        out_error = installed_error.empty() ? remote_error : installed_error;
        return false;
    }

    s_runtime.packages.reserve(installed.size());
    for (const auto& record : installed)
    {
        if (!record.id.empty())
        {
            s_runtime.packages.push_back(make_offline_installed_package(record));
        }
    }
    std::sort(s_runtime.packages.begin(),
              s_runtime.packages.end(),
              [](const packs::PackageRecord& lhs, const packs::PackageRecord& rhs)
              {
                  return lhs.display_name < rhs.display_name;
              });
    return true;
}

void show_list_view();
void render_current_view();
void refresh_catalog_and_render();

void set_install_action_buttons_disabled(bool disabled)
{
    lv_obj_t* buttons[] = {s_runtime.primary_action_btn, s_runtime.uninstall_btn};
    for (lv_obj_t* button : buttons)
    {
        if (button == nullptr)
        {
            continue;
        }
        if (disabled)
        {
            lv_obj_add_state(button, LV_STATE_DISABLED);
        }
        else
        {
            lv_obj_clear_state(button, LV_STATE_DISABLED);
        }
    }
}

void mark_package_installed(const std::string& package_id)
{
    const std::size_t package_index = find_package_index(package_id);
    if (package_index == kInvalidIndex)
    {
        return;
    }

    packs::PackageRecord& package = s_runtime.packages[package_index];
    package.installed = true;
    package.update_available = false;
    package.installed_record.id = package.id;
    package.installed_record.version = package.version;
    package.installed_record.archive_sha256 = package.archive_sha256;
}

void sync_install_ui(bool notify_completion)
{
    const packs::PackageInstallStatus status = packs::install_status();
    if (status.busy)
    {
        const char* title = status.message.empty()
                                ? ::ui::i18n::tr("Installing package...")
                                : status.message.c_str();
        const char* detail = !status.detail.empty()
                                 ? status.detail.c_str()
                                 : (status.package_id.empty() ? nullptr : status.package_id.c_str());
        namespace foreground = ::ui::widgets::foreground_operation;
        foreground::publish(
            foreground::make_snapshot(foreground::Slot::PackageInstall,
                                      foreground::Policy::Overlay,
                                      foreground::Priority::Foreground,
                                      title,
                                      detail,
                                      status.progress_percent));
        set_status_text(title);
        set_install_action_buttons_disabled(true);
    }
    else
    {
        ::ui::widgets::foreground_operation::clear(
            ::ui::widgets::foreground_operation::Slot::PackageInstall);
        set_install_action_buttons_disabled(false);
    }

    if (notify_completion && s_runtime.install_was_busy && !status.busy)
    {
        if (status.phase == packs::PackageInstallPhase::Succeeded)
        {
            ::ui::i18n::reload_language();
            ::ui::feedback::show_notice(::ui::i18n::tr("Package installed"), 2000);
            if (!status.package_id.empty())
            {
                mark_package_installed(status.package_id);
                s_runtime.selected_package_id = status.package_id;
            }
            s_runtime.filter = PackageFilter::Installed;
            s_runtime.view = MainView::Detail;
            render_current_view();
#if defined(ESP_PLATFORM)
            ESP_LOGI(kLogTag,
                     "[UI][Txn] extensions install_complete package=%s full_app_rebuild=0",
                     status.package_id.empty() ? "<none>" : status.package_id.c_str());
#endif
        }
        else if (status.phase == packs::PackageInstallPhase::Failed)
        {
            const std::string error = status.message.empty()
                                          ? std::string(::ui::i18n::tr("Install package failed"))
                                          : status.message;
            set_status_text(error);
            ::ui::feedback::show_notice(error.c_str(), 3000);
        }
    }

    s_runtime.install_was_busy = status.busy;
}

void install_timer_cb(lv_timer_t* /*timer*/)
{
    if (s_runtime.view == MainView::Creator || s_runtime.menu.is_open() ||
        (app_g && lv_group_get_default() != app_g)) return;
    auto* selected = app_g ? lv_group_get_focused(app_g) : nullptr;
    const bool keep_selection = selected && (selected == s_runtime.top_bar.back_btn ||
        selected == s_runtime.menu.button() || selected == s_runtime.creator_btn);
    sync_install_ui(true);
    const auto catalog = packs::catalog_status();
    if (catalog.revision != s_runtime.catalog_revision && !packs::install_status().busy)
    {
        s_runtime.catalog_revision = catalog.revision;
        refresh_catalog_and_render();
    }
    if (keep_selection && app_g && lv_obj_is_valid(selected) && lv_group_get_focused(app_g) != selected)
        sync_focus_group(selected);
}

void update_filter_button_states()
{
    auto apply = [](lv_obj_t* button, bool active)
    {
        if (button == nullptr)
        {
            return;
        }
        if (active)
        {
            lv_obj_add_state(button, LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_clear_state(button, LV_STATE_CHECKED);
        }
    };

    apply(s_runtime.installed_filter_btn, s_runtime.filter == PackageFilter::Installed);
    apply(s_runtime.uninstalled_filter_btn, s_runtime.filter == PackageFilter::Uninstalled);
}

void on_filter_clicked(lv_event_t* event)
{
    const uintptr_t raw = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    s_runtime.filter = raw == 1U ? PackageFilter::Uninstalled : PackageFilter::Installed;
    if (s_runtime.filter == PackageFilter::Uninstalled) packs::request_catalog_refresh();
    update_filter_button_states();
    s_runtime.menu.close();
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
    std::string error;
    if (!packs::start_install_package(package, error))
    {
        set_status_text(error);
        ::ui::feedback::show_notice(error.c_str(), 3000);
        sync_install_ui(false);
        return;
    }

    s_runtime.install_was_busy = true;
    sync_install_ui(true);
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
        ::ui::feedback::show_notice(error.c_str(), 3000);
        return;
    }

    ::ui::i18n::reload_language();
    ::ui::feedback::show_notice(::ui::i18n::tr("Package uninstalled"), 2000);
    s_runtime.selected_package_id = package.id;
    s_runtime.view = MainView::Detail;
    refresh_catalog_and_render();
    ui_request_rebuild_active_app();
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
    two_pane_layout::make_non_scrollable(button);
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

bool wifi_has_saved_credentials(platform::ui::wifi::Config& out_config)
{
    if (!platform::ui::wifi::load_config(out_config))
    {
        return false;
    }
    return out_config.ssid[0] != '\0';
}

void on_connect_wifi_clicked(lv_event_t* event)
{
    (void)event;
    if (!platform::ui::wifi::is_supported())
    {
        const char* text = ::ui::i18n::tr("Wi-Fi unsupported");
        set_status_text(text);
        ::ui::feedback::show_notice(text, 3000);
        return;
    }

    platform::ui::wifi::Config config{};
    if (!wifi_has_saved_credentials(config))
    {
        const char* text = ::ui::i18n::tr("Configure Wi-Fi in Settings first");
        set_status_text(text);
        ::ui::feedback::show_notice(text, 3500);
        return;
    }

    config.enabled = true;
    (void)platform::ui::wifi::save_config(config);
    if (!platform::ui::wifi::apply_enabled(true) ||
        !platform::ui::wifi::connect(&config))
    {
        const platform::ui::wifi::Status wifi = platform::ui::wifi::status();
        const char* text = wifi.message[0] ? wifi.message : ::ui::i18n::tr("Wi-Fi connect failed");
        set_status_text(text);
        ::ui::feedback::show_notice(text, 3500);
        update_top_bar_status();
        return;
    }

    const platform::ui::wifi::Status wifi = platform::ui::wifi::status();
    if (wifi.connected)
    {
        packs::request_catalog_refresh();
        ::ui::feedback::show_notice(::ui::i18n::tr("Wi-Fi connected"), 2000);
        refresh_catalog_and_render();
        return;
    }

    const char* text =
        wifi.message[0] ? wifi.message : ::ui::i18n::tr("Wi-Fi connecting");
    set_status_text(text);
    ::ui::feedback::show_notice(text, 2500);
    update_top_bar_status();
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
    lv_obj_set_style_text_color(label, lv_color_hex(two_pane_styles::kTextMuted), 0);
    ::ui::i18n::set_content_label_text_raw(label, text ? text : "");
}

void show_uninstalled_offline_body()
{
    clear_body();
    set_header_title("Uninstalled");

    if (platform::ui::network::status().connected)
    {
        const char* text = s_runtime.catalog_error.empty() ? "Package catalog unavailable" : s_runtime.catalog_error.c_str();
        set_status_text(text);
        show_message_body(text);
        set_header_title("Uninstalled");
        sync_focus_group(s_runtime.uninstalled_filter_btn);
        update_top_bar_status();
        return;
    }

    const platform::ui::wifi::Status wifi = platform::ui::wifi::status();
    platform::ui::wifi::Config saved_wifi{};
    const bool has_saved_credentials = wifi_has_saved_credentials(saved_wifi);
    const char* text = nullptr;
    if (!wifi.supported)
    {
        text = ::ui::i18n::tr("Wi-Fi unsupported on this device");
    }
    else if (!has_saved_credentials)
    {
        text = ::ui::i18n::tr("Configure Wi-Fi in Settings first");
    }
    else
    {
        text = ::ui::i18n::tr("Connect Wi-Fi to browse uninstalled packages");
    }
    set_status_text(text);

    lv_obj_t* card = lv_obj_create(s_runtime.body_panel);
    style_detail_card(card);
    create_wrapped_label(card,
                         s_runtime.catalog_error.empty()
                             ? std::string(text)
                             : s_runtime.catalog_error,
                         lv_color_hex(two_pane_styles::kTextMuted));

    if (wifi.supported)
    {
        s_runtime.connect_btn =
            create_action_button(card, "Connect Wi-Fi", on_connect_wifi_clicked, nullptr, true);
    }
    sync_focus_group(s_runtime.connect_btn ? s_runtime.connect_btn
                                           : s_runtime.uninstalled_filter_btn);
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
                             lv_color_hex(two_pane_styles::kTextPrimary),
                             true);
        create_wrapped_label(item, state_badge_text(package), state_color(package));
        create_wrapped_label(item,
                             package.summary,
                             lv_color_hex(two_pane_styles::kTextMuted));

        const std::string detail = package_counts_text(package);
        create_wrapped_label(item,
                             detail,
                             (!package.compatible_firmware || !package.compatible_memory_profile)
                                 ? ::ui::theme::error()
                                 : lv_color_hex(two_pane_styles::kTextMuted));
    }

    s_runtime.list_buttons.push_back(item);
}

void render_list_view()
{
    clear_body();
    set_header_title(nullptr);

    if (s_runtime.filtered_indices.empty())
    {
        if (s_runtime.filter == PackageFilter::Uninstalled && !s_runtime.remote_catalog_loaded)
        {
            show_uninstalled_offline_body();
            return;
        }

        const char* empty_text =
            s_runtime.filter == PackageFilter::Installed
                ? ::ui::i18n::tr("No installed packages")
                : ::ui::i18n::tr("No uninstalled packages");
        set_status_text(empty_text);
        show_message_body(empty_text);
        sync_focus_group(s_runtime.filter == PackageFilter::Installed
                             ? s_runtime.installed_filter_btn
                             : s_runtime.uninstalled_filter_btn);
        return;
    }

    set_status_text(::ui::i18n::format(
        s_runtime.filter == PackageFilter::Installed ? "Installed packages: %lu"
                                                     : "Uninstalled packages: %lu",
        static_cast<unsigned long>(s_runtime.filtered_indices.size())));

    for (std::size_t index : s_runtime.filtered_indices)
    {
        create_package_list_item(s_runtime.packages[index], index);
    }

    sync_focus_group(!s_runtime.list_buttons.empty()
                         ? s_runtime.list_buttons.front()
                         : (s_runtime.filter == PackageFilter::Installed
                                ? s_runtime.installed_filter_btn
                                : s_runtime.uninstalled_filter_btn));
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
    set_header_title("Package Details");
    set_status_text(state_text(package));

    lv_obj_t* hero = lv_obj_create(s_runtime.body_panel);
    style_detail_card(hero);
    create_wrapped_label(hero,
                         ::ui::i18n::format("%s  %s",
                                            package.display_name.c_str(),
                                            package.version.c_str()),
                         lv_color_hex(two_pane_styles::kTextPrimary),
                         true);
    create_wrapped_label(hero, state_text(package), state_color(package));
    create_wrapped_label(hero, package.summary, lv_color_hex(two_pane_styles::kTextMuted));

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
                            lv_color_hex(two_pane_styles::kTextMuted));
    }

    std::string details = ::ui::i18n::format("Archive: %s\nExtra font RAM: %s\nProfiles: %s\n%s",
                                             format_size(package.archive_size_bytes).c_str(),
                                             format_size(package.estimated_unique_font_ram_bytes).c_str(),
                                             join_values(package.supported_memory_profiles).c_str(),
                                             compatibility_text(package).c_str());
    create_section_card(s_runtime.body_panel,
                        "Package Info",
                        details,
                        package.compatible_firmware && package.compatible_memory_profile
                            ? lv_color_hex(two_pane_styles::kTextMuted)
                            : ::ui::theme::error());

    std::string contents = ::ui::i18n::format("Locales: %s\nFonts: %s\nIME: %s",
                                              join_values(package.provided_locale_ids).c_str(),
                                              join_values(package.provided_font_ids).c_str(),
                                              join_values(package.provided_ime_ids).c_str());
    create_section_card(s_runtime.body_panel, "Contents", contents);

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
    update_filter_button_states();
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
        if (selected_index != kInvalidIndex && is_extension_package(s_runtime.packages[selected_index]))
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
        sync_focus_group(s_runtime.installed_filter_btn);
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
    two_pane_layout::SidePanelSpec panel_spec{};
    panel_spec.width = profile.filter_panel_width;
    panel_spec.pad_row = profile.filter_panel_pad_row;
    panel_spec.margin_right = 0;

    s_runtime.filter_panel = two_pane_layout::create_side_panel(parent, panel_spec);
    two_pane_styles::apply_panel_side(s_runtime.filter_panel);

    auto create_filter_button = [&](const char* label_text,
                                    PackageFilter filter,
                                    bool active) -> lv_obj_t*
    {
        lv_obj_t* button = lv_btn_create(s_runtime.filter_panel);
        lv_obj_set_size(button, LV_PCT(100), profile.filter_button_height);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CHECKABLE);
        if (active)
        {
            lv_obj_add_state(button, LV_STATE_CHECKED);
        }
        two_pane_layout::make_non_scrollable(button);
        two_pane_styles::apply_btn_filter(button);
        lv_obj_add_event_cb(button,
                            on_filter_clicked,
                            LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(
                                static_cast<uintptr_t>(filter == PackageFilter::Uninstalled ? 1U : 0U)));
        lv_obj_add_event_cb(button, on_focus_scroll, LV_EVENT_FOCUSED, nullptr);

        lv_obj_t* label = lv_label_create(button);
        ::ui::i18n::set_label_text(label, label_text);
        two_pane_styles::apply_label_primary(label);
        lv_obj_center(label);
        return button;
    };

    s_runtime.installed_filter_btn =
        create_filter_button("Installed", PackageFilter::Installed, true);
    s_runtime.uninstalled_filter_btn =
        create_filter_button("Uninstalled", PackageFilter::Uninstalled, false);
}

void create_main_panel(lv_obj_t* parent)
{
    const auto& profile = ::ui::page_profile::current();

    two_pane_layout::MainPanelSpec panel_spec{};
    panel_spec.pad_all = 0;
    panel_spec.pad_row = profile.list_panel_pad_row;
    panel_spec.pad_left = profile.list_panel_pad_left;
    panel_spec.pad_right = profile.list_panel_pad_right;
    panel_spec.pad_bottom = profile.list_panel_margin_bottom;
    panel_spec.scrollbar_mode = LV_SCROLLBAR_MODE_OFF;

    s_runtime.main_panel = two_pane_layout::create_main_panel(parent, panel_spec);
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

    s_runtime.body_host = lv_obj_create(s_runtime.main_panel);
    lv_obj_set_width(s_runtime.body_host, LV_PCT(100));
    lv_obj_set_height(s_runtime.body_host, 0);
    lv_obj_set_flex_grow(s_runtime.body_host, 1);
    lv_obj_set_style_bg_opa(s_runtime.body_host, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_runtime.body_host, 0, 0);
    lv_obj_set_style_pad_all(s_runtime.body_host, 0, 0);
    lv_obj_set_scrollbar_mode(s_runtime.body_host, LV_SCROLLBAR_MODE_AUTO);

    s_runtime.body_panel = create_staged_body_panel(s_runtime.body_host);
    s_runtime.visible_body_panel = s_runtime.body_panel;
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
    two_pane_layout::RootSpec root_spec{};
    root_spec.pad_row = profile.top_content_gap;
    s_runtime.root = two_pane_layout::create_root(parent, root_spec);
    apply_root_style(s_runtime.root);

    ::ui::widgets::TopBarConfig top_bar_config{};
    top_bar_config.power_indicator = false;
    ::ui::widgets::top_bar_init(s_runtime.top_bar, s_runtime.root, top_bar_config);
    ::ui::widgets::top_bar_set_title(s_runtime.top_bar, ::ui::i18n::tr("Extensions"));
    ::ui::widgets::top_bar_set_back_callback(s_runtime.top_bar, on_back, nullptr);

    two_pane_layout::ContentSpec content_spec{};
    content_spec.pad_left = profile.content_pad_left;
    content_spec.pad_right = profile.content_pad_right;
    content_spec.pad_top = profile.content_pad_top;
    content_spec.pad_bottom = profile.content_pad_bottom;
    s_runtime.content = two_pane_layout::create_content_row(s_runtime.root, content_spec);

    create_filter_panel(s_runtime.content);
    create_main_panel(s_runtime.content);
    auto* bar = ::ui::components::bottom_bar::create(s_runtime.root);
    auto* menu_button = ::ui::components::bottom_bar::add_button(bar, "Menu");
    s_runtime.menu.init(s_runtime.root, menu_button, s_runtime.filter_panel,
                        [](void*, bool)
                        { sync_focus_group(); });
    if (::platform::ui::tmap_creation::supported())
    {
        s_runtime.creator_btn = ::ui::components::bottom_bar::add_button(bar, "T Make TMAP");
        lv_obj_add_event_cb(
            s_runtime.creator_btn, [](lv_event_t*)
            {
                s_runtime.menu.close();
                s_runtime.view = MainView::Creator;
                ::extensions::ui::tmap_creator::open(s_runtime.root, [](void*) { show_list_view(); sync_focus_group(s_runtime.creator_btn); }, nullptr);
            }, LV_EVENT_CLICKED, nullptr);
    }
    packs::request_catalog_refresh();
    refresh_catalog_and_render();
    sync_install_ui(false);
    s_runtime.install_timer = lv_timer_create(install_timer_cb, 250, nullptr);
    if (s_runtime.install_timer != nullptr)
    {
        lv_timer_set_repeat_count(s_runtime.install_timer, -1);
    }

    if (app_g == nullptr && s_runtime.previous_group != nullptr)
    {
        set_default_group(s_runtime.previous_group);
    }
}

void exit(lv_obj_t* parent)
{
    (void)parent;
    ::extensions::ui::tmap_creator::shutdown();
    s_runtime.menu.reset();
    if (s_runtime.install_timer != nullptr)
    {
        lv_timer_del(s_runtime.install_timer);
        s_runtime.install_timer = nullptr;
    }
    ::ui::widgets::foreground_operation::clear(
        ::ui::widgets::foreground_operation::Slot::PackageInstall);
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
