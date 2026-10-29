#include "chat/infra/lxmf/lxmf_propagation_stamp.h"
#include "chat/linux_reticulum_stamp.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

int main()
{
    namespace protocol = chat::lxmf::runtime;
    using Stamp = trailmate::linux_app::LinuxReticulumStamp;
    std::array<uint8_t, 32> transient{}, stamp{}, threshold{};
    for (size_t i = 0; i < transient.size(); ++i) transient[i] = uint8_t(i);
    threshold[0] = 0x80;
    assert(protocol::propagationStampMeetsTarget(threshold.data(), 1));
    threshold[31] = 1;
    assert(!protocol::propagationStampMeetsTarget(threshold.data(), 1));
    threshold.fill(0);
    threshold[31] = 2;
    assert(protocol::propagationStampMeetsTarget(threshold.data(), 255));
    threshold[31] = 3;
    assert(!protocol::propagationStampMeetsTarget(threshold.data(), 255));
    assert(protocol::propagationStampMeetsTarget(threshold.data(), 0));
    assert(!protocol::propagationStampMeetsTarget(nullptr, 0));
    Stamp owner;
    assert(owner.state() == Stamp::State::Idle && !owner.take(stamp.data()));
    assert(owner.begin(transient.data(), 12));
    assert(owner.poll(0, 0) == Stamp::State::Expanding && owner.expandedRounds() == 0);
    assert(owner.poll(3, 0) == Stamp::State::Expanding && owner.expandedRounds() == 3);
    owner.reset();
    assert(owner.state() == Stamp::State::Idle && !owner.take(stamp.data()));
    assert(owner.begin(transient.data(), 12));
    unsigned iterations = 0;
    while (owner.state() == Stamp::State::Expanding || owner.state() == Stamp::State::Searching)
    {
        assert(++iterations < 100000);
        owner.poll(16, 256);
    }
    assert(owner.state() == Stamp::State::Complete && owner.expandedRounds() == protocol::kPropagationStampExpandRounds && owner.searchRounds());
    assert(owner.take(stamp.data()) && owner.state() == Stamp::State::Idle && !owner.take(threshold.data()));
    std::printf("{\"cost\":12,\"transient\":\"");
    for (auto byte : transient) std::printf("%02x", byte);
    std::printf("\",\"stamp\":\"");
    for (auto byte : stamp) std::printf("%02x", byte);
    std::printf("\"}\n");
}
