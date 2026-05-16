#pragma once

#include <cstddef>

namespace trailmate::headless
{

struct NodeStatusDescriptor
{
    const char* target_id = nullptr;
    const char* status_label = nullptr;
    bool enabled = false;
};

class HeadlessRuntimeDescriptorConsumer
{
  public:
    bool consume(const NodeStatusDescriptor& descriptor) noexcept;

    bool consumed() const noexcept;
    const NodeStatusDescriptor& nodeStatus() const noexcept;

  private:
    NodeStatusDescriptor node_status_{};
    bool consumed_ = false;
};

class HeadlessPageManifestConsumer
{
  public:
    bool consume(const char* manifest_id, std::size_t page_count) noexcept;

    const char* manifestId() const noexcept;
    std::size_t pageCount() const noexcept;
    bool consumed() const noexcept;

  private:
    const char* manifest_id_ = nullptr;
    std::size_t page_count_ = 0;
    bool consumed_ = false;
};

} // namespace trailmate::headless
