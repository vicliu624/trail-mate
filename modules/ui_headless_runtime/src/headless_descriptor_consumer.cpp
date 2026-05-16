#include "ui_headless_runtime/headless_descriptor_consumer.h"

namespace trailmate::headless
{

bool HeadlessRuntimeDescriptorConsumer::consume(
    const NodeStatusDescriptor& descriptor) noexcept
{
    if (descriptor.target_id == nullptr || descriptor.status_label == nullptr)
    {
        consumed_ = false;
        node_status_ = {};
        return false;
    }

    node_status_ = descriptor;
    consumed_ = true;
    return true;
}

bool HeadlessRuntimeDescriptorConsumer::consumed() const noexcept
{
    return consumed_;
}

const NodeStatusDescriptor& HeadlessRuntimeDescriptorConsumer::nodeStatus()
    const noexcept
{
    return node_status_;
}

bool HeadlessPageManifestConsumer::consume(const char* manifest_id,
                                           std::size_t page_count) noexcept
{
    if (manifest_id == nullptr)
    {
        manifest_id_ = nullptr;
        page_count_ = 0;
        consumed_ = false;
        return false;
    }

    manifest_id_ = manifest_id;
    page_count_ = page_count;
    consumed_ = true;
    return true;
}

const char* HeadlessPageManifestConsumer::manifestId() const noexcept
{
    return manifest_id_;
}

std::size_t HeadlessPageManifestConsumer::pageCount() const noexcept
{
    return page_count_;
}

bool HeadlessPageManifestConsumer::consumed() const noexcept
{
    return consumed_;
}

} // namespace trailmate::headless
