#include "platform/linux/node_identity.h"
#include "platform/ui/settings_store.h"
#include <array>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <stdexcept>

void require(bool value)
{
    if (!value) throw std::runtime_error("node identity contract failed");
}
int main()
{
    char temporary[] = "/tmp/trailmate-identity-XXXXXX";
    require(mkdtemp(temporary));
    const std::filesystem::path root(temporary), first = root / "first", second = root / "second";
    unsetenv("TRAIL_MATE_NODE_ID");
    setenv("TRAIL_MATE_SETTINGS_ROOT", first.c_str(), 1);
    platform::ui::settings_store::put_string("preserve", "value", "existing configuration");
    std::array<std::future<uint32_t>, 6> workers;
    for (auto& worker : workers) worker = std::async(std::launch::async, []
                                                     { std::string error; const auto id=platform::linux_runtime::load_or_create_node_id(error); require(error.empty()); return id; });
    const auto id = workers[0].get();
    require(id && id != 0xFFFFFFFF);
    for (unsigned index = 1; index < workers.size(); ++index) require(workers[index].get() == id);
    std::string error;
    require(platform::linux_runtime::load_or_create_node_id(error) == id);
    setenv("TRAIL_MATE_SETTINGS_ROOT", second.c_str(), 1);
    const auto other = platform::linux_runtime::load_or_create_node_id(error);
    require(other && other != id);
    setenv("TRAIL_MATE_NODE_ID", "0x12345678", 1);
    require(platform::linux_runtime::load_or_create_node_id(error) == 0x12345678);
    setenv("TRAIL_MATE_NODE_ID", "0xffffffff", 1);
    require(platform::linux_runtime::load_or_create_node_id(error) == 0 && !error.empty());
    unsetenv("TRAIL_MATE_NODE_ID");
    require(platform::linux_runtime::load_or_create_node_id(error) == other);
    setenv("TRAIL_MATE_SETTINGS_ROOT", first.c_str(), 1);
    require(platform::linux_runtime::load_or_create_node_id(error) == id);
    std::string preserved;
    require(platform::ui::settings_store::get_string("preserve", "value", preserved) && preserved == "existing configuration");
    std::filesystem::remove_all(root);
}
