#include "hostlink_config_service.h"

#include "../app/app_context.h"
#include "../board/BoardBase.h"
#include "hostlink_bridge_radio.h"

#include <Arduino.h>
#include <cstring>
#include <sys/time.h>

namespace hostlink
{

namespace
{
void push_tlv(std::vector<uint8_t>& out, uint8_t key, const void* data, size_t len)
{
    if (!data || len == 0 || len > 255)
    {
        return;
    }
    out.push_back(key);
    out.push_back(static_cast<uint8_t>(len));
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    out.insert(out.end(), bytes, bytes + len);
}

template <typename T>
void push_tlv_val(std::vector<uint8_t>& out, uint8_t key, const T& value)
{
    push_tlv(out, key, &value, sizeof(value));
}

void push_tlv_string(std::vector<uint8_t>& out, uint8_t key, const char* value)
{
    if (!value)
    {
        return;
    }
    size_t len = strlen(value);
    if (len == 0)
    {
        return;
    }
    if (len > 255)
    {
        len = 255;
    }
    push_tlv(out, key, value, len);
}
} // namespace

bool build_status_payload(std::vector<uint8_t>& out, uint8_t link_state, uint32_t last_error, bool include_config)
{
    out.clear();
    out.reserve(96);

    int battery = board.getBatteryLevel();
    uint8_t battery_u8 = (battery < 0) ? 0xFF : static_cast<uint8_t>(battery);
    uint8_t charging = board.isCharging() ? 1 : 0;

    push_tlv_val(out, static_cast<uint8_t>(StatusKey::Battery), battery_u8);
    push_tlv_val(out, static_cast<uint8_t>(StatusKey::Charging), charging);
    push_tlv_val(out, static_cast<uint8_t>(StatusKey::LinkState), link_state);

    app::AppContext& app_ctx = app::AppContext::getInstance();
    app::AppConfig& cfg = app_ctx.getConfig();
    uint8_t protocol = static_cast<uint8_t>(cfg.mesh_protocol);
    uint8_t region = cfg.mesh_config.region;
    uint8_t channel = cfg.chat_channel;
    uint8_t duty_cycle = cfg.net_duty_cycle ? 1 : 0;
    uint8_t util = cfg.net_channel_util;

    push_tlv_val(out, static_cast<uint8_t>(StatusKey::MeshProtocol), protocol);
    push_tlv_val(out, static_cast<uint8_t>(StatusKey::Region), region);
    push_tlv_val(out, static_cast<uint8_t>(StatusKey::Channel), channel);
    push_tlv_val(out, static_cast<uint8_t>(StatusKey::DutyCycle), duty_cycle);
    push_tlv_val(out, static_cast<uint8_t>(StatusKey::ChannelUtil), util);

    push_tlv_val(out, static_cast<uint8_t>(StatusKey::LastError), last_error);

    hostlink::bridge::AppRxStats stats = hostlink::bridge::get_app_rx_stats();
    push_tlv_val(out, static_cast<uint8_t>(StatusKey::AppRxTotal), stats.total);
    push_tlv_val(out, static_cast<uint8_t>(StatusKey::AppRxFromIs), stats.from_is);
    push_tlv_val(out, static_cast<uint8_t>(StatusKey::AppRxDirect), stats.direct);
    push_tlv_val(out, static_cast<uint8_t>(StatusKey::AppRxRelayed), stats.relayed);

    if (include_config)
    {
        const app::AprsConfig& aprs = cfg.aprs;
        uint8_t enabled = aprs.enabled ? 1 : 0;
        push_tlv_val(out, static_cast<uint8_t>(StatusKey::AprsEnable), enabled);
        push_tlv_string(out, static_cast<uint8_t>(StatusKey::AprsIgateCallsign), aprs.igate_callsign);
        push_tlv_val(out, static_cast<uint8_t>(StatusKey::AprsIgateSsid), aprs.igate_ssid);
        push_tlv_string(out, static_cast<uint8_t>(StatusKey::AprsToCall), aprs.tocall);
        push_tlv_string(out, static_cast<uint8_t>(StatusKey::AprsPath), aprs.path);
        push_tlv_val(out, static_cast<uint8_t>(StatusKey::AprsTxMinIntervalSec), aprs.tx_min_interval_s);
        push_tlv_val(out, static_cast<uint8_t>(StatusKey::AprsDedupeWindowSec), aprs.dedupe_window_s);
        if (aprs.symbol_table != 0)
        {
            push_tlv_val(out, static_cast<uint8_t>(StatusKey::AprsSymbolTable), aprs.symbol_table);
        }
        if (aprs.symbol_code != 0)
        {
            push_tlv_val(out, static_cast<uint8_t>(StatusKey::AprsSymbolCode), aprs.symbol_code);
        }
        push_tlv_val(out, static_cast<uint8_t>(StatusKey::AprsPositionIntervalSec), aprs.position_interval_s);
        if (aprs.node_map_len > 0)
        {
            push_tlv(out, static_cast<uint8_t>(StatusKey::AprsNodeIdMap), aprs.node_map, aprs.node_map_len);
        }
        uint8_t self_enable = aprs.self_enable ? 1 : 0;
        push_tlv_val(out, static_cast<uint8_t>(StatusKey::AprsSelfEnable), self_enable);
        push_tlv_string(out, static_cast<uint8_t>(StatusKey::AprsSelfCallsign), aprs.self_callsign);
    }
    return true;
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

    app::AppContext& app_ctx = app::AppContext::getInstance();
    app::AppConfig& cfg = app_ctx.getConfig();
    bool mesh_changed = false;
    bool net_changed = false;
    bool chat_changed = false;
    bool aprs_changed = false;

    size_t off = 0;
    while (off + 2 <= len)
    {
        uint8_t key = data[off++];
        uint8_t vlen = data[off++];
        if (off + vlen > len)
        {
            return false;
        }

        switch (static_cast<ConfigKey>(key))
        {
        case ConfigKey::MeshProtocol:
            if (vlen != 1)
            {
                return false;
            }
            cfg.mesh_protocol = static_cast<chat::MeshProtocol>(data[off]);
            mesh_changed = true;
            break;
        case ConfigKey::Region:
            if (vlen != 1)
            {
                return false;
            }
            cfg.mesh_config.region = data[off];
            mesh_changed = true;
            break;
        case ConfigKey::Channel:
            if (vlen != 1)
            {
                return false;
            }
            cfg.chat_channel = data[off];
            chat_changed = true;
            break;
        case ConfigKey::DutyCycle:
            if (vlen != 1)
            {
                return false;
            }
            cfg.net_duty_cycle = (data[off] != 0);
            net_changed = true;
            break;
        case ConfigKey::ChannelUtil:
            if (vlen != 1)
            {
                return false;
            }
            cfg.net_channel_util = data[off];
            net_changed = true;
            break;
        case ConfigKey::AprsEnable:
            if (vlen != 1)
            {
                return false;
            }
            cfg.aprs.enabled = (data[off] != 0);
            aprs_changed = true;
            break;
        case ConfigKey::AprsIgateCallsign:
            if (vlen >= sizeof(cfg.aprs.igate_callsign))
            {
                return false;
            }
            if (vlen > 0)
            {
                memcpy(cfg.aprs.igate_callsign, &data[off], vlen);
            }
            cfg.aprs.igate_callsign[vlen] = '\0';
            aprs_changed = true;
            break;
        case ConfigKey::AprsIgateSsid:
            if (vlen != 1)
            {
                return false;
            }
            cfg.aprs.igate_ssid = data[off];
            aprs_changed = true;
            break;
        case ConfigKey::AprsToCall:
            if (vlen >= sizeof(cfg.aprs.tocall))
            {
                return false;
            }
            if (vlen > 0)
            {
                memcpy(cfg.aprs.tocall, &data[off], vlen);
            }
            cfg.aprs.tocall[vlen] = '\0';
            aprs_changed = true;
            break;
        case ConfigKey::AprsPath:
            if (vlen >= sizeof(cfg.aprs.path))
            {
                return false;
            }
            if (vlen > 0)
            {
                memcpy(cfg.aprs.path, &data[off], vlen);
            }
            cfg.aprs.path[vlen] = '\0';
            aprs_changed = true;
            break;
        case ConfigKey::AprsTxMinIntervalSec:
            if (vlen != 2)
            {
                return false;
            }
            cfg.aprs.tx_min_interval_s = static_cast<uint16_t>(data[off] | (data[off + 1] << 8));
            aprs_changed = true;
            break;
        case ConfigKey::AprsDedupeWindowSec:
            if (vlen != 2)
            {
                return false;
            }
            cfg.aprs.dedupe_window_s = static_cast<uint16_t>(data[off] | (data[off + 1] << 8));
            aprs_changed = true;
            break;
        case ConfigKey::AprsSymbolTable:
            if (vlen != 1)
            {
                return false;
            }
            cfg.aprs.symbol_table = static_cast<char>(data[off]);
            aprs_changed = true;
            break;
        case ConfigKey::AprsSymbolCode:
            if (vlen != 1)
            {
                return false;
            }
            cfg.aprs.symbol_code = static_cast<char>(data[off]);
            aprs_changed = true;
            break;
        case ConfigKey::AprsPositionIntervalSec:
            if (vlen != 2)
            {
                return false;
            }
            cfg.aprs.position_interval_s = static_cast<uint16_t>(data[off] | (data[off + 1] << 8));
            aprs_changed = true;
            break;
        case ConfigKey::AprsNodeIdMap:
            if (vlen > sizeof(cfg.aprs.node_map))
            {
                return false;
            }
            if (vlen > 0)
            {
                memcpy(cfg.aprs.node_map, &data[off], vlen);
            }
            cfg.aprs.node_map_len = vlen;
            aprs_changed = true;
            break;
        case ConfigKey::AprsSelfEnable:
            if (vlen != 1)
            {
                return false;
            }
            cfg.aprs.self_enable = (data[off] != 0);
            aprs_changed = true;
            break;
        case ConfigKey::AprsSelfCallsign:
            if (vlen >= sizeof(cfg.aprs.self_callsign))
            {
                return false;
            }
            if (vlen > 0)
            {
                memcpy(cfg.aprs.self_callsign, &data[off], vlen);
            }
            cfg.aprs.self_callsign[vlen] = '\0';
            aprs_changed = true;
            break;
        default:
            return false;
        }
        off += vlen;
    }

    app_ctx.saveConfig();
    if (mesh_changed)
    {
        app_ctx.applyMeshConfig();
    }
    if (net_changed)
    {
        app_ctx.applyNetworkLimits();
    }
    if (chat_changed)
    {
        app_ctx.getChatService().switchChannel(static_cast<chat::ChannelId>(cfg.chat_channel));
    }
    (void)aprs_changed;
    return true;
}

bool set_time_epoch(uint64_t epoch_seconds)
{
    timeval tv;
    tv.tv_sec = static_cast<time_t>(epoch_seconds);
    tv.tv_usec = 0;
    return settimeofday(&tv, nullptr) == 0;
}

} // namespace hostlink
