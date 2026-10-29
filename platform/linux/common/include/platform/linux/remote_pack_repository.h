#pragma once
#include "platform/ui/pack_repository_runtime.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>

namespace platform::linux_runtime
{
struct PackRepositorySource
{
    std::string catalog_url;
    std::string base_url;
};
bool remote_pack_catalog_enabled();
bool remote_pack_catalog_snapshot(std::vector<::ui::runtime::packs::PackageRecord>& packages, std::string& error);
void refresh_remote_pack_catalog();
::ui::runtime::packs::CatalogStatus remote_pack_catalog_status();
void stop_remote_pack_catalog();
bool parse_pack_catalog(const std::string& text, const std::string& base_url,
                        std::vector<::ui::runtime::packs::PackageRecord>& packages, std::string& error);
class StagedResourcePack
{
  public:
    ~StagedResourcePack();
    StagedResourcePack(const StagedResourcePack&) = delete;
    StagedResourcePack& operator=(const StagedResourcePack&) = delete;
    std::filesystem::path directory() const { return owner_ / "content"; }
    std::filesystem::path archive() const { return owner_ / "archive.zip"; }
  private:
    explicit StagedResourcePack(std::filesystem::path owner) : owner_(std::move(owner)) {}
    friend std::unique_ptr<StagedResourcePack> stage_remote_pack(
        const ::ui::runtime::packs::PackageRecord&, const std::filesystem::path&,
        const std::filesystem::path&, std::string&, const std::atomic_bool*, std::function<void(int)>);
    std::filesystem::path owner_;
};
std::unique_ptr<StagedResourcePack> stage_remote_pack(
    const ::ui::runtime::packs::PackageRecord& package, const std::filesystem::path& cache_root,
    const std::filesystem::path& installed_root, std::string& error,
    const std::atomic_bool* cancelled = nullptr, std::function<void(int)> progress = {});
}
