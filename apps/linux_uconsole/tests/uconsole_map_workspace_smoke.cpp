#include "app/linux_app_services.h"
#include "chat/infra/meshtastic/mt_radio_config.h"
#include "chat/usecase/contact_service.h"
#include "meshtastic/config.pb.h"
#include "platform/ui/settings_store.h"
#include "uconsole/uconsole_map_workspace_model.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{

void set_env_var(const char* name, const std::string& value)
{
    setenv(name, value.c_str(), 1);
}

} // namespace

int main()
{
    const auto root =
        std::filesystem::temp_directory_path() /
        "trailmate_uconsole_map_workspace_smoke";

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "settings", ec);
    std::filesystem::create_directories(root / "sd", ec);
    std::filesystem::create_directories(root / "cache", ec);

    set_env_var("TRAIL_MATE_SETTINGS_ROOT", (root / "settings").string());
    set_env_var("TRAIL_MATE_SD_ROOT", (root / "sd").string());
    set_env_var("TRAIL_MATE_CACHE_ROOT", (root / "cache").string());
    set_env_var("TRAIL_MATE_GPS_VALID", "0");
    set_env_var("TRAIL_MATE_MAP_LAT", "");
    set_env_var("TRAIL_MATE_MAP_LNG", "");

    ::platform::ui::settings_store::clear_namespace("uconsole_map");

    trailmate::linux_app::LinuxAppServices services;
    trailmate::uconsole::UConsoleMapWorkspaceModel model(services);
    const auto snapshot = model.snapshot();

    assert(snapshot.has_center);
    assert(!snapshot.has_fix);
    assert(snapshot.presentation_workspace.header.valid);
    assert(snapshot.presentation_workspace.viewport.zoom == 2);
    assert(!snapshot.presentation_workspace.self.valid);
    assert(snapshot.presentation_workspace.active_tool ==
           ::ui::map::MapToolKind::Pan);
    assert(snapshot.using_default_center);
    assert(!snapshot.has_configured_center);
    assert(snapshot.zoom == 2);
    assert(std::abs(snapshot.lat) < 0.000001);
    assert(std::abs(snapshot.lon) < 0.000001);
    assert(snapshot.source_label == "OSM");
    assert(snapshot.columns == 5U);
    assert(snapshot.rows == 3U);
    assert(snapshot.center_tile_index == 7U);
    assert(snapshot.tiles.size() == 15U);
    for (const auto& tile : snapshot.tiles)
    {
        assert(tile.id.source == ::platform::linux_runtime::MapBaseSource::Osm);
        assert(tile.id.z == 2);
    }

    model.panByDisplayDelta(100.0,
                            0.0,
                            500,
                            300,
                            snapshot.lat,
                            snapshot.lon,
                            snapshot.zoom,
                            true);
    const auto panned = model.snapshot();
    assert(panned.has_manual_center);
    assert(!panned.using_default_center);
    assert(panned.zoom == 2);
    assert(panned.lon < -40.0);

    model.clearManualCenter();
    const auto recentered = model.snapshot();
    assert(!recentered.has_manual_center);
    assert(recentered.using_default_center);

    set_env_var("TRAIL_MATE_MAP_LAT", "31.2304");
    set_env_var("TRAIL_MATE_MAP_LNG", "121.4737");
    model.setZoom(20);
    assert(model.snapshot().zoom == 18);
    model.setZoom(0);
    assert(model.snapshot().zoom == 1);
    model.setZoom(12);

    model.setContourEnabled(false);
    const auto contours_off = model.snapshot();
    assert(!contours_off.contour_enabled);
    assert(contours_off.contour_tiles.empty());

    model.setContourEnabled(true);
    model.setContourUltraFineEnabled(false);
    model.setEarthdataToken("  test-earthdata-token  ");
    assert(model.earthdataToken() == "test-earthdata-token");
    assert(model.earthdataTokenConfigured());
    const auto contours_missing = model.snapshot();
    assert(contours_missing.contour_enabled);
    assert(contours_missing.presentation_workspace.layers.contour);
    assert(!contours_missing.contour_ultra_fine_enabled);
    assert(contours_missing.earthdata_token_configured);
    assert(contours_missing.contour_profiles.size() == 2U);
    assert(contours_missing.contour_profiles[0] == "major-100");
    assert(contours_missing.contour_profiles[1] == "minor-50");
    assert(contours_missing.contour_tiles.size() ==
           contours_missing.tiles.size() *
               contours_missing.contour_profiles.size());
    assert(contours_missing.contour_available_count == 0U);
    assert(contours_missing.contour_missing_count ==
           contours_missing.contour_tiles.size());

    const auto contour_tile_path = contours_missing.contour_tiles.front().path;
    std::filesystem::create_directories(contour_tile_path.parent_path(), ec);
    {
        std::ofstream out(contour_tile_path, std::ios::binary);
        out << "png";
    }
    const auto contours_available = model.snapshot();
    assert(contours_available.contour_available_count == 1U);
    assert(contours_available.contour_missing_count + 1U ==
           contours_available.contour_tiles.size());
    assert(contours_available.contour_tiles.front().available);
    assert(contours_available.contour_tiles.front().path == contour_tile_path);

    model.setContourUltraFineEnabled(true);
    model.setZoom(17);
    const auto contours_ultra = model.snapshot();
    assert(contours_ultra.contour_ultra_fine_enabled);
    assert(contours_ultra.contour_profiles.size() == 2U);
    assert(contours_ultra.contour_profiles[0] == "major-25");
    assert(contours_ultra.contour_profiles[1] == "minor-5");
    model.setContourEnabled(false);
    assert(model.snapshot().contour_tiles.empty());
    model.setZoom(12);

    ::chat::contacts::NodeUpdate mqtt_node{};
    mqtt_node.short_name = "MQ01";
    mqtt_node.long_name = "MQTT Node";
    mqtt_node.has_last_seen = true;
    mqtt_node.last_seen = 123456U;
    mqtt_node.has_via_mqtt = true;
    mqtt_node.via_mqtt = true;
    mqtt_node.has_position = true;
    mqtt_node.position.valid = true;
    mqtt_node.position.latitude_i = 312304000;
    mqtt_node.position.longitude_i = 1214737000;
    services.contacts().applyNodeUpdate(0x1234ABCDU, mqtt_node);

    model.setShowMqttNodes(true);
    const auto mqtt_visible = model.snapshot();
    assert(mqtt_visible.show_mqtt_nodes);
    assert(mqtt_visible.visible_mqtt_node_count == 1U);
    assert(mqtt_visible.hidden_mqtt_node_count == 0U);
    assert(mqtt_visible.nodes.size() == 1U);
    assert(mqtt_visible.nodes[0].via_mqtt);
    assert(mqtt_visible.nodes[0].x_fraction >= 0.0);
    assert(mqtt_visible.nodes[0].x_fraction <= 1.0);
    assert(mqtt_visible.nodes[0].y_fraction >= 0.0);
    assert(mqtt_visible.nodes[0].y_fraction <= 1.0);

    const auto click_point =
        model.coordinateAtDisplayPoint(mqtt_visible, 240.0, 120.0, 500, 300);
    assert(click_point.valid);
    assert(std::isfinite(click_point.lat));
    assert(std::isfinite(click_point.lon));
    const int clicked_zoom = mqtt_visible.zoom;
    model.zoomInAt(click_point.lat, click_point.lon);
    const auto zoomed_in = model.snapshot();
    assert(zoomed_in.has_manual_center);
    assert(zoomed_in.zoom == clicked_zoom + 1);
    assert(std::abs(zoomed_in.lat - click_point.lat) < 0.000001);
    assert(std::abs(zoomed_in.lon - click_point.lon) < 0.000001);
    model.zoomOutAt(click_point.lat, click_point.lon);
    const auto zoomed_out = model.snapshot();
    assert(zoomed_out.zoom == clicked_zoom);
    assert(std::abs(zoomed_out.lat - click_point.lat) < 0.000001);
    assert(std::abs(zoomed_out.lon - click_point.lon) < 0.000001);
    model.setZoom(12);

    ::chat::contacts::NodeUpdate lora_node{};
    lora_node.short_name = "LORA";
    lora_node.long_name = "LoRa Node";
    lora_node.has_last_seen = true;
    lora_node.last_seen = 123457U;
    lora_node.has_via_mqtt = true;
    lora_node.via_mqtt = false;
    lora_node.has_position = true;
    lora_node.position.valid = true;
    lora_node.position.latitude_i = 312305000;
    lora_node.position.longitude_i = 1214747000;
    lora_node.has_hops_away = true;
    lora_node.hops_away = 1;
    services.contacts().applyNodeUpdate(0x2234ABCDU, lora_node);

    model.setShowMqttNodes(false);
    const auto mqtt_hidden = model.snapshot();
    assert(!mqtt_hidden.show_mqtt_nodes);
    assert(mqtt_hidden.visible_mqtt_node_count == 0U);
    assert(mqtt_hidden.hidden_mqtt_node_count == 1U);
    assert(mqtt_hidden.nodes.size() == 1U);
    assert(!mqtt_hidden.nodes[0].via_mqtt);
    assert(mqtt_hidden.nodes[0].node_id == 0x2234ABCDU);

    ::chat::MeshConfig mt_config{};
    mt_config.region = static_cast<uint8_t>(
        meshtastic_Config_LoRaConfig_RegionCode_CN);
    mt_config.use_preset = true;
    mt_config.modem_preset = static_cast<uint8_t>(
        meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST);
    mt_config.tx_power = 0;
    const auto radio = ::chat::meshtastic::deriveRadioConfig(mt_config);
    assert(radio.region_code == meshtastic_Config_LoRaConfig_RegionCode_CN);
    assert(radio.sync_word == ::chat::meshtastic::kMeshtasticLoraSyncWord);
    assert(radio.preamble_len == ::chat::meshtastic::kMeshtasticLoraPreambleLen);
    assert(radio.crc_len == ::chat::meshtastic::kMeshtasticLoraCrcLen);
    assert(radio.sf == 11U);
    assert(radio.cr_denom == 5U);
    assert(radio.freq_mhz >= 470.0f);
    assert(radio.freq_mhz <= 510.0f);
    assert(std::abs(radio.freq_mhz - 433.175f) > 1.0f);

    std::filesystem::remove_all(root, ec);
    return 0;
}
