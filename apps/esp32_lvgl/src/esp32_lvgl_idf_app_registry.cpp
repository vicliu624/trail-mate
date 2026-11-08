#include "ui/app_registry.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/lora_runtime.h"
#include "platform/ui/pack_repository_runtime.h"
#include "platform/ui/route_storage.h"
#include "platform/ui/sstv_runtime.h"
#include "platform/ui/tracker_runtime.h"
#include "platform/ui/usb_support_runtime.h"
#include "platform/ui/walkie_runtime.h"
#include "platform/ui/wireless_companion_runtime.h"
#include "ui/app_catalog.h"
#include "ui/app_catalog_builder.h"
#include "ui/app_screen.h"
#include "ui/callback_app_screen.h"
#include "ui/localization.h"
#include "ui/ui_theme.h"

#include <cstdio>

namespace ui::assets
{
extern "C" const lv_image_dsc_t Setting;
}

namespace
{

#define APP_REG_LOG(...) std::printf("[UI][Registry] " __VA_ARGS__)

using ui::assets::Setting;

struct CompanionPageState
{
    lv_obj_t* root = nullptr;
};

CompanionPageState s_companion_page_state;

void add_label(lv_obj_t* parent,
               const char* text,
               const lv_font_t* font,
               lv_color_t color)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
}

void add_status_line(lv_obj_t* parent, const char* label, const char* value)
{
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s: %s", label ? label : "", value ? value : "");
    add_label(parent, buf, &lv_font_montserrat_14, ui::theme::text());
}

void add_u32_line(lv_obj_t* parent, const char* label, unsigned long value)
{
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s: %lu", label ? label : "", value);
    add_label(parent, buf, &lv_font_montserrat_14, ui::theme::text());
}

void add_hex_line(lv_obj_t* parent, const char* label, uint32_t value)
{
    char buf[48];
    std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(value));
    add_status_line(parent, label, buf);
}

