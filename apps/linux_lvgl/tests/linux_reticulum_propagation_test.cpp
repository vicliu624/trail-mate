#include "chat/infra/mesh_peer_directory_core.h"
#include "chat/linux_reticulum_propagation.h"
#include <cassert>

struct Store final : chat::IMeshPeerDirectoryBlobStore
{
    chat::MeshPeerDirectoryBlobLoadResult loadBlob(std::vector<uint8_t>&) override { return chat::MeshPeerDirectoryBlobLoadResult::Missing; }
    bool saveBlob(const uint8_t*, size_t) override { return true; }
    void clearBlob() override {}
};
int main()
{
    Store store;
    chat::MeshPeerDirectoryCore directory(store);
    assert(directory.begin().succeeded());
    trailmate::linux_app::LinuxReticulumPropagation owner(directory);
    using Event = trailmate::linux_app::LinuxReticulumIngress::PropagationAnnounce;
    Event first;
    first.destination[0] = 1;
    first.delivery[0] = 2;
    first.identity[0] = 3;
    first.public_key[0] = 4;
    first.data.valid = first.data.node_active = true;
    first.data.stamp_cost = 12;
    first.hops = 2;
    first.received_ms = 0xfffffff0;
    first.received_s = 100;
    chat::reticulum::LxmfPropagationClientConfig config;
    owner.observe(first);
    const auto* selected = owner.select(config, true, 25);
    assert(selected && selected->propagation_hash[0] == 1 && selected->stamp_cost == 12);
    assert(!owner.select(config, false, 25));
    config.enabled = false;
    assert(!owner.select(config, true, 25));
    config.enabled = true;
    auto second = first;
    second.destination[0] = 5;
    second.delivery[0] = 6;
    second.identity[0] = 7;
    second.hops = 1;
    second.received_ms = 20;
    second.received_s = 1; // UTC corrections do not change local freshness.
    owner.observe(second);
    assert(owner.select(config, true, 25)->propagation_hash[0] == 5);
    config.automatic_node = false;
    config.node_hash[0] = 1;
    assert(owner.select(config, true, 25)->propagation_hash[0] == 1);
    config.node_hash[0] = 9;
    assert(!owner.select(config, true, 25));
    config.automatic_node = true;
    chat::MeshPeerRecord ignored;
    ignored.valid = true;
    ignored.identity = chat::makeMeshPeerReticulumIdentity(chat::makeReticulumPeerIdentity(second.delivery.data(), second.identity.data()));
    ignored.flags.ignored = true;
    assert(directory.record(ignored).succeeded());
    assert(directory.setUserFlags(ignored.identity, ignored.flags).succeeded());
    assert(owner.select(config, true, 25)->propagation_hash[0] == 1);
    first.data.node_active = false;
    owner.observe(first);
    assert(!owner.select(config, true, 25));
    first.data.node_active = true;
    owner.observe(first);
    assert(!owner.select(config, true, first.received_ms + 24U * 60U * 60U * 1000U + 1));
    owner.reset();
    assert(!owner.select(config, true, 25) && !owner.selected());
}
