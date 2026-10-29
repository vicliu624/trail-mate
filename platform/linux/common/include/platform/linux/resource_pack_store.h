#pragma once

#include "platform/ui/pack_repository_runtime.h"
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace platform::linux_runtime
{
// One self-contained directory snapshot is published with rename/exchange.
// The optional checkpoint runs after durable staging and before publication.
class ResourcePackStore
{
  public:
    explicit ResourcePackStore(std::filesystem::path root,
                               std::function<void()> before_publish = {},
                               std::function<bool()> cancelled = {});
    bool installed(std::vector<::ui::runtime::packs::InstalledPackageRecord>& out, std::string& error) const;
    bool install(const ::ui::runtime::packs::PackageRecord& package, std::string& error);
    bool uninstall(const std::string& id, std::string& error);

  private:
    bool transact(const ::ui::runtime::packs::PackageRecord* package, const std::string& id, std::string& error);
    std::filesystem::path root_;
    std::function<void()> before_publish_;
    std::function<bool()> cancelled_;
};
} // namespace platform::linux_runtime
