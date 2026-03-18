#include "platform/nrf52/arduino_common/chat/infra/blob_file_store.h"

#include <map>
#include <string>

namespace platform::nrf52::arduino_common::chat::infra
{
namespace
{

std::map<std::string, std::vector<uint8_t>>& blobStore()
{
    static std::map<std::string, std::vector<uint8_t>> store;
    return store;
}

} // namespace

BlobFileStore::BlobFileStore(const char* path)
    : path_(path)
{
}

bool BlobFileStore::ensureFs() const
{
    return path_ != nullptr;
}

bool BlobFileStore::loadBlob(std::vector<uint8_t>& out)
{
    out.clear();
    if (!path_ || !ensureFs())
    {
        return false;
    }

    auto it = blobStore().find(path_);
    if (it == blobStore().end() || it->second.empty())
    {
        return false;
    }

    out = it->second;
    return true;
}

bool BlobFileStore::saveBlob(const uint8_t* data, size_t len)
{
    if (!path_ || !ensureFs())
    {
        return false;
    }

    if (!data || len == 0)
    {
        clearBlob();
        return true;
    }

    blobStore()[path_] = std::vector<uint8_t>(data, data + len);
    return true;
}

void BlobFileStore::clearBlob()
{
    if (!path_ || !ensureFs())
    {
        return;
    }
    blobStore().erase(path_);
}

} // namespace platform::nrf52::arduino_common::chat::infra
