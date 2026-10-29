#pragma once
#include "platform/linux/geocaching_install_journal.h"
#include "platform/linux/geocaching_record_store.h"
#include "ui_presentation/geocaching/geocaching_source.h"
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>

namespace platform::linux_runtime
{
// Private part of the Linux presentation source. All methods run on its storage
// thread; UI code receives only the copied bounded projection.
class GeocachingOfflineCache
{
  public:
    struct Window
    {
        std::array<::ui::geocaching::Item, 4> rows{};
        std::array<LinuxGeocachingInstallJournal::InstalledReference, 4> references{};
        LinuxGeocachingInstallJournal::Volume volume{};
        size_t offset = 0, count = 0, total = 0;
        bool ready = false;
        std::string status;
    };
    explicit GeocachingOfflineCache(std::filesystem::path storage_root);
    ~GeocachingOfflineCache();
    bool open(const std::atomic_bool& stopping);
    bool healthy() const;
    void window(size_t offset, Window&);
    bool detail(const LinuxGeocachingInstallJournal::InstalledReference&, LinuxGeocachingRecordStore::Record&, std::string& error);
    const std::string& status() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
