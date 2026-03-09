#include "hostlink/hostlink_config_codec.h"

#include <cstring>

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
    size_t len = std::strlen(value);
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

uint16_t read_u16_le(const uint8_t* data)
{
    return static_cast<uint16_t>(data[0] | (data[1] << 8));
}

bool copy_string_field(char* dest, size_t dest_size, const uint8_t* src, size_t src_len)
{
    if (!dest || dest_size == 0 || src_len >= dest_size)
    {
        return false;
    }
    std::memset(dest, 0, dest_size);
    if (src_len > 0)
    {
        std::memcpy(dest, src, src_len);
    }
    dest[src_len] = '\0';
    return true;
}

} // namespace

bool encode_status_payload(std::vector<uint8_t>& out,
                           const StatusPayloadSnapshot& snapshot,
                           bool include_config)
{
    out.clear();
    out.reserve(96);

    const uint8_t charging = snapshot.charging ? 1 : 0;
    const uint8_t duty_cycle = snapshot.duty_cycle ? 1 : 0;

    push_tlv_val(out, static_cast<uint8_t>(StatusKey::Battery), snapshot.battery);
    push_tlv_val(out, static_cast<uint8_t>(StatusKey::Charging), charging);
    push_tlv_val(out, static_cast<uint8_t>(StatusKey::LinkState), snapshot.link_state);
    push_tlv_val(out, static_cast<uint8_t>(StatusKey::MeshProtocol), snapshot.mesh_protocol);
    push_tlv_val(out, static_cast<uint8_t>(StatusKey::Region), snapshot.region);
    push_tlv_val(out, static_cast<uint8_t>(StatusKey::Channel), snapshot.channel);
    push_tlv_val(out, static_cast<uint8_t>(StatusKey::DutyCycle), duty_cycle);
    push_tlv_val(out, static_cast<uint8_t>(StatusKey::ChannelUtil), snapshot.channel_util);
    push_tlv_val(out, static_cast<uint8_t>(StatusKey::LastError), snapshot.last_error);
    push_tlv_val(out, static_cast<uint8_t>(StatusKey::AppRxTotal), snapshot.app_rx.total);
    push_tlv_val(out, static_cast<uint8_t>(StatusKey::AppRxFromIs), snapshot.app_rx.from_is);
    push_tlv_val(out, static_cast<uint8_t>(StatusKey::AppRxDirect), snapshot.app_rx.direct);
    push_tlv_val(out, static_cast<uint8_t>(StatusKey::AppRxRelayed), snapshot.app_rx.relayed);

    if (!include_config)
    {
        return true;
    }

    const AprsConfigSnapshot& aprs = snapshot.aprs;
    const uint8_t enabled = aprs.enabled ? 1 : 0;
    const uint8_t self_enable = aprs.self_enable ? 1 : 0;

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
    push_tlv_val(out, static_cast<uint8_t>(StatusKey::AprsSelfEnable), self_enable);
    push_tlv_string(out, static_cast<uint8_t>(StatusKey::AprsSelfCallsign), aprs.self_callsign);
    return true;
}

bool decode_config_patch(const uint8_t* data, size_t len, ConfigPatch& out)
{
    if (!data || len == 0)
    {
        return false;
    }

    out = ConfigPatch{};
    size_t off = 0;
    while (off + 2 <= len)
    {
        const uint8_t key = data[off++];
        const uint8_t value_len = data[off++];
        if (off + value_len > len)
        {
            return false;
        }

        const uint8_t* value = &data[off];
        switch (static_cast<ConfigKey>(key))
        {
        case ConfigKey::MeshProtocol:
            if (value_len != 1)
            {
                return false;
            }
            out.has_mesh_protocol = true;
            out.mesh_protocol = value[0];
            break;
        case ConfigKey::Region:
            if (value_len != 1)
            {
                return false;
            }
            out.has_region = true;
            out.region = value[0];
            break;
        case ConfigKey::Channel:
            if (value_len != 1)
            {
                return false;
            }
            out.has_channel = true;
            out.channel = value[0];
            break;
        case ConfigKey::DutyCycle:
            if (value_len != 1)
            {
                return false;
            }
            out.has_duty_cycle = true;
            out.duty_cycle = (value[0] != 0);
            break;
        case ConfigKey::ChannelUtil:
            if (value_len != 1)
            {
                return false;
            }
            out.has_channel_util = true;
            out.channel_util = value[0];
            break;
        case ConfigKey::AprsEnable:
            if (value_len != 1)
            {
                return false;
            }
            out.has_aprs_enable = true;
            out.aprs_enable = (value[0] != 0);
            break;
        case ConfigKey::AprsIgateCallsign:
            if (!copy_string_field(out.aprs_igate_callsign, sizeof(out.aprs_igate_callsign), value, value_len))
            {
                return false;
            }
            out.has_aprs_igate_callsign = true;
            break;
        case ConfigKey::AprsIgateSsid:
            if (value_len != 1)
            {
                return false;
            }
            out.has_aprs_igate_ssid = true;
            out.aprs_igate_ssid = value[0];
            break;
        case ConfigKey::AprsToCall:
            if (!copy_string_field(out.aprs_tocall, sizeof(out.aprs_tocall), value, value_len))
            {
                return false;
            }
            out.has_aprs_tocall = true;
            break;
        case ConfigKey::AprsPath:
            if (!copy_string_field(out.aprs_path, sizeof(out.aprs_path), value, value_len))
            {
                return false;
            }
            out.has_aprs_path = true;
            break;
        case ConfigKey::AprsTxMinIntervalSec:
            if (value_len != 2)
            {
                return false;
            }
            out.has_aprs_tx_min_interval_s = true;
            out.aprs_tx_min_interval_s = read_u16_le(value);
            break;
        case ConfigKey::AprsDedupeWindowSec:
            if (value_len != 2)
            {
                return false;
            }
            out.has_aprs_dedupe_window_s = true;
            out.aprs_dedupe_window_s = read_u16_le(value);
            break;
        case ConfigKey::AprsSymbolTable:
            if (value_len != 1)
            {
                return false;
            }
            out.has_aprs_symbol_table = true;
            out.aprs_symbol_table = static_cast<char>(value[0]);
            break;
        case ConfigKey::AprsSymbolCode:
            if (value_len != 1)
            {
                return false;
            }
            out.has_aprs_symbol_code = true;
            out.aprs_symbol_code = static_cast<char>(value[0]);
            break;
        case ConfigKey::AprsPositionIntervalSec:
            if (value_len != 2)
            {
                return false;
            }
            out.has_aprs_position_interval_s = true;
            out.aprs_position_interval_s = read_u16_le(value);
            break;
        case ConfigKey::AprsNodeIdMap:
            if (value_len > sizeof(out.aprs_node_map))
            {
                return false;
            }
            out.has_aprs_node_map = true;
            out.aprs_node_map_len = value_len;
            if (value_len > 0)
            {
                std::memcpy(out.aprs_node_map, value, value_len);
            }
            break;
        case ConfigKey::AprsSelfEnable:
            if (value_len != 1)
            {
                return false;
            }
            out.has_aprs_self_enable = true;
            out.aprs_self_enable = (value[0] != 0);
            break;
        case ConfigKey::AprsSelfCallsign:
            if (!copy_string_field(out.aprs_self_callsign, sizeof(out.aprs_self_callsign), value, value_len))
            {
                return false;
            }
            out.has_aprs_self_callsign = true;
            break;
        default:
            return false;
        }

        off += value_len;
    }

    return off == len;
}

} // namespace hostlink
