/**
 * @file contact_store.cpp
 * @brief Contact nickname storage implementation
 */

#include "contact_store.h"
#include <cstdio>
#include <cstring>

namespace chat
{
namespace contacts
{

void ContactStore::begin()
{
    // Try to load from SD card first
    if (loadFromSD())
    {
        use_sd_ = true;
        return;
    }

    // Fallback to Flash
    if (loadFromFlash())
    {
        use_sd_ = false;
        return;
    }

    // No existing data, start fresh
    entries_.clear();
    use_sd_ = (SD.cardType() != CARD_NONE);
}

std::string ContactStore::getNickname(uint32_t node_id) const
{
    for (const auto& e : entries_)
    {
        if (e.node_id == node_id)
        {
            return std::string(e.nickname);
        }
    }
    return std::string();
}

bool ContactStore::setNickname(uint32_t node_id, const char* nickname)
{
    if (!nickname || strlen(nickname) == 0)
    {
        return false; // Empty nickname not allowed
    }

    if (strlen(nickname) > 12)
    {
        return false; // Too long
    }

    // Check for duplicate nickname (excluding current node_id)
    for (const auto& e : entries_)
    {
        if (e.node_id != node_id && strcmp(e.nickname, nickname) == 0)
        {
            return false; // Duplicate name
        }
    }

    // Find existing entry
    for (auto& e : entries_)
    {
        if (e.node_id == node_id)
        {
            strncpy(e.nickname, nickname, sizeof(e.nickname) - 1);
            e.nickname[sizeof(e.nickname) - 1] = '\0';
            save();
            return true;
        }
    }

    // Check capacity
    if (entries_.size() >= kMaxContacts)
    {
        return false; // Storage full
    }

    // Add new entry
    Entry e{};
    e.node_id = node_id;
    strncpy(e.nickname, nickname, sizeof(e.nickname) - 1);
    e.nickname[sizeof(e.nickname) - 1] = '\0';
    entries_.push_back(e);
    save();
    return true;
}

bool ContactStore::removeNickname(uint32_t node_id)
{
    for (auto it = entries_.begin(); it != entries_.end(); ++it)
    {
        if (it->node_id == node_id)
        {
            entries_.erase(it);
            save();
            return true;
        }
    }
    return false;
}

bool ContactStore::hasNickname(const char* nickname) const
{
    if (!nickname)
    {
        return false;
    }
    for (const auto& e : entries_)
    {
        if (strcmp(e.nickname, nickname) == 0)
        {
            return true;
        }
    }
    return false;
}

std::vector<uint32_t> ContactStore::getAllContactIds() const
{
    std::vector<uint32_t> ids;
    ids.reserve(entries_.size());
    for (const auto& e : entries_)
    {
        ids.push_back(e.node_id);
    }
    return ids;
}

bool ContactStore::loadFromSD()
{
    if (SD.cardType() == CARD_NONE)
    {
        return false;
    }

    File file = SD.open(kSdPath, FILE_READ);
    if (!file)
    {
        return false;
    }

    size_t file_size = file.size();
    if (file_size == 0 || file_size % sizeof(Entry) != 0)
    {
        file.close();
        return false;
    }

    size_t count = file_size / sizeof(Entry);
    if (count > kMaxContacts)
    {
        count = kMaxContacts;
    }

    entries_.resize(count);
    size_t read_bytes = file.read((uint8_t*)entries_.data(), count * sizeof(Entry));
    file.close();

    return (read_bytes == count * sizeof(Entry));
}

bool ContactStore::saveToSD()
{
    if (SD.cardType() == CARD_NONE)
    {
        return false;
    }

    // Remove old file if exists
    if (SD.exists(kSdPath))
    {
        SD.remove(kSdPath);
    }

    File file = SD.open(kSdPath, FILE_WRITE);
    if (!file)
    {
        return false;
    }

    if (!entries_.empty())
    {
        size_t written = file.write((uint8_t*)entries_.data(), entries_.size() * sizeof(Entry));
        file.close();
        return (written == entries_.size() * sizeof(Entry));
    }
    else
    {
        file.close();
        return true; // Empty file is valid
    }
}

bool ContactStore::loadFromFlash()
{
    Preferences prefs;
    if (!prefs.begin(kPrefNs, true))
    {
        return false;
    }

    size_t len = prefs.getBytesLength(kPrefKey);
    if (len == 0 || len % sizeof(Entry) != 0)
    {
        prefs.end();
        return false;
    }

    size_t count = len / sizeof(Entry);
    if (count > kMaxContacts)
    {
        count = kMaxContacts;
    }

    entries_.resize(count);
    size_t read_bytes = prefs.getBytes(kPrefKey, entries_.data(), count * sizeof(Entry));
    prefs.end();

    return (read_bytes == count * sizeof(Entry));
}

bool ContactStore::saveToFlash()
{
    Preferences prefs;
    if (!prefs.begin(kPrefNs, false))
    {
        return false;
    }

    if (!entries_.empty())
    {
        bool ok = prefs.putBytes(kPrefKey, entries_.data(), entries_.size() * sizeof(Entry));
        prefs.end();
        return ok;
    }
    else
    {
        prefs.remove(kPrefKey);
        prefs.end();
        return true; // Empty is valid
    }
}

void ContactStore::save()
{
    // Update storage preference based on SD card availability
    bool sd_available = (SD.cardType() != CARD_NONE);

    if (sd_available)
    {
        if (saveToSD())
        {
            use_sd_ = true;
            // Also clear Flash backup if SD is working
            if (!use_sd_)
            {
                Preferences prefs;
                if (prefs.begin(kPrefNs, false))
                {
                    prefs.remove(kPrefKey);
                    prefs.end();
                }
            }
        }
        else
        {
            // SD failed, try Flash
            if (saveToFlash())
            {
                use_sd_ = false;
            }
        }
    }
    else
    {
        // No SD, use Flash
        if (saveToFlash())
        {
            use_sd_ = false;
        }
    }
}

} // namespace contacts
} // namespace chat
