#pragma once
#include "geocaching/usecase/gpx_install.h"
#include "platform/linux/geocaching_install_journal.h"
#include "platform/linux/geocaching_record_store.h"
#include "platform/linux/geocaching_volume_lease.h"
#include <memory>

namespace platform::linux_runtime
{
// One storage-owner task. All dependencies must outlive the port. Mutations use
// the journal's generation/volume guard and descriptor-relative no-replace I/O.
class LinuxGeocachingGpxFilePort final : public ::geocaching::GpxInstallPort
{
  public:
    using Effect = ::geocaching::InstallEffectResult;
    LinuxGeocachingGpxFilePort(LinuxGeocachingInstallJournal::TaskId task,
                               LinuxGeocachingVolumeLease&, LinuxGeocachingInstallJournal&,
                               LinuxGeocachingRecordStore&, ::geocaching::protocol::RecordCrypto&);
    ~LinuxGeocachingGpxFilePort() override;
    Effect stage(const ::geocaching::InstallIdentity&);
    Effect execute(::geocaching::InstallStep, const ::geocaching::InstallIdentity&) override;
    // Repeated bounded steps. Rollback first cancels continuing intent; cleanup
    // retains the old GPX by revision hash before finalizing its journal marker.
    Effect rollback(const ::geocaching::InstallIdentity&);
    Effect cleanup(const ::geocaching::InstallIdentity&);
    LinuxGeocachingInstallJournal::Result lastResult() const;
    // Read-only validation for background projections, independent of a pending
    // task's selected revision. Returns owned authenticated record bytes.
    static LinuxGeocachingInstallJournal::Result readInstalled(
        const LinuxGeocachingInstallJournal::InstalledReference&, LinuxGeocachingVolumeLease&,
        LinuxGeocachingRecordStore&, ::geocaching::protocol::RecordCrypto&, LinuxGeocachingRecordStore::Record&);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
