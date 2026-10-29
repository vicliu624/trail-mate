#include "linux_cardputer_zero_app_shell.h"

#include "ui_lvgl_ux_packs/ux/ux_menu_provider.h"
#include "ui_presentation/menu/menu_model.h"

#include <cassert>
#include <cstring>

int main()
{
    trailmate::apps::linux_cardputer_zero::LinuxCardputerZeroAppShell shell;
    assert(shell.validate());

    const auto& config = shell.config();
    assert(std::strcmp(config.target_id, "cardputerzero") == 0);
    assert(std::strcmp(config.ux_pack_id, "cardputer_compact") == 0);
    assert(std::strcmp(shell.targetId(), "cardputerzero") == 0);
    assert(std::strcmp(shell.activeUxPackId(), "cardputer_compact") == 0);

    const auto& facts = shell.boardFacts();
    assert(std::strcmp(facts.board_id, "cardputerzero") == 0);
    assert(facts.logical_display_width == 320);
    assert(facts.logical_display_height == 170);
    assert(facts.keyboard_present);
    assert(!facts.touch_present);
    assert(!facts.pointer_present);
    assert(!facts.trackball_present);
    assert(facts.lora_present);
    assert(std::strcmp(facts.lora_state, "hardware_documented_runtime_pending") == 0);
    assert(std::strcmp(facts.lora_external_module, "M5Stack Cap LoRa-1262") == 0);
    assert(std::strcmp(facts.lora_chip, "sx1262") == 0);
    assert(std::strcmp(facts.lora_spidev, "/dev/spidev0.1") == 0);
    assert(std::strcmp(facts.lora_gpiochip, "/dev/gpiochip0") == 0);
    assert(facts.lora_spi_speed_hz == 500000);
    assert(facts.lora_reset_gpio == 26);
    assert(facts.lora_irq_gpio == 23);
    assert(facts.lora_busy_gpio == 22);
    assert(facts.lora_power_gpio == -1);
    assert(facts.lora_dio2_as_rf_switch);
    assert(facts.lora_dio3_tcxo_voltage);
    assert(facts.gps_present);
    assert(std::strcmp(facts.gps_state,
                       "cap_lora_1262_gnss_hardware_documented_runtime_pending") == 0);
    assert(std::strcmp(facts.gps_external_module, "M5Stack Cap LoRa-1262") == 0);
    assert(std::strcmp(facts.gps_chip, "ATGM336H-6N@AT6668") == 0);
    assert(std::strcmp(facts.gps_protocol, "NMEA 0183 4.1") == 0);
    assert(std::strcmp(facts.gps_transport, "uart") == 0);
    assert(facts.gps_default_baud == 115200);
    assert(facts.gps_rx_gpio == 15);
    assert(facts.gps_tx_gpio == 14);

    const auto& notifications = shell.notificationPort().contract();
    assert(std::strcmp(notifications.bus_name, "org.freedesktop.Notifications") == 0);
    assert(std::strcmp(notifications.object_path, "/org/freedesktop/Notifications") == 0);
    assert(std::strcmp(notifications.notify_signature, "susssasa{sv}i") == 0);
    assert(std::strcmp(notifications.capability_body, "body") == 0);
    assert(std::strcmp(notifications.capability_actions, "actions") == 0);
    assert(std::strcmp(notifications.capability_persistence, "persistence") == 0);
    assert(std::strcmp(notifications.shell_ipc_socket_suffix,
                       "cardputer-zero/notifyd.sock") == 0);
    assert(!notifications.shell_ipc_creates_notifications);

    trailmate::apps::linux_cardputer_zero::NotificationRequest notification_request;
    notification_request.summary = "Mesh";
    notification_request.body = "Message received";
    notification_request.urgency =
        trailmate::apps::linux_cardputer_zero::NotificationUrgency::Critical;
    const auto notify_call = shell.notificationPort().makeNotifyCall(notification_request);
    assert(notify_call.contract == &notifications);
    assert(std::strcmp(notify_call.summary, "Mesh") == 0);
    assert(std::strcmp(notify_call.body, "Message received") == 0);
    assert(notify_call.urgency_hint == 2);

    const auto& input_method = shell.inputMethodPort().contract();
    assert(std::strcmp(input_method.framework_name, "fcitx5") == 0);
    assert(std::strcmp(input_method.ui_addon_name, "cardputerzero-ui") == 0);
    assert(std::strcmp(input_method.panel_socket_suffix,
                       "cardputer-zero/ime-panel.sock") == 0);
    assert(std::strcmp(input_method.panel_update_message, "panel.update") == 0);
    assert(std::strcmp(input_method.panel_hide_message, "panel.hide") == 0);
    assert(std::strcmp(input_method.state_update_message, "state.update") == 0);
    assert(std::strcmp(input_method.xmodifiers, "@im=fcitx") == 0);
    assert(std::strcmp(input_method.sdl_im_module, "fcitx") == 0);
    assert(input_method.screen_width == 320);
    assert(input_method.screen_height == 170);
    assert(input_method.max_exported_candidates == 3);
    assert(!input_method.app_owns_input_method_engine);
    assert(!input_method.app_owns_candidate_display);
    assert(!input_method.app_owns_text_commit);
    assert(!input_method.panel_socket_commits_text);
    assert(!input_method.panel_requests_keyboard_focus);
    assert(!input_method.missing_panel_blocks_text_input);
    assert(!input_method.uses_embedded_touch_ime);

    const auto* profile = shell.targetProfile();
    assert(profile != nullptr);
    assert(profile->platform == product_composition::TargetPlatform::Linux);
    assert(profile->renderer == product_composition::TargetRenderer::Lvgl);
    assert(std::strcmp(profile->app_shell, "apps/linux_cardputer_zero") == 0);
    assert(profile->has_lora);
    assert(profile->has_gps);

    ui::menu::MenuModel menu;
    assert(ui_lvgl_ux::buildMenuForUxPack(shell.activeUxPackId(), menu));
    assert(menu.size() == 14);
    bool has_map = false;
    bool has_gps = false;
    bool has_sky_plot = false;
    bool has_energy_sweep = false;
    bool has_walkie = false;
    bool has_sstv = false;
    bool has_extensions = false;
    bool has_settings = false;
    for (size_t index = 0; index < menu.size(); ++index)
    {
        if (menu.items()[index].screen_id == ui::menu::MenuScreenId::Map)
        {
            has_map = true;
        }
        if (menu.items()[index].screen_id == ui::menu::MenuScreenId::Gps)
        {
            has_gps = true;
        }
        if (menu.items()[index].screen_id == ui::menu::MenuScreenId::SkyPlot)
        {
            has_sky_plot = true;
        }
        if (menu.items()[index].screen_id == ui::menu::MenuScreenId::EnergySweep)
        {
            has_energy_sweep = true;
        }
        if (menu.items()[index].screen_id == ui::menu::MenuScreenId::WalkieTalkie)
        {
            has_walkie = true;
        }
        if (menu.items()[index].screen_id == ui::menu::MenuScreenId::Sstv)
        {
            has_sstv = true;
        }
        if (menu.items()[index].screen_id == ui::menu::MenuScreenId::Extensions)
        {
            has_extensions = true;
        }
        if (menu.items()[index].screen_id == ui::menu::MenuScreenId::Settings)
        {
            has_settings = true;
        }
    }
    assert(has_map);
    assert(!has_gps);
    assert(has_sky_plot);
    assert(has_energy_sweep);
    assert(has_walkie);
    assert(!has_sstv);
    assert(has_extensions);
    assert(has_settings);
    return 0;
}
