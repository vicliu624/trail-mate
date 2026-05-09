#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace trailmate::linux_app
{
class LinuxAppServices;
}

namespace trailmate::uconsole
{

struct ConversationPreview
{
    std::string title{};
    std::string preview{};
    std::string meta{};
    int unread = 0;
};

struct ContactPreview
{
    std::string name{};
    std::string node_id{};
    std::string status{};
    std::string protocol{};
};

struct UConsoleDashboardSnapshot
{
    std::size_t conversation_count = 0;
    int unread_count = 0;
    std::size_t contact_count = 0;
    std::size_t nearby_count = 0;
    std::size_t ignored_count = 0;
    std::string mesh_protocol{};
    std::string self_node{};
    std::vector<ConversationPreview> conversations{};
    std::vector<ContactPreview> contacts{};
    std::vector<std::string> capability_lines{};
};

class UConsoleDashboardModel final
{
  public:
    explicit UConsoleDashboardModel(linux_app::LinuxAppServices& services);

    [[nodiscard]] UConsoleDashboardSnapshot snapshot() const;

  private:
    linux_app::LinuxAppServices& services_;
};

} // namespace trailmate::uconsole
