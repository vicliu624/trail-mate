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
    explicit BlobFileStore(const char* path);

    bool loadBlob(std::vector<uint8_t>& out);
    bool saveBlob(const uint8_t* data, size_t len);
    void clearBlob();

  private:
    bool ensureFs() const;

    const char* path_ = nullptr;
};

class NodeBlobFileStore final : public ::chat::contacts::INodeBlobStore
{
  public:
    explicit NodeBlobFileStore(const char* path)
        : store_(path)
    {
    }

    bool loadBlob(std::vector<uint8_t>& out) override { return store_.loadBlob(out); }
    bool saveBlob(const uint8_t* data, size_t len) override { return store_.saveBlob(data, len); }
    void clearBlob() override { store_.clearBlob(); }

  private:
    BlobFileStore store_;
};

class ContactBlobFileStore final : public ::chat::IContactBlobStore
{
  public:
    explicit ContactBlobFileStore(const char* path)
        : store_(path)
    {
    }

    bool loadBlob(std::vector<uint8_t>& out) override { return store_.loadBlob(out); }
    bool saveBlob(const uint8_t* data, size_t len) override { return store_.saveBlob(data, len); }

  private:
    BlobFileStore store_;
};

} // namespace platform::nrf52::arduino_common::chat::infra
