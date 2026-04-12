#include "platform/nrf52/arduino_common/chat/infra/blob_file_store.h"

#include "chat/infra/contact_store_core.h"
#include "chat/infra/node_store_blob_format.h"
#include "chat/infra/node_store_core.h"
#include "platform/nrf52/arduino_common/internal_fs_utils.h"

#include <InternalFileSystem.h>

#include <algorithm>
#include <cstring>

namespace platform::nrf52::arduino_common::chat::infra
{
namespace
{
using Adafruit_LittleFS_Namespace::FILE_O_READ;
constexpr const char* kLogTag = "[nrf52][blob_store]";

bool writeBytes(Adafruit_LittleFS_Namespace::File& file, const uint8_t* data, std::size_t len)
{
    if (!data && len != 0)
    {
        return false;
    }

    constexpr std::size_t kChunkSize = 128;
    std::size_t offset = 0;
    while (offset < len)
    {
        const std::size_t chunk = std::min(kChunkSize, len - offset);
        const std::size_t written = file.write(data + offset, chunk);
        if (written != chunk)
        {
            return false;
        }
        offset += written;
    }
    return true;
}

} // namespace

BlobFileStore::BlobFileStore(const char* path, uint32_t magic, uint16_t version)
    : path_(path),
      magic_(magic),
      version_(version)
{
}

bool BlobFileStore::ensureFs() const
{
    return path_ != nullptr && InternalFS.begin();
}

bool BlobFileStore::loadBlob(std::vector<uint8_t>& out)
{
    out.clear();
    if (!path_ || !ensureFs())
    {
        return false;
    }

    if (!InternalFS.exists(path_))
    {
        return false;
    }

    auto file = InternalFS.open(path_, FILE_O_READ);
    if (!file)
    {
        return false;
    }

    const uint32_t size = file.size();
    if (size == 0)
    {
        file.close();
        return false;
    }

    if (size < sizeof(FileHeader))
    {
        out.resize(size);
        const int read = file.read(out.data(), static_cast<uint16_t>(size));
        file.close();
        if (read != static_cast<int>(size))
        {
            out.clear();
            clearBlob();
            return false;
        }
        return true;
    }

    FileHeader header{};
    if (file.read(&header, sizeof(header)) != sizeof(header))
    {
        file.close();
        clearBlob();
        return false;
    }

    if (header.magic != magic_)
    {
        file.close();
        out.resize(size);
        auto legacy = InternalFS.open(path_, FILE_O_READ);
        if (!legacy)
        {
            out.clear();
            return false;
        }
        const int read = legacy.read(out.data(), static_cast<uint16_t>(size));
        legacy.close();
        if (read != static_cast<int>(size))
        {
            out.clear();
            clearBlob();
            return false;
        }
        return true;
    }

    if (header.version != version_ || header.payload_len != (size - sizeof(FileHeader)))
    {
        file.close();
        clearBlob();
        return false;
    }

    out.resize(header.payload_len);
    const int read = header.payload_len > 0 ? file.read(out.data(), static_cast<uint16_t>(header.payload_len)) : 0;
    file.close();
    if (read != static_cast<int>(header.payload_len) ||
        computeCrc(out.data(), out.size()) != header.crc)
    {
        out.clear();
        clearBlob();
        return false;
    }

    return true;
}

bool BlobFileStore::saveBlob(const uint8_t* data, size_t len)
{
    if (!path_ || !::platform::nrf52::arduino_common::internal_fs::ensureMounted(false, kLogTag))
    {
        return false;
    }

    if (!data || len == 0)
    {
        clearBlob();
        return true;
    }

    Adafruit_LittleFS_Namespace::File file(InternalFS);
    if (!::platform::nrf52::arduino_common::internal_fs::openForOverwrite(path_, &file, false, kLogTag))
    {
        return false;
    }
    if (!::platform::nrf52::arduino_common::internal_fs::rewindForOverwrite(file))
    {
        file.close();
        return false;
    }

    FileHeader header{};
    header.magic = magic_;
    header.version = version_;
    header.payload_len = static_cast<uint32_t>(len);
    header.crc = computeCrc(data, len);

    uint32_t final_size = static_cast<uint32_t>(sizeof(header) + len);
    bool ok = writeBytes(file, reinterpret_cast<const uint8_t*>(&header), sizeof(header));
    if (ok && len > 0)
    {
        ok = writeBytes(file, data, len);
    }
    const bool trunc_ok = ok && ::platform::nrf52::arduino_common::internal_fs::truncateAfterWrite(file, final_size);
    file.flush();
    file.close();
    return ok && trunc_ok;
}

void BlobFileStore::clearBlob()
{
    if (!path_ || !ensureFs())
    {
        return;
    }
    ::platform::nrf52::arduino_common::internal_fs::removeIfExists(path_);
}

uint32_t BlobFileStore::computeCrc(const uint8_t* data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    if (!data || len == 0)
    {
        return ~crc;
    }

    for (size_t i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc & 1U) ? ((crc >> 1) ^ 0xEDB88320U) : (crc >> 1);
        }
    }
    return ~crc;
}

bool NodeBlobFileStore::loadBlob(std::vector<uint8_t>& out)
{
    if (!store_.loadBlob(out))
    {
        return false;
    }

    if (!::chat::contacts::isValidNodeBlobSize(out.size()) ||
        ::chat::contacts::nodeBlobEntryCount(out.size()) > ::chat::contacts::NodeStoreCore::kMaxNodes)
    {
        out.clear();
        store_.clearBlob();
        return false;
    }

    return true;
}

bool ContactBlobFileStore::loadBlob(std::vector<uint8_t>& out)
{
    if (!store_.loadBlob(out))
    {
        return false;
    }

    if ((out.size() % ::chat::contacts::ContactStoreCore::kSerializedEntrySize) != 0 ||
        (out.size() / ::chat::contacts::ContactStoreCore::kSerializedEntrySize) > ::chat::contacts::ContactStoreCore::kMaxContacts)
    {
        out.clear();
        store_.clearBlob();
        return false;
    }

    return true;
}

} // namespace platform::nrf52::arduino_common::chat::infra
