#include "platform/esp/arduino_common/hostlink/hostlink_config_service.h"

#include "hostlink/hostlink_config_codec.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/usecase/chat_service.h"
#include "platform/esp/arduino_common/hostlink/hostlink_bridge_radio.h"
#include "platform/ui/device_runtime.h"
#if defined(ARDUINO)
#include "platform/esp/arduino_common/storage/storage_runtime.h"
#include "sys/clock.h"
#include <esp_heap_caps.h>
#endif

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
#include "platform/esp/idf_common/tab5_rtc_runtime.h"
#endif

#include <cstring>
#include <sys/time.h>

namespace hostlink
{

namespace
{
template <size_t DestSize, size_t SrcSize>
void copy_cstr_field(char (&dest)[DestSize], const char (&src)[SrcSize])
{
    static_assert(DestSize > 0, "destination buffer must not be empty");
    constexpr size_t copy_len = (DestSize - 1 < SrcSize - 1) ? (DestSize - 1) : (SrcSize - 1);
    std::memcpy(dest, src, copy_len);
    dest[copy_len] = '\0';
}
} // namespace

bool build_status_payload(std::vector<uint8_t>& out, uint8_t link_state, uint32_t last_error, bool include_config)
{
    const platform::ui::device::BatteryInfo battery_info = platform::ui::device::battery_info();
    app::IAppConfigFacade& config_api = app::configFacade();
    const app::AppConfig& cfg = config_api.readConfig();
    hostlink::bridge::AppRxStats stats = hostlink::bridge::get_app_rx_stats();

    StatusPayloadSnapshot snapshot;
    snapshot.battery = (!battery_info.available || battery_info.level < 0) ? 0xFF : static_cast<uint8_t>(battery_info.level);
    snapshot.charging = battery_info.charging;
    snapshot.link_state = link_state;
    snapshot.last_error = last_error;
    snapshot.mesh_protocol =
        static_cast<uint8_t>(chat::infra::normalizeMeshProtocol(cfg.mesh_protocol));
    snapshot.region = cfg.meshtastic_config.region;
    snapshot.channel = cfg.chat_channel;
    snapshot.duty_cycle = cfg.net_duty_cycle;
    snapshot.channel_util = cfg.net_channel_util;
    snapshot.app_rx.total = stats.total;
    snapshot.app_rx.from_is = stats.from_is;
    snapshot.app_rx.direct = stats.direct;
    snapshot.app_rx.relayed = stats.relayed;

    const app::AprsConfig& aprs = cfg.aprs;
    snapshot.aprs.enabled = aprs.enabled;
    copy_cstr_field(snapshot.aprs.igate_callsign, aprs.igate_callsign);
    snapshot.aprs.igate_ssid = aprs.igate_ssid;
    copy_cstr_field(snapshot.aprs.tocall, aprs.tocall);
    copy_cstr_field(snapshot.aprs.path, aprs.path);
    snapshot.aprs.tx_min_interval_s = aprs.tx_min_interval_s;
    snapshot.aprs.dedupe_window_s = aprs.dedupe_window_s;
    snapshot.aprs.symbol_table = aprs.symbol_table;
    snapshot.aprs.symbol_code = aprs.symbol_code;
    snapshot.aprs.position_interval_s = aprs.position_interval_s;
    snapshot.aprs.node_map_len = aprs.node_map_len;
    if (snapshot.aprs.node_map_len > 0)
    {
        std::memcpy(snapshot.aprs.node_map, aprs.node_map, snapshot.aprs.node_map_len);
    }
    snapshot.aprs.self_enable = aprs.self_enable;
    copy_cstr_field(snapshot.aprs.self_callsign, aprs.self_callsign);

#if defined(ARDUINO)
    if (!encode_status_payload(out, snapshot, include_config)) return false;
    if (include_config) return true;
    // Optional read-only storage health snapshot. No filesystem access,
    // configuration payload, paths, or credentials are included.
    const auto storage = platform::esp::arduino_common::storage::runtime_diagnostics();
    out.push_back(240);
    out.push_back(26);
    out.push_back(1);
    out.push_back(storage.state);
    out.push_back(storage.active_operation);
    out.push_back(storage.pending_operation);
    out.push_back(storage.retry_attempt);
    out.push_back(storage.flags);
    const auto append_u32 = [&out](uint32_t value)
    {
        for (unsigned shift = 0; shift < 32; shift += 8)
            out.push_back(static_cast<uint8_t>(value >> shift));
    };
    append_u32(storage.generation);
    append_u32(storage.retry_due_ms);
    append_u32(sys::millis_now());
    append_u32(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    append_u32(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    return true;
#else
    return encode_status_payload(out, snapshot, include_config);
#endif
}

bool apply_config(const uint8_t* data, size_t len, uint32_t* out_err)
{
    if (out_err)
    {
        *out_err = 0;
    }
    if (!data || len == 0)
    {
        return false;
    }

    ConfigPatch patch;
    if (!decode_config_patch(data, len, patch))
    {
        return false;
    }

    app::IAppConfigFacade& config_api = app::configFacade();
    app::IAppMessagingFacade& messaging_api = app::messagingFacade();
    const app::AppConfig& current_config = config_api.readConfig();
    const chat::MeshProtocol original_protocol = current_config.mesh_protocol;
    bool mesh_changed = false;
    bool net_changed = false;
    bool chat_changed = false;
    bool aprs_changed = false;
    bool protocol_changed = false;
    chat::MeshProtocol target_protocol = current_config.mesh_protocol;

    if (patch.has_mesh_protocol)
    {
        if (!chat::infra::isValidMeshProtocolValue(patch.mesh_protocol))
        {
            return false;
        }
        target_protocol = chat::infra::meshProtocolFromRaw(patch.mesh_protocol);
        mesh_changed = true;
        protocol_changed = (target_protocol != original_protocol);
    }
    auto edit = config_api.beginConfigEdit();
    if (!edit)
    {
        return false;
    }
    app::AppConfig& cfg = edit.config();

    if (patch.has_region)
    {
        cfg.meshtastic_config.region = patch.region;
        mesh_changed = true;
    }
    if (patch.has_channel)
    {
        cfg.chat_channel = patch.channel;
        chat_changed = true;
    }
    if (patch.has_duty_cycle)
    {
        cfg.net_duty_cycle = patch.duty_cycle;
        net_changed = true;
    }
    if (patch.has_channel_util)
    {
        cfg.net_channel_util = patch.channel_util;
        net_changed = true;
    }
    if (patch.has_aprs_enable)
    {
        cfg.aprs.enabled = patch.aprs_enable;
        aprs_changed = true;
    }
    if (patch.has_aprs_igate_callsign)
    {
        copy_cstr_field(cfg.aprs.igate_callsign, patch.aprs_igate_callsign);
        aprs_changed = true;
    }
    if (patch.has_aprs_igate_ssid)
    {
        cfg.aprs.igate_ssid = patch.aprs_igate_ssid;
        aprs_changed = true;
    }
    if (patch.has_aprs_tocall)
    {
        copy_cstr_field(cfg.aprs.tocall, patch.aprs_tocall);
        aprs_changed = true;
    }
    if (patch.has_aprs_path)
    {
        copy_cstr_field(cfg.aprs.path, patch.aprs_path);
        aprs_changed = true;
    }
    if (patch.has_aprs_tx_min_interval_s)
    {
        cfg.aprs.tx_min_interval_s = patch.aprs_tx_min_interval_s;
        aprs_changed = true;
    }
    if (patch.has_aprs_dedupe_window_s)
    {
        cfg.aprs.dedupe_window_s = patch.aprs_dedupe_window_s;
        aprs_changed = true;
    }
    if (patch.has_aprs_symbol_table)
    {
        cfg.aprs.symbol_table = patch.aprs_symbol_table;
        aprs_changed = true;
    }
    if (patch.has_aprs_symbol_code)
    {
        cfg.aprs.symbol_code = patch.aprs_symbol_code;
        aprs_changed = true;
    }
    if (patch.has_aprs_position_interval_s)
    {
        cfg.aprs.position_interval_s = patch.aprs_position_interval_s;
        aprs_changed = true;
    }
    if (patch.has_aprs_node_map)
    {
        std::memset(cfg.aprs.node_map, 0, sizeof(cfg.aprs.node_map));
        if (patch.aprs_node_map_len > 0)
        {
            std::memcpy(cfg.aprs.node_map, patch.aprs_node_map, patch.aprs_node_map_len);
        }
        cfg.aprs.node_map_len = patch.aprs_node_map_len;
        aprs_changed = true;
    }
    if (patch.has_aprs_self_enable)
    {
        cfg.aprs.self_enable = patch.aprs_self_enable;
        aprs_changed = true;
    }
    if (patch.has_aprs_self_callsign)
    {
        copy_cstr_field(cfg.aprs.self_callsign, patch.aprs_self_callsign);
        aprs_changed = true;
    }

    app::AppConfigChangeSet changes = app::AppConfigChangeSet::none();
    if (mesh_changed)
    {
        changes.mergeIn(app::AppConfigChangeSet::mesh());
    }
    if (net_changed)
    {
        changes.mergeIn(app::AppConfigChangeSet::network());
    }
    if (chat_changed)
    {
        changes.mergeIn(app::AppConfigChangeSet::chatUi());
    }
    if (aprs_changed)
    {
        changes.mergeIn(app::AppConfigChangeSet::aprs());
    }
    edit.commit(changes);

    if (protocol_changed)
    {
        if (!config_api.switchMeshProtocol(target_protocol, false))
        {
            return false;
        }
        mesh_changed = false; // backend switch already applies active mesh config
    }
    if (mesh_changed)
    {
        config_api.applyMeshConfig();
    }
    if (net_changed)
    {
        config_api.applyNetworkLimits();
    }
    if (chat_changed)
    {
        messaging_api.getChatService().switchChannel(
            static_cast<chat::ChannelId>(config_api.readConfig().chat_channel));
    }
    if (protocol_changed)
    {
        changes.mergeIn(app::AppConfigChangeSet::mesh());
    }
    config_api.requestSaveConfig(changes);
    return true;
}

bool set_time_epoch(uint64_t epoch_seconds)
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return platform::esp::idf_common::tab5_rtc_runtime::apply_system_time_and_sync_rtc(
        static_cast<time_t>(epoch_seconds), "hostlink");
#else
    timeval tv;
    tv.tv_sec = static_cast<time_t>(epoch_seconds);
    tv.tv_usec = 0;
    return settimeofday(&tv, nullptr) == 0;
#endif
}

} // namespace hostlink
