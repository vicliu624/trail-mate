#include "ui_headless_runtime/headless_descriptor_consumer.h"

#include <cassert>
#include <cstring>

int main()
{
    trailmate::headless::HeadlessRuntimeDescriptorConsumer runtime_consumer;
    trailmate::headless::NodeStatusDescriptor status{
        "gat562_mesh_evb_pro",
        "node status",
        true,
    };

    assert(runtime_consumer.consume(status));
    assert(runtime_consumer.consumed());
    assert(std::strcmp(runtime_consumer.nodeStatus().target_id,
                       "gat562_mesh_evb_pro") == 0);
    assert(std::strcmp(runtime_consumer.nodeStatus().status_label,
                       "node status") == 0);
    assert(runtime_consumer.nodeStatus().enabled);

    trailmate::headless::HeadlessPageManifestConsumer page_consumer;
    assert(page_consumer.consume("node_status_manifest", 1));
    assert(page_consumer.consumed());
    assert(std::strcmp(page_consumer.manifestId(), "node_status_manifest") == 0);
    assert(page_consumer.pageCount() == 1);

    assert(!runtime_consumer.consume({}));
    assert(!runtime_consumer.consumed());
    assert(!page_consumer.consume(nullptr, 0));
    assert(!page_consumer.consumed());
    return 0;
}
