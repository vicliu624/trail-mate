#include "chat/infra/contact_store_core.h"

#include <cstring>

namespace chat
{
namespace contacts
{

namespace
{

bool isEmptyNickname(const char* nickname)
{
    return nickname == nullptr || nickname[0] == '\0';
}

} // namespace

ContactStoreCore::ContactStoreCore(IContactBlobStore& blob_store)
    : blob_store_(blob_store)
{
}

void ContactStoreCore::begin()
{
    if (!loadEntries())
    {
        entries_.clear();
    }
}

std::string ContactStoreCore::getNickname(uint32_t node_id) const
{
    for (const auto& entry : entries_)
    {
        if (entry.node_id == node_id)
        {
            return std::string(entry.nickname);
        }
    }
    return std::string();
}

bool ContactStoreCore::setNickname(uint32_t node_id, const char* nickname)
{
    if (isEmptyNickname(nickname))
    {
        return false;
    }

    if (strlen(nickname) > 12)
    {
        return false;
    }

    for (const auto& entry : entries_)
    {
        if (entry.node_id != node_id && strcmp(entry.nickname, nickname) == 0)
        {
            return false;
        }
    }

    for (auto& entry : entries_)
    {
        if (entry.node_id == node_id)
        {
            strncpy(entry.nickname, nickname, sizeof(entry.nickname) - 1);
            entry.nickname[sizeof(entry.nickname) - 1] = '\0';
            return saveEntries();
        }
    }

    if (entries_.size() >= kMaxContacts)
    {
        return false;
    }

    Entry entry{};
    entry.node_id = node_id;
    strncpy(entry.nickname, nickname, sizeof(entry.nickname) - 1);
    entry.nickname[sizeof(entry.nickname) - 1] = '\0';
    entries_.push_back(entry);
    return saveEntries();
}

bool ContactStoreCore::removeNickname(uint32_t node_id)
{
    for (auto it = entries_.begin(); it != entries_.end(); ++it)
    {
        if (it->node_id == node_id)
        {
            entries_.erase(it);
            return saveEntries();
        }
    }
    return false;
}

bool ContactStoreCore::hasNickname(const char* nickname) const
{
    if (isEmptyNickname(nickname))
    {
        return false;
    }

    for (const auto& entry : entries_)
    {
        if (strcmp(entry.nickname, nickname) == 0)
        {
            return true;
        }
    }
    return false;
}

std::vector<uint32_t> ContactStoreCore::getAllContactIds() const
{
    std::vector<uint32_t> ids;
    ids.reserve(entries_.size());
    for (const auto& entry : entries_)
    {
        ids.push_back(entry.node_id);
    }
    return ids;
}

size_t ContactStoreCore::getCount() const
{
    return entries_.size();
}

bool ContactStoreCore::loadEntries()
{
    std::vector<uint8_t> blob;
    if (!blob_store_.loadBlob(blob))
    {
        return false;
    }
    return decodeEntries(blob.data(), blob.size());
}

bool ContactStoreCore::saveEntries()
{
    std::vector<uint8_t> blob;
    encodeEntries(blob);
    return blob_store_.saveBlob(blob.data(), blob.size());
}

bool ContactStoreCore::decodeEntries(const uint8_t* data, size_t len)
{
    if (!data)
    {
        return len == 0;
    }

    if (len == 0)
    {
        entries_.clear();
        return true;
    }

    if ((len % kSerializedEntrySize) != 0)
    {
        return false;
    }

    size_t count = len / kSerializedEntrySize;
    if (count > kMaxContacts)
    {
        count = kMaxContacts;
    }

    entries_.resize(count);
    memcpy(entries_.data(), data, count * kSerializedEntrySize);
    for (auto& entry : entries_)
    {
        entry.nickname[sizeof(entry.nickname) - 1] = '\0';
    }
    return true;
}

void ContactStoreCore::encodeEntries(std::vector<uint8_t>& out) const
{
    out.clear();
    if (entries_.empty())
    {
        return;
    }

    out.resize(entries_.size() * kSerializedEntrySize);
    memcpy(out.data(), entries_.data(), out.size());
}

} // namespace contacts
} // namespace chat
