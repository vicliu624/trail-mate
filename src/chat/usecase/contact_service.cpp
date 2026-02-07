/**
 * @file contact_service.cpp
 * @brief Contact service implementation
 */

#include "contact_service.h"
#include "../domain/contact_types.h"
#include <Arduino.h>
#include <algorithm>
#include <cstring>
#include <ctime>

namespace chat
{
namespace contacts
{

ContactService::ContactService(INodeStore& node_store, IContactStore& contact_store)
    : node_store_(node_store), contact_store_(contact_store), cache_timestamp_(0)
{
}

void ContactService::begin()
{
    node_store_.begin();
    contact_store_.begin();
    invalidateCache();
}

void ContactService::updateNodeInfo(uint32_t node_id, const char* short_name, const char* long_name,
                                    float snr, float rssi, uint32_t now_secs, uint8_t protocol, uint8_t role,
                                    uint8_t hops_away)
{
    Serial.printf("[ContactService] updateNodeInfo node=%08lX snr=%.1f rssi=%.1f ts=%lu\n",
                  (unsigned long)node_id,
                  snr,
                  rssi,
                  (unsigned long)now_secs);
    node_store_.upsert(node_id, short_name, long_name, now_secs, snr, rssi, protocol, role, hops_away);
    invalidateCache();
}

void ContactService::updateNodeProtocol(uint32_t node_id, uint8_t protocol, uint32_t now_secs)
{
    node_store_.updateProtocol(node_id, protocol, now_secs);
    invalidateCache();
}

void ContactService::updateNodePosition(uint32_t node_id, const NodePosition& pos)
{
    positions_[node_id] = pos;
    invalidateCache();
}

std::string ContactService::getContactName(uint32_t node_id) const
{
    std::string nickname = contact_store_.getNickname(node_id);
    if (!nickname.empty())
    {
        return nickname;
    }

    // Fallback to short_name from NodeStore
    buildCache();
    for (const auto& node : cached_nodes_)
    {
        if (node.node_id == node_id)
        {
            return std::string(node.short_name);
        }
    }

    // Not found in cache, try NodeStore directly
    const auto& entries = node_store_.getEntries();
    for (const auto& entry : entries)
    {
        if (entry.node_id == node_id)
        {
            return std::string(entry.short_name);
        }
    }

    // Not found, return empty
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
        if (!node.is_contact && isNodeVisible(node.last_seen))
        {
            nearby.push_back(node);
        }
    }
    return nearby;
}

bool ContactService::addContact(uint32_t node_id, const char* nickname)
{
    if (contact_store_.setNickname(node_id, nickname))
    {
        invalidateCache();
        return true;
    }
    return false;
}

bool ContactService::editContact(uint32_t node_id, const char* nickname)
{
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
    uint32_t now_ms = millis();
    if (cache_timestamp_ != 0 && (now_ms - cache_timestamp_) < kCacheTimeoutMs)
    {
        return; // Cache still valid
    }

    cached_nodes_.clear();

    // Get all contact IDs
    std::vector<uint32_t> contact_ids = contact_store_.getAllContactIds();

    // Build node info from NodeStore entries
    const auto& node_entries = node_store_.getEntries();
    for (const auto& entry : node_entries)
    {
        // Check if node is visible (within 6 days)
        if (!isNodeVisible(entry.last_seen))
        {
            continue; // Skip nodes older than 6 days
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
        info.protocol = static_cast<NodeProtocolType>(entry.protocol);
        info.role = static_cast<NodeRoleType>(entry.role);
        auto pos_it = positions_.find(entry.node_id);
        if (pos_it != positions_.end())
        {
            info.position = pos_it->second;
        }

        // Check if this node is a contact
        info.is_contact = std::find(contact_ids.begin(), contact_ids.end(), entry.node_id) != contact_ids.end();

        // Set display name
        if (info.is_contact)
        {
            info.display_name = contact_store_.getNickname(entry.node_id);
        }
        else
        {
            info.display_name = std::string(info.short_name);
        }

        cached_nodes_.push_back(info);
    }

    cache_timestamp_ = now_ms;
}

bool ContactService::isNodeVisible(uint32_t last_seen) const
{
    (void)last_seen;
    return true;
}

std::string ContactService::formatTimeStatus(uint32_t last_seen) const
{
    uint32_t now_secs = time(nullptr);
    if (now_secs < last_seen)
    {
        return "Offline";
    }

    uint32_t age_secs = now_secs - last_seen;

    // Online: ≤ 2 minutes
    if (age_secs <= 120)
    {
        return "Online";
    }

    // Minutes: 3-59 minutes
    if (age_secs < 3600)
    {
        uint32_t minutes = age_secs / 60;
        char buf[16];
        snprintf(buf, sizeof(buf), "Seen %um", minutes);
        return std::string(buf);
    }

    // Hours: 1-23 hours
    if (age_secs < 86400)
    {
        uint32_t hours = age_secs / 3600;
        char buf[16];
        snprintf(buf, sizeof(buf), "Seen %uh", hours);
        return std::string(buf);
    }

    // Days: 1-6 days
    if (age_secs < 6 * 86400)
    {
        uint32_t days = age_secs / 86400;
        char buf[16];
        snprintf(buf, sizeof(buf), "Seen %ud", days);
        return std::string(buf);
    }

    // > 6 days: should be filtered out
    return "Offline";
}

} // namespace contacts
} // namespace chat