void companion_enter(void* user_data, lv_obj_t* parent)
{
    auto* state = static_cast<CompanionPageState*>(user_data);
    if (!state || !parent || (state->root && lv_obj_is_valid(state->root)))
    {
        return;
    }

    state->root = lv_obj_create(parent);
    lv_obj_set_size(state->root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(state->root, ui::theme::white(), 0);
    lv_obj_set_style_bg_opa(state->root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(state->root, 0, 0);
    lv_obj_set_style_radius(state->root, 0, 0);
    lv_obj_set_style_pad_left(state->root, 18, 0);
    lv_obj_set_style_pad_right(state->root, 18, 0);
    lv_obj_set_style_pad_top(state->root, 18, 0);
    lv_obj_set_style_pad_bottom(state->root, 18, 0);
    lv_obj_set_flex_flow(state->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(state->root, 8, 0);

    add_label(state->root, ::ui::i18n::tr("C6 Companion"), &lv_font_montserrat_14, ui::theme::text());

    const auto st = platform::ui::wireless_companion::status();
    add_status_line(state->root, "State", platform::ui::wireless_companion::state_name(st.state));
    add_status_line(state->root, "Message", st.message);
    add_status_line(state->root, "Detail", st.detail);
    add_status_line(state->root, "Board capable", st.board_capable ? "yes" : "no");
    add_status_line(state->root, "Started", st.started ? "yes" : "no");
    add_status_line(state->root, "Present", st.present ? "yes" : "no");
    add_u32_line(state->root, "Protocol min", st.protocol_min);
    add_u32_line(state->root, "Protocol max", st.protocol_max);
    add_u32_line(state->root, "Selected protocol", st.selected_protocol);

    add_hex_line(state->root, "Supported features", st.supported_features);
    add_hex_line(state->root, "Enabled features", st.enabled_features);
    add_u32_line(state->root, "Config seq", st.config_seq);
    add_u32_line(state->root, "Config error", st.config_error);
    add_u32_line(state->root, "Selected MTU", st.selected_mtu);
    add_status_line(state->root,
                    "BLE",
                    platform::ui::wireless_companion::service_state_name(st.ble_state));
    add_status_line(state->root,
                    "ESP-NOW",
                    platform::ui::wireless_companion::service_state_name(st.espnow_state));
    add_status_line(state->root,
                    "Wi-Fi",
                    platform::ui::wireless_companion::service_state_name(st.wifi_state));

    add_u32_line(state->root, "BLE uplink", st.ble_uplink_count);
    add_u32_line(state->root, "BLE events", st.ble_event_count);
    add_u32_line(state->root, "ESP-NOW uplink", st.espnow_uplink_count);
    add_u32_line(state->root, "ESP-NOW events", st.espnow_event_count);
    add_u32_line(state->root, "Wi-Fi events", st.wifi_event_count);
    add_u32_line(state->root, "Firmware", st.firmware_version);
    add_u32_line(state->root, "Free heap", st.free_heap);
}

void companion_exit(void* user_data, lv_obj_t* parent)
{
    (void)parent;
    auto* state = static_cast<CompanionPageState*>(user_data);
    if (!state || !state->root || !lv_obj_is_valid(state->root))
    {
        if (state)
        {
            state->root = nullptr;
        }
        return;
    }
    lv_obj_del(state->root);
    state->root = nullptr;
}

ui::CallbackAppScreen s_companion_app("c6_companion",
                                      "C6 Companion",
                                      &Setting,
                                      companion_enter,
                                      companion_exit,
                                      &s_companion_page_state);

struct IdfCatalogState
{
    ui::AppCatalog base{};
    AppScreen* companion = nullptr;
};

std::size_t idf_catalog_count(void* user_data)
{
    auto* state = static_cast<IdfCatalogState*>(user_data);
    if (state == nullptr)
    {
        return 0;
    }
    return ui::catalogCount(state->base) + (state->companion != nullptr ? 1U : 0U);
}

AppScreen* idf_catalog_at(void* user_data, std::size_t index)
{
    auto* state = static_cast<IdfCatalogState*>(user_data);
    if (state == nullptr)
    {
        return nullptr;
    }

    const std::size_t base_count = ui::catalogCount(state->base);
    if (index < base_count)
    {
        return ui::catalogAt(state->base, index);
    }
    if (index == base_count)
    {
        return state->companion;
    }
    return nullptr;
}

ui::app_catalog_builder::FeatureFlags buildFeatureFlags()
{
    ui::app_catalog_builder::FeatureFlags flags{};
    flags.profile = ui::app_catalog_builder::CatalogProfile::IdfDefault;
    flags.include_gps_map = platform::ui::device::gps_supported();
    flags.include_gnss_skyplot = platform::ui::device::gps_supported();
    flags.include_tracker = platform::ui::route_storage::is_supported() ||
                            platform::ui::tracker::is_supported();
    flags.include_energy_sweep = platform::ui::lora::is_supported();
    flags.include_sstv = platform::ui::sstv::is_supported();
    flags.include_usb = platform::ui::usb_support::is_supported() &&
                        platform::ui::device::sd_ready();
    flags.include_extensions = ui::runtime::packs::is_supported();
    flags.include_network = app::hasAppFacade() &&
                            chat::infra::isReticulumMeshProtocol(
                                app::configFacade().readConfig().mesh_protocol);
    flags.include_walkie_talkie = platform::ui::walkie::is_supported();
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    flags.include_power_off = true;
#endif
    APP_REG_LOG(
        "flags profile=idf gps_map=%d skyplot=%d tracker=%d chat=%d sweep=%d sstv=%d usb=%d extensions=%d network=%d walkie=%d power_off=%d gps_supported=%d gps_ready=%d sd_ready=%d\n",
        flags.include_gps_map ? 1 : 0,
        flags.include_gnss_skyplot ? 1 : 0,
        flags.include_tracker ? 1 : 0,
        flags.include_chat ? 1 : 0,
        flags.include_energy_sweep ? 1 : 0,
        flags.include_sstv ? 1 : 0,
        flags.include_usb ? 1 : 0,
        flags.include_extensions ? 1 : 0,
        flags.include_network ? 1 : 0,
        flags.include_walkie_talkie ? 1 : 0,
        flags.include_power_off ? 1 : 0,
        platform::ui::device::gps_supported() ? 1 : 0,
        platform::ui::device::gps_ready() ? 1 : 0,
        platform::ui::device::sd_ready() ? 1 : 0);
    return flags;
}

IdfCatalogState s_catalog_state;
ui::AppCatalog s_catalog{&s_catalog_state, idf_catalog_count, idf_catalog_at};

ui::AppCatalog buildCatalog()
{
    s_catalog_state.base = ui::app_catalog_builder::build(buildFeatureFlags());
    s_catalog_state.companion =
        platform::ui::wireless_companion::is_supported() ? &s_companion_app : nullptr;
    return s_catalog;
}

} // namespace

namespace ui
{

AppCatalog appCatalog()
{
    return buildCatalog();
}

} // namespace ui
