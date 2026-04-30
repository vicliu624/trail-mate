#include "ui/menu/dashboard/dashboard_widgets.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "app/app_facade_access.h"
#include "ble/ble_manager.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/team_ui_store_runtime.h"
#include "ui/localization.h"
#include "ui/menu/dashboard/dashboard_state.h"
#include "ui/ui_status.h"

namespace ui::menu::dashboard
{
namespace
{

void set_label_text_if_changed_raw(lv_obj_t* label, const char* text)
{
    if (label == nullptr || text == nullptr)
    {
        return;
    }
    const char* current = lv_label_get_text(label);
    if (current == nullptr || std::strcmp(current, text) != 0)
    {
        ::ui::i18n::set_label_text_raw(label, text);
    }
}

void set_label_text_if_changed(lv_obj_t* label, const char* english)
{
    const char* localized = ::ui::i18n::tr(english);
    if (label == nullptr || localized == nullptr)
    {
        return;
    }
    const char* current = lv_label_get_text(label);
    if (current == nullptr || std::strcmp(current, localized) != 0)
    {
        ::ui::i18n::set_label_text_raw(label, localized);
    }
}

void create_stat_column(MeshWidgetUi& mesh, lv_obj_t* parent, lv_coord_t x, lv_coord_t body_h)
{
    static constexpr const char* kCaptions[] = {"Team", "Unread", "Power"};
    const lv_coord_t tile_w = std::max<lv_coord_t>(92, lv_obj_get_width(parent) - x);
    const lv_coord_t tile_h = (body_h - 12) / 3;
    const lv_color_t colors[] = {color_soft_amber(), color_soft_blue(), color_soft_green()};

    for (size_t i = 0; i < 3; ++i)
    {
        mesh.stat_tiles[i] = lv_obj_create(parent);
        lv_obj_set_size(mesh.stat_tiles[i], tile_w, tile_h);
        lv_obj_set_pos(mesh.stat_tiles[i], x, static_cast<lv_coord_t>(i * (tile_h + 6)));
        style_stat_tile(mesh.stat_tiles[i], colors[i]);

        mesh.stat_values[i] = lv_label_create(mesh.stat_tiles[i]);
        style_body_label(mesh.stat_values[i], &lv_font_montserrat_16, color_text());
        lv_obj_align(mesh.stat_values[i], LV_ALIGN_TOP_LEFT, 0, 0);
        ::ui::i18n::set_label_text_raw(mesh.stat_values[i], "--");

        mesh.stat_captions[i] = lv_label_create(mesh.stat_tiles[i]);
        style_body_label(mesh.stat_captions[i], &lv_font_montserrat_12, color_text_dim());
        lv_obj_align(mesh.stat_captions[i], LV_ALIGN_BOTTOM_LEFT, 0, 0);
        ::ui::i18n::set_label_text(mesh.stat_captions[i], kCaptions[i]);
    }
}

} // namespace

void create_mesh_widget(lv_obj_t* parent, lv_coord_t card_w, lv_coord_t card_h)
{
    auto& mesh = dashboard_state().mesh;
    mesh.chrome = create_card_chrome(parent, ::ui::i18n::tr("Mesh Status"), card_w, card_h);

    const lv_coord_t body_w = lv_obj_get_width(mesh.chrome.body);
    const lv_coord_t body_h = lv_obj_get_height(mesh.chrome.body);
    const lv_coord_t hero_d = std::min<lv_coord_t>(body_h - 12, std::max<lv_coord_t>(60, body_w / 2 - 8));

    mesh.orbit_outer = lv_obj_create(mesh.chrome.body);
    style_ring_object(mesh.orbit_outer, hero_d, 2, color_line());
    lv_obj_align(mesh.orbit_outer, LV_ALIGN_LEFT_MID, 4, 0);

    mesh.orbit_mid = lv_obj_create(mesh.chrome.body);
    style_ring_object(mesh.orbit_mid, hero_d - 16, 2, color_amber());
    lv_obj_align_to(mesh.orbit_mid, mesh.orbit_outer, LV_ALIGN_CENTER, 0, 0);

    mesh.orbit_core = lv_obj_create(mesh.chrome.body);
    lv_obj_set_size(mesh.orbit_core, hero_d - 34, hero_d - 34);
    lv_obj_set_style_radius(mesh.orbit_core, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(mesh.orbit_core, color_amber_dark(), 0);
    lv_obj_set_style_bg_opa(mesh.orbit_core, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(mesh.orbit_core, 0, 0);
    lv_obj_align_to(mesh.orbit_core, mesh.orbit_outer, LV_ALIGN_CENTER, 0, 0);

    mesh.hero_value = lv_label_create(mesh.orbit_core);
    style_body_label(mesh.hero_value, &lv_font_montserrat_16, lv_color_white());
    lv_obj_align(mesh.hero_value, LV_ALIGN_CENTER, 0, -8);
    ::ui::i18n::set_label_text(mesh.hero_value, "MESH");

    mesh.hero_caption = lv_label_create(mesh.orbit_core);
    style_body_label(mesh.hero_caption, &lv_font_montserrat_12, lv_color_hex(0xFFF7E8));
    lv_obj_align(mesh.hero_caption, LV_ALIGN_CENTER, 0, 10);
    ::ui::i18n::set_label_text(mesh.hero_caption, "online");

    create_stat_column(mesh, mesh.chrome.body, hero_d + 16, body_h);

    mesh.signal_bar_wrap = lv_obj_create(mesh.chrome.footer);
    lv_obj_set_size(mesh.signal_bar_wrap, 68, 18);
    lv_obj_align(mesh.signal_bar_wrap, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_opa(mesh.signal_bar_wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mesh.signal_bar_wrap, 0, 0);
    lv_obj_set_style_pad_all(mesh.signal_bar_wrap, 0, 0);
    lv_obj_clear_flag(mesh.signal_bar_wrap, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t i = 0; i < 12; ++i)
    {
        mesh.signal_bars[i] = lv_obj_create(mesh.signal_bar_wrap);
        lv_obj_set_size(mesh.signal_bars[i], 4, 6);
        lv_obj_set_pos(mesh.signal_bars[i], static_cast<lv_coord_t>(i * 5), 6);
        lv_obj_set_style_radius(mesh.signal_bars[i], 2, 0);
        lv_obj_set_style_bg_color(mesh.signal_bars[i], color_line(), 0);
        lv_obj_set_style_bg_opa(mesh.signal_bars[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(mesh.signal_bars[i], 0, 0);
    }

    mesh.footer_label = lv_label_create(mesh.chrome.footer);
    style_footer_label(mesh.footer_label);
    lv_obj_align(mesh.footer_label, LV_ALIGN_RIGHT_MID, 0, 0);
    ::ui::i18n::set_label_text(mesh.footer_label, "waiting for traffic");
}

void refresh_mesh_widget()
{
    auto& mesh = dashboard_state().mesh;
    if (mesh.chrome.card == nullptr)
    {
        return;
    }

    team::ui::TeamUiSnapshot team_snap;
    const bool has_team = team::ui::team_ui_get_store().load(team_snap) && team_snap.in_team;
    const int unread = ui::status::get_total_unread();
    const auto battery = platform::ui::device::battery_info();

    char power_buf[16];
    if (battery.level >= 0)
    {
        if (battery.charging)
        {
            std::snprintf(power_buf, sizeof(power_buf), "USB");
        }
        else
        {
            std::snprintf(power_buf, sizeof(power_buf), "%d%%", battery.level);
        }
    }
    else
    {
        std::snprintf(power_buf, sizeof(power_buf), "--");
    }

    set_status_chip(mesh.chrome, has_team ? "LINKED" : "SOLO", has_team ? color_soft_green() : color_soft_amber(),
                    has_team ? color_ok() : color_text());
    set_label_text_if_changed(mesh.hero_value, has_team ? "TEAM" : "MESH");
    set_label_text_if_changed(mesh.hero_caption, unread > 0 ? "traffic active" : "standing by");

    char team_buf[16];
    std::snprintf(team_buf, sizeof(team_buf), "%u", has_team ? static_cast<unsigned>(team_snap.members.size()) : 0U);
    set_label_text_if_changed_raw(mesh.stat_values[0], team_buf);

    char unread_buf[16];
    std::snprintf(unread_buf, sizeof(unread_buf), "%d", unread);
    set_label_text_if_changed_raw(mesh.stat_values[1], unread_buf);
    lv_obj_set_style_text_color(mesh.stat_values[1], unread > 0 ? color_warn() : color_text(), 0);

    set_label_text_if_changed_raw(mesh.stat_values[2], power_buf);
    lv_obj_set_style_text_color(mesh.stat_values[2], battery.charging ? color_info() : color_text(), 0);

    char footer[64];
    bool ble_active = false;
    bool ble_linked = false;
    if (auto* ble = app::runtimeFacade().getBleManager())
    {
        ble_active = ble->isEnabled();
        ble::BlePairingStatus ble_status{};
        if (ble->getPairingStatus(&ble_status))
        {
            ble_linked = ble_status.is_connected;
        }
    }
    std::snprintf(footer,
                  sizeof(footer),
                  "%s",
                  ::ui::i18n::format(
                      ble_linked ? "BLE linked  |  %s"
                                 : (ble_active ? "BLE bridge ready  |  %s" : "LoRa direct path  |  %s"),
                      unread > 0 ? ::ui::i18n::tr("new activity") : ::ui::i18n::tr("quiet net"))
                      .c_str());
    set_label_text_if_changed_raw(mesh.footer_label, footer);

    lv_obj_set_style_border_opa(mesh.orbit_outer, LV_OPA_60, 0);
    lv_obj_set_style_border_opa(mesh.orbit_mid, LV_OPA_COVER, 0);

    const int activity = std::min(12, std::max(2, unread + (has_team ? 3 : 1)));
    for (size_t i = 0; i < 12; ++i)
    {
        const lv_coord_t height = static_cast<lv_coord_t>(5 + (i % 4U) * 3U);
        lv_obj_set_height(mesh.signal_bars[i], height);
        lv_obj_set_y(mesh.signal_bars[i], static_cast<lv_coord_t>(18 - height));
        lv_obj_set_style_bg_color(mesh.signal_bars[i], i < static_cast<size_t>(activity) ? color_ok() : color_line(), 0);
        lv_obj_set_style_bg_opa(mesh.signal_bars[i], i < static_cast<size_t>(activity) ? LV_OPA_COVER : LV_OPA_60, 0);
    }
}

} // namespace ui::menu::dashboard
