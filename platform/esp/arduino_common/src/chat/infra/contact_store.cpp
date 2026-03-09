/**
 * @file contact_store.cpp
 * @brief Contact nickname storage shell implementation
 */

#include "platform/esp/arduino_common/chat/infra/contact_store.h"
#include "../internal/blob_store_io.h"

#include <SD.h>

namespace chat
{
namespace contacts
{

ContactStore::ContactStore()
    : core_(*this)
{
}

void ContactStore::begin()
{
    core_.begin();
}

std::string ContactStore::getNickname(uint32_t node_id) const
{
    return core_.getNickname(node_id);
}

bool ContactStore::setNickname(uint32_t node_id, const char* nickname)
{
    return core_.setNickname(node_id, nickname);
}

bool ContactStore::removeNickname(uint32_t node_id)
{
    return core_.removeNickname(node_id);
}

bool ContactStore::hasNickname(const char* nickname) const
{
    return core_.hasNickname(nickname);
}

std::vector<uint32_t> ContactStore::getAllContactIds() const
{
    return core_.getAllContactIds();
}

size_t ContactStore::getCount() const
{
    return core_.getCount();
}

bool ContactStore::loadBlob(std::vector<uint8_t>& out)
{
    if (loadFromSD(out))
    {
        return true;
    }
    if (loadFromFlash(out))
    {
        return true;
    }
    out.clear();
    return false;
}

bool ContactStore::saveBlob(const uint8_t* data, size_t len)
{
    const bool sd_available = (SD.cardType() != CARD_NONE);
    if (sd_available && saveToSD(data, len))
    {
        clearFlashBackup();
        return true;
    }
    return saveToFlash(data, len);
}

bool ContactStore::loadFromSD(std::vector<uint8_t>& out) const
{
    return chat::infra::loadRawBlobFromSd(kSdPath, out);
}

bool ContactStore::saveToSD(const uint8_t* data, size_t len) const
{
    return chat::infra::saveRawBlobToSd(kSdPath, data, len);
}

bool ContactStore::loadFromFlash(std::vector<uint8_t>& out) const
{
    return chat::infra::loadRawBlobFromPreferences(kPrefNs, kPrefKey, out);
}

bool ContactStore::saveToFlash(const uint8_t* data, size_t len) const
{
    return chat::infra::saveRawBlobToPreferences(kPrefNs, kPrefKey, data, len);
}

void ContactStore::clearFlashBackup() const
{
    chat::infra::clearRawBlobFromPreferences(kPrefNs, kPrefKey);
}

} // namespace contacts
} // namespace chat