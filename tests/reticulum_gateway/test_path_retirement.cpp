#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

struct PathEntry
{
    uint8_t destination_hash[16] = {};
    uint8_t cached_packet_hash[32] = {};
    uint8_t interface_id = 0;
};
struct ReverseEntry
{
    uint8_t interface_id = 0;
};
struct LinkRelayEntry
{
    uint8_t initiator_interface_id = 0;
    uint8_t responder_interface_id = 0;
};
class PathManager
{
  public:
    struct Transport
    {
        std::vector<PathEntry> paths;
        std::vector<ReverseEntry> reverse_table;
        std::vector<LinkRelayEntry> link_relays;
    } transport_;
    std::vector<uint8_t> resolved, forgotten;
    void resolvePendingPathRequest(const uint8_t* hash) { resolved.push_back(hash[0]); }
    void forgetPacket(const uint8_t* hash) { forgotten.push_back(hash[0]); }
    void retireInterface(uint8_t);
};
#include "path_retirement.inc"

int main()
{
    PathManager manager;
    manager.transport_.paths = {{{1}, {11}, 35}, {{2}, {12}, 1}, {{3}, {13}, 32}};
    manager.transport_.reverse_table = {{35}, {1}, {32}};
    manager.transport_.link_relays = {{35, 1}, {32, 35}, {1, 32}};
    manager.retireInterface(0);
    assert(manager.transport_.paths.size() == 3);
    manager.retireInterface(35);
    assert(manager.transport_.paths.size() == 2);
    assert(manager.transport_.paths[0].interface_id == 1 && manager.transport_.paths[1].interface_id == 32);
    assert(manager.transport_.reverse_table.size() == 2);
    assert(manager.transport_.reverse_table[0].interface_id == 1);
    assert(manager.transport_.reverse_table[1].interface_id == 32);
    assert(manager.transport_.link_relays.size() == 1);
    assert(manager.transport_.link_relays[0].initiator_interface_id == 1);
    assert(manager.transport_.link_relays[0].responder_interface_id == 32);
    assert(manager.resolved == std::vector<uint8_t>{1});
    assert(manager.forgotten == std::vector<uint8_t>{11});
    manager.retireInterface(35);
    assert(manager.transport_.paths.size() == 2 && manager.resolved.size() == 1);
    return 0;
}
