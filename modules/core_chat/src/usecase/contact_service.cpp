/**
 * @file contact_service.cpp
 * @brief Contact service implementation
 */

#include "chat/usecase/contact_service.h"
#include "sys/clock.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <inttypes.h>

namespace chat
{
namespace contacts
{

#ifndef CONTACT_SERVICE_LOG_ENABLE
#define CONTACT_SERVICE_LOG_ENABLE 0
#endif

#if CONTACT_SERVICE_LOG_ENABLE
#define CONTACT_SERVICE_LOG(...) std::printf(__VA_ARGS__)
#else
#define CONTACT_SERVICE_LOG(...)
#endif

ContactService::ContactService(INodeStore& node_store, IContactStore& contact_store)
    : node_store_(node_store), contact_store_(contact_store), cache_timestamp_(0)
{
}

void ContactService::begin()
{
    node_store_.begin();
    contact_store_.begin();

    const std::vector<uint32_t> contact_ids = contact_store_.getAllContactIds();
    for (size_t i = 0; i < contact_ids.size(); ++i)
    {
        (void)ensureNodeExistsForContact(contact_ids[i]);
    }

    invalidateCache();
}

void ContactService::applyNodeUpdate(uint32_t node_id, const NodeUpdate& update)
{
    node_store_.applyUpdate(node_id, update);
    invalidateCache();
}

void ContactService::updateNodeInfo(uint32_t node_id, const char* short_name, const char* long_name,
                                    float snr, float rssi, uint32_t now_secs, uint8_t protocol, uint8_t role,
                                    uint8_t hops_away, uint8_t hw_model, uint8_t channel)
{
    CONTACT_SERVICE_LOG("[ContactService] updateNodeInfo node=%08lX snr=%.1f rssi=%.1f ts=%lu\n",
                        (unsigned long)node_id,
                        snr,
                        rssi,
                        (unsigned long)now_secs);
    NodeUpdate update{};
    update.short_name = short_name;
    update.long_name = long_name;
    update.has_last_seen = true;
    update.last_seen = now_secs;
    update.has_snr = !std::isnan(snr);
    update.snr = snr;
    update.has_rssi = !std::isnan(rssi);
    update.rssi = rssi;
    update.has_protocol = (protocol != 0);
    update.protocol = protocol;
    update.has_role = (role != kNodeRoleUnknown);
    update.role = role;
    update.has_hops_away = (hops_away != 0xFF);
    update.hops_away = hops_away;
    update.has_hw_model = (hw_model != 0);
    update.hw_model = hw_model;
    update.has_channel = (channel != 0xFF);
    update.channel = channel;
    applyNodeUpdate(node_id, update);
}

void ContactService::updateNodeProtocol(uint32_t node_id, uint8_t protocol, uint32_t now_secs)
{
    NodeUpdate update{};
    update.has_protocol = (protocol != 0);
    update.protocol = protocol;
    update.has_last_seen = true;
    update.last_seen = now_secs;
    applyNodeUpdate(node_id, update);
}

void ContactService::updateNodePosition(uint32_t node_id, const NodePosition& pos)
{
    NodeUpdate update{};
    update.has_position = true;
    update.position = pos;
    applyNodeUpdate(node_id, update);
}

std::string ContactService::getContactName(uint32_t node_id) const
{
    std::string nickname = contact_store_.getNickname(node_id);
    if (!nickname.empty())
    {
        return nickname;
    }

    buildCache();
    for (const auto& node : cached_nodes_)
    {
        if (node.node_id == node_id)
        {
            return std::string(node.short_name);
        }
    }

    const auto& entries = node_store_.getEntries();
    for (const auto& entry : entries)
    {
        if (entry.node_id == node_id)
        {
            return std::string(entry.short_name);
        }
    }

    return std::string();
}

std::vector<NodeInfo> ContactService::getContacts() const
{
    buildCache();
    std::vector<NodeInfo> contacts;
    for (const auto& node : cached_nodes_)
    {
        if (node.is_contact)
        {
            contacts.push_back(node);
        }
    }
    return contacts;
}

std::vector<NodeInfo> ContactService::getNearby() const
{
    buildCache();
    std::vector<NodeInfo> nearby;
    for (const auto& node : cached_nodes_)
    {
        if (!node.is_contact && !node.is_ignored && isNodeVisible(node.last_seen))
        {
            nearby.push_back(node);
        }
    }
    return nearby;
}

std::vector<NodeInfo> ContactService::getIgnoredNodes() const
{
    buildCache();
    std::vector<NodeInfo> ignored;
    for (const auto& node : cached_nodes_)
    {
        if (!node.is_contact && node.is_ignored && isNodeVisible(node.last_seen))
        {
            ignored.push_back(node);
        }
    }
    return ignored;
}

bool ContactService::addContact(uint32_t node_id, const char* nickname)
{
    if (!ensureNodeExistsForContact(node_id))
    {
        return false;
    }
    if (contact_store_.setNickname(node_id, nickname))
    {
        invalidateCache();
        return true;
    }
    return false;
}

bool ContactService::editContact(uint32_t node_id, const char* nickname)
{
    if (!ensureNodeExistsForContact(node_id))
    {
        return false;
    }
    if (contact_store_.setNickname(node_id, nickname))
    {
        invalidateCache();
        return true;
    }
    return false;
}

bool ContactService::removeContact(uint32_t node_id)
{
    if (contact_store_.removeNickname(node_id))
    {
        invalidateCache();
        return true;
    }
    return false;
}

bool ContactService::removeNode(uint32_t node_id)
{
    bool removed = false;
    if (contact_store_.removeNickname(node_id))
    {
        removed = true;
    }
    if (node_store_.remove(node_id))
    {
        removed = true;
    }
    if (removed)
    {
        invalidateCache();
    }
    return removed;
}

bool ContactService::setNodeIgnored(uint32_t node_id, bool ignored)
{
    if (!hasNodeEntry(node_id))
    {
        return false;
    }

    NodeUpdate update{};
    update.has_is_ignored = true;
    update.is_ignored = ignored;
    applyNodeUpdate(node_id, update);
    return true;
}

bool ContactService::setNodeKeyManuallyVerified(uint32_t node_id, bool verified)
{
    if (!hasNodeEntry(node_id))
    {
        return false;
    }

    NodeUpdate update{};
    update.has_key_manually_verified = true;
    update.key_manually_verified = verified;
    applyNodeUpdate(node_id, update);
    return true;
}

const NodeInfo* ContactService::getNodeInfo(uint32_t node_id) const
{
    buildCache();
    for (const auto& node : cached_nodes_)
    {
        if (node.node_id == node_id)
        {
            return &node;
        }
    }
    return nullptr;
}

void ContactService::clearCache()
{
    invalidateCache();
}

void ContactService::invalidateCache() const
{
    cache_timestamp_ = 0;
    cached_nodes_.clear();
}

void ContactService::buildCache() const
{
    uint32_t now_ms = sys::millis_now();
    if (cache_timestamp_ != 0 && (now_ms - cache_timestamp_) < kCacheTimeoutMs)
    {
        return;
    }

    cached_nodes_.clear();

    std::vector<uint32_t> contact_ids = contact_store_.getAllContactIds();

    const auto& node_entries = node_store_.getEntries();
    for (const auto& entry : node_entries)
    {
        if (!isNodeVisible(entry.last_seen))
        {
            continue;
        }

        NodeInfo info{};
        info.node_id = entry.node_id;
        strncpy(info.short_name, entry.short_name, sizeof(info.short_name) - 1);
        info.short_name[sizeof(info.short_name) - 1] = '\0';
        strncpy(info.long_name, entry.long_name, sizeof(info.long_name) - 1);
        info.long_name[sizeof(info.long_name) - 1] = '\0';
        info.last_seen = entry.last_seen;
        info.snr = entry.snr;
        info.rssi = entry.rssi;
        info.hops_away = entry.hops_away;
        info.channel = entry.channel;
        info.protocol = static_cast<NodeProtocolType>(entry.protocol);
        info.role = static_cast<NodeRoleType>(entry.role);
        info.hw_model = entry.hw_model;
        info.next_hop = entry.next_hop;
        info.has_macaddr = entry.has_macaddr;
        std::memcpy(info.macaddr, entry.macaddr, sizeof(info.macaddr));
        info.via_mqtt = entry.via_mqtt;
        info.is_ignored = entry.is_ignored;
        info.has_public_key = entry.has_public_key;
        info.key_manually_verified = entry.key_manually_verified;
        info.has_device_metrics = entry.has_device_metrics;
        info.device_metrics = entry.device_metrics;
        info.position.valid = entry.position_valid;
        info.position.latitude_i = entry.position_latitude_i;
        info.position.longitude_i = entry.position_longitude_i;
        info.position.has_altitude = entry.position_has_altitude;
        info.position.altitude = entry.position_altitude;
        info.position.timestamp = entry.position_timestamp;
        info.position.precision_bits = entry.position_precision_bits;
        info.position.pdop = entry.position_pdop;
        info.position.hdop = entry.position_hdop;
        info.position.vdop = entry.position_vdop;
        info.position.gps_accuracy_mm = entry.position_gps_accuracy_mm;

        if (info.short_name[0] == '\0')
        {
            std::snprintf(info.short_name, sizeof(info.short_name), "%04X",
                          static_cast<unsigned>(info.node_id & 0xFFFF));
        }

        info.is_contact = std::find(contact_ids.begin(), contact_ids.end(), entry.node_id) != contact_ids.end();

        if (info.is_contact)
        {
            info.display_name = contact_store_.getNickname(entry.node_id);
            if (info.display_name.empty())
            {
                info.display_name = std::string(info.short_name);
            }
        }
        else
        {
            info.display_name = std::string(info.short_name);
        }

        cached_nodes_.push_back(info);
    }

    cache_timestamp_ = now_ms;
}

bool ContactService::ensureNodeExistsForContact(uint32_t node_id)
{
    if (node_id == 0)
    {
        return false;
    }

    if (hasNodeEntry(node_id))
    {
        return true;
    }

    NodeUpdate update{};
    update.has_last_seen = true;
    update.last_seen = 0;
    applyNodeUpdate(node_id, update);
    return hasNodeEntry(node_id);
}

bool ContactService::hasNodeEntry(uint32_t node_id) const
{
    const auto& entries = node_store_.getEntries();
    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (entries[i].node_id == node_id)
        {
            return true;
        }
    }
    return false;
}

bool ContactService::isNodeVisible(uint32_t last_seen) const
{
    (void)last_seen;
    return true;
}

std::string ContactService::formatTimeStatus(uint32_t last_seen) const
{
    uint32_t now_secs = sys::epoch_seconds_now();
    if (now_secs < last_seen)
    {
        return "Offline";
    }

    uint32_t age_secs = now_secs - last_seen;

    if (age_secs <= 120)
    {
        return "Online";
    }

    if (age_secs < 3600)
    {
        uint32_t minutes = age_secs / 60;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "Seen %" PRIu32 "m", minutes);
        return std::string(buf);
    }

    if (age_secs < 86400)
    {
        uint32_t hours = age_secs / 3600;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "Seen %" PRIu32 "h", hours);
        return std::string(buf);
    }

    if (age_secs < 6 * 86400)
    {
        uint32_t days = age_secs / 86400;
        char buf[16];
        std::snprintf(buf, sizeof(buf), "Seen %" PRIu32 "d", days);
        return std::string(buf);
    }

    return "Offline";
}

} // namespace contacts
} // namespace chat
