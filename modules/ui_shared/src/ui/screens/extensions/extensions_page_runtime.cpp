#include "ui/screens/extensions/extensions_page_runtime.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "platform/ui/device_runtime.h"
#include "platform/ui/wifi_runtime.h"
#include "ui/app_runtime.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/runtime/memory_profile.h"
#include "ui/runtime/pack_repository.h"
#include "ui/ui_theme.h"
#include "ui/widgets/system_notification.h"
#include "ui/widgets/top_bar.h"

#if !defined(LV_FONT_MONTSERRAT_16) || !LV_FONT_MONTSERRAT_16
#define lv_font_montserrat_16 lv_font_montserrat_14
#endif

namespace
{

using Host = extensions::ui::shell::Host;

struct RuntimeState
{
    const Host* host = nullptr;
    lv_obj_t* root = nullptr;
    lv_obj_t* content = nullptr;
    lv_obj_t* toolbar = nullptr;
    lv_obj_t* status_label = nullptr;
    lv_obj_t* list = nullptr;
    lv_obj_t* refresh_btn = nullptr;
    ::ui::widgets::TopBar top_bar;
    std::vector<ui::runtime::packs::PackageRecord> packages;
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

std::string compatibility_text(const ui::runtime::packs::PackageRecord& package)
{
    if (package.compatible_firmware && package.compatible_memory_profile)
    {
        return ::ui::i18n::tr("Compatibility: this device");
    }

    std::string text = ::ui::i18n::tr("Compatibility: ");
    bool appended = false;
    if (!package.compatible_memory_profile)
    {
        text += ::ui::i18n::tr("memory profile mismatch");
        appended = true;
    }
    if (!package.compatible_firmware)
    {
        if (appended)
        {
            text += ", ";
        }
        text += ::ui::i18n::format("needs %s", package.min_firmware_version.c_str());
    }
    return text;
}

std::string state_text(const ui::runtime::packs::PackageRecord& package)
{
    if (package.update_available)
    {
        return ::ui::i18n::format("Update available: %s", package.version.c_str());
    }
    if (package.installed)
    {
        return ::ui::i18n::format("Installed: %s", package.installed_record.version.c_str());
    }
    return ::ui::i18n::tr("Not installed");
}

const char* action_label(const ui::runtime::packs::PackageRecord& package)
{
    if (!package.compatible_firmware || !package.compatible_memory_profile)
    {
        return "Incompatible";
    }
    if (package.update_available)
    {
        return "Update";
    }
    if (package.installed)
    {
        return "Installed";
    }
    return "Install";
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

void set_status_text(const char* text)
{
    if (s_runtime.status_label == nullptr)
    {
        return;
    }
    ::ui::i18n::set_content_label_text_raw(s_runtime.status_label, text ? text : "");
}

void clear_list()
{
    if (s_runtime.list == nullptr)
    {
        return;
    }
    lv_obj_clean(s_runtime.list);
    if (app_g != nullptr && s_runtime.top_bar.back_btn != nullptr)
    {
        lv_group_remove_all_objs(app_g);
        lv_group_add_obj(app_g, s_runtime.top_bar.back_btn);
        if (s_runtime.refresh_btn != nullptr)
        {
            lv_group_add_obj(app_g, s_runtime.refresh_btn);
        }
    }
}

void show_empty_message(const char* text)
{
    clear_list();
    if (s_runtime.list == nullptr)
    {
        return;
    }

    lv_obj_t* label = lv_label_create(s_runtime.list);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, ui::theme::text_muted(), 0);
    ::ui::i18n::set_content_label_text_raw(label, text ? text : "");
}

void rebuild_list();

void on_install_clicked(lv_event_t* event)
{
    const uintptr_t raw_index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    if (raw_index == 0U)
    {
        return;
    }

    const std::size_t index = static_cast<std::size_t>(raw_index - 1U);
    if (index >= s_runtime.packages.size())
    {
        return;
    }

    std::string error;
    set_status_text(::ui::i18n::tr("Installing package..."));
    if (!ui::runtime::packs::install_package(s_runtime.packages[index], error))
    {
        set_status_text(error.c_str());
        ::ui::SystemNotification::show(error.c_str(), 3000);
        return;
    }

    ::ui::SystemNotification::show(::ui::i18n::tr("Package installed"), 2000);
    rebuild_list();
}

void create_package_card(const ui::runtime::packs::PackageRecord& package, std::size_t index)
{
    lv_obj_t* card = lv_obj_create(s_runtime.list);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_style_bg_color(card, ui::theme::surface(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, ui::theme::border(), 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title_row = lv_obj_create(card);
    lv_obj_set_width(title_row, LV_PCT(100));
    lv_obj_set_style_bg_opa(title_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_row, 0, 0);
    lv_obj_set_style_pad_all(title_row, 0, 0);
    lv_obj_set_style_pad_column(title_row, 8, 0);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(title_row);
    lv_obj_set_flex_grow(title, 1);
    lv_obj_set_width(title, LV_PCT(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(title, ui::theme::text(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    const std::string title_text = ::ui::i18n::format("%s  %s",
                                                      package.display_name.c_str(),
                                                      package.version.c_str());
    ::ui::i18n::set_content_label_text_raw(title, title_text.c_str());

    lv_obj_t* action_btn = lv_btn_create(title_row);
    lv_obj_set_size(action_btn, 96, ::ui::page_profile::resolve_control_button_height());
    lv_obj_add_event_cb(action_btn,
                        on_install_clicked,
                        LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(index + 1U)));
    lv_obj_add_event_cb(action_btn, on_focus_scroll, LV_EVENT_FOCUSED, nullptr);
    if ((!package.compatible_firmware || !package.compatible_memory_profile) ||
        (package.installed && !package.update_available))
    {
        lv_obj_add_state(action_btn, LV_STATE_DISABLED);
    }
    lv_obj_t* action_label_obj = lv_label_create(action_btn);
    ::ui::i18n::set_label_text(action_label_obj, action_label(package));
    lv_obj_center(action_label_obj);

    lv_obj_t* summary = lv_label_create(card);
    lv_obj_set_width(summary, LV_PCT(100));
    lv_label_set_long_mode(summary, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(summary, ui::theme::text_muted(), 0);
    ::ui::i18n::set_content_label_text_raw(summary, package.summary.c_str());

    lv_obj_t* meta = lv_label_create(card);
    lv_obj_set_width(meta, LV_PCT(100));
    lv_label_set_long_mode(meta, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(meta, ui::theme::text_muted(), 0);
    const std::string meta_text =
        ::ui::i18n::format("Size: %s", format_size(package.archive_size_bytes).c_str());
    ::ui::i18n::set_content_label_text_raw(meta, meta_text.c_str());

    lv_obj_t* compat = lv_label_create(card);
    lv_obj_set_width(compat, LV_PCT(100));
    lv_label_set_long_mode(compat, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(compat,
                                (package.compatible_firmware && package.compatible_memory_profile)
                                    ? ui::theme::text_muted()
                                    : ui::theme::error(),
                                0);
    const std::string compat_text = compatibility_text(package);
    ::ui::i18n::set_content_label_text_raw(compat, compat_text.c_str());

    lv_obj_t* state = lv_label_create(card);
    lv_obj_set_width(state, LV_PCT(100));
    lv_label_set_long_mode(state, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(state,
                                package.update_available ? ui::theme::status_blue()
                                                         : ui::theme::status_green(),
                                0);
    const std::string state_value = state_text(package);
    ::ui::i18n::set_content_label_text_raw(state, state_value.c_str());

    if (app_g != nullptr)
    {
        lv_group_add_obj(app_g, action_btn);
    }
}

bool load_catalog(std::string& out_error)
{
    out_error.clear();
    s_runtime.packages.clear();

    if (!platform::ui::device::card_ready())
    {
        out_error = "Insert an SD card to use Extensions";
        return false;
    }

    if (!ui::runtime::packs::fetch_catalog(s_runtime.packages, out_error))
    {
        return false;
    }
    return true;
}

void rebuild_list()
{
    update_top_bar_status();

    std::string error;
    if (!load_catalog(error))
    {
        set_status_text(error.c_str());
        show_empty_message(error.c_str());
        return;
    }

    if (s_runtime.packages.empty())
    {
        set_status_text(::ui::i18n::tr("No packages available"));
        show_empty_message(::ui::i18n::tr("No packages available"));
        return;
    }

    set_status_text(::ui::i18n::format("Available packages: %lu",
                                       static_cast<unsigned long>(s_runtime.packages.size()))
                        .c_str());
    clear_list();
    for (std::size_t i = 0; i < s_runtime.packages.size(); ++i)
    {
        create_package_card(s_runtime.packages[i], i);
    }

    if (app_g != nullptr && s_runtime.top_bar.back_btn != nullptr)
    {
        lv_group_focus_obj(s_runtime.top_bar.back_btn);
        set_default_group(app_g);
        lv_group_set_editing(app_g, false);
    }
}

void on_refresh_clicked(lv_event_t* event)
{
    (void)event;
    rebuild_list();
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
    s_runtime.host = host;

    lv_group_t* previous_group = lv_group_get_default();
    set_default_group(nullptr);

    s_runtime.root = lv_obj_create(parent);
    lv_obj_set_size(s_runtime.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_runtime.root, ::ui::theme::page_bg(), 0);
    lv_obj_set_style_bg_opa(s_runtime.root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_runtime.root, 0, 0);
    lv_obj_set_style_radius(s_runtime.root, 0, 0);
    lv_obj_set_style_pad_all(s_runtime.root, 0, 0);
    lv_obj_set_style_pad_row(s_runtime.root, 0, 0);
    lv_obj_set_flex_flow(s_runtime.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_runtime.root, LV_OBJ_FLAG_SCROLLABLE);

    ::ui::widgets::top_bar_init(s_runtime.top_bar, s_runtime.root);
    ::ui::widgets::top_bar_set_title(s_runtime.top_bar, ::ui::i18n::tr("Extensions"));
    ::ui::widgets::top_bar_set_back_callback(s_runtime.top_bar, on_back, nullptr);

    s_runtime.content = lv_obj_create(s_runtime.root);
    lv_obj_set_width(s_runtime.content, LV_PCT(100));
    lv_obj_set_flex_grow(s_runtime.content, 1);
    lv_obj_set_style_bg_opa(s_runtime.content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_runtime.content, 0, 0);
    lv_obj_set_style_pad_all(s_runtime.content, 10, 0);
    lv_obj_set_style_pad_row(s_runtime.content, 8, 0);
    lv_obj_set_flex_flow(s_runtime.content, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_runtime.content, LV_OBJ_FLAG_SCROLLABLE);

    s_runtime.toolbar = lv_obj_create(s_runtime.content);
    lv_obj_set_width(s_runtime.toolbar, LV_PCT(100));
    lv_obj_set_style_bg_color(s_runtime.toolbar, ::ui::theme::surface(), 0);
    lv_obj_set_style_bg_opa(s_runtime.toolbar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_runtime.toolbar, 1, 0);
    lv_obj_set_style_border_color(s_runtime.toolbar, ::ui::theme::border(), 0);
    lv_obj_set_style_radius(s_runtime.toolbar, 12, 0);
    lv_obj_set_style_pad_all(s_runtime.toolbar, 8, 0);
    lv_obj_set_style_pad_column(s_runtime.toolbar, 8, 0);
    lv_obj_set_flex_flow(s_runtime.toolbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_runtime.toolbar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_runtime.toolbar, LV_OBJ_FLAG_SCROLLABLE);

    s_runtime.status_label = lv_label_create(s_runtime.toolbar);
    lv_obj_set_flex_grow(s_runtime.status_label, 1);
    lv_obj_set_width(s_runtime.status_label, LV_PCT(100));
    lv_label_set_long_mode(s_runtime.status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_runtime.status_label, ::ui::theme::text_muted(), 0);
    ::ui::i18n::set_label_text(s_runtime.status_label, "Loading...");

    s_runtime.refresh_btn = lv_btn_create(s_runtime.toolbar);
    lv_obj_set_size(s_runtime.refresh_btn, 90, ::ui::page_profile::resolve_control_button_height());
    lv_obj_add_event_cb(s_runtime.refresh_btn, on_refresh_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_runtime.refresh_btn, on_focus_scroll, LV_EVENT_FOCUSED, nullptr);
    lv_obj_t* refresh_label = lv_label_create(s_runtime.refresh_btn);
    ::ui::i18n::set_label_text(refresh_label, "Refresh");
    lv_obj_center(refresh_label);

    s_runtime.list = lv_obj_create(s_runtime.content);
    lv_obj_set_width(s_runtime.list, LV_PCT(100));
    lv_obj_set_flex_grow(s_runtime.list, 1);
    lv_obj_set_style_bg_opa(s_runtime.list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_runtime.list, 0, 0);
    lv_obj_set_style_pad_all(s_runtime.list, 0, 0);
    lv_obj_set_style_pad_row(s_runtime.list, 8, 0);
    lv_obj_set_flex_flow(s_runtime.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(s_runtime.list, LV_SCROLLBAR_MODE_AUTO);

    if (app_g != nullptr && s_runtime.top_bar.back_btn != nullptr)
    {
        lv_group_remove_all_objs(app_g);
        lv_group_add_obj(app_g, s_runtime.top_bar.back_btn);
        lv_group_add_obj(app_g, s_runtime.refresh_btn);
        lv_group_focus_obj(s_runtime.top_bar.back_btn);
        set_default_group(app_g);
        lv_group_set_editing(app_g, false);
    }
    else
    {
        set_default_group(previous_group);
    }

    rebuild_list();
}

void exit(lv_obj_t* parent)
{
    (void)parent;
    if (s_runtime.root != nullptr)
    {
        lv_obj_del(s_runtime.root);
    }
    s_runtime = RuntimeState{};
}

} // namespace extensions::ui::runtime
