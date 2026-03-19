#pragma once

#include "chat/ports/i_contact_blob_store.h"
#include "chat/ports/i_node_blob_store.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace platform::nrf52::arduino_common::chat::infra
{

class BlobFileStore
{
  public:
    BlobFileStore(const char* path, uint32_t magic, uint16_t version);

    bool loadBlob(std::vector<uint8_t>& out);
    bool saveBlob(const uint8_t* data, size_t len);
    void clearBlob();

  private:
    struct FileHeader
    {
        uint32_t magic = 0;
        uint16_t version = 0;
        uint16_t reserved = 0;
        uint32_t payload_len = 0;
        uint32_t crc = 0;
    } __attribute__((packed));

    bool ensureFs() const;
    static uint32_t computeCrc(const uint8_t* data, size_t len);

    const char* path_ = nullptr;
    uint32_t magic_ = 0;
    uint16_t version_ = 0;
};

class NodeBlobFileStore final : public ::chat::contacts::INodeBlobStore
{
  public:
    explicit NodeBlobFileStore(const char* path)
        : store_(path, 0x444F4E43UL, 1)
    {
    }

    bool loadBlob(std::vector<uint8_t>& out) override;
    bool saveBlob(const uint8_t* data, size_t len) override { return store_.saveBlob(data, len); }
    void clearBlob() override { store_.clearBlob(); }

  private:
    BlobFileStore store_;
};

class ContactBlobFileStore final : public ::chat::IContactBlobStore
{
  public:
    explicit ContactBlobFileStore(const char* path)
        : store_(path, 0x544E4F43UL, 1)
    {
    }

    bool loadBlob(std::vector<uint8_t>& out) override;
    bool saveBlob(const uint8_t* data, size_t len) override { return store_.saveBlob(data, len); }
    void clearBlob() { store_.clearBlob(); }

  private:
    BlobFileStore store_;
};

} // namespace platform::nrf52::arduino_common::chat::infra
