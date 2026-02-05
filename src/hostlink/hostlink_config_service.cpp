#include "hostlink_config_service.h"

#include "../app/app_context.h"
#include "../board/BoardBase.h"

#include <Arduino.h>
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
} // namespace

bool build_status_payload(std::vector<uint8_t>& out, uint8_t link_state, uint32_t last_error)
{
    out.clear();
    out.reserve(64);

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
