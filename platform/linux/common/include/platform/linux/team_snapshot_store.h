#pragma once
#include "platform/ui/team_ui_snapshot_store.h"
#include <filesystem>

namespace platform::linux_runtime
{
class TeamSnapshotStore final : public team::ui::ITeamUiSnapshotStore
{
  public:
    explicit TeamSnapshotStore(std::filesystem::path path);
    bool load(team::ui::TeamUiSnapshot& out) override;
    void save(const team::ui::TeamUiSnapshot& value) override;
    void clear() override;

  private:
    std::filesystem::path path_;
};
} // namespace platform::linux_runtime
