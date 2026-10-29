#pragma once
#include "hostlink/hostlink_types.h"
#include <vector>

namespace platform::linux_runtime
{
class HostlinkUploads;
class HostlinkPackageInstall;
class HostlinkConfigSnapshot;
class HostlinkConfigTransfer;
class HostlinkIdentityBackup;
class HostlinkRadioIdentity;
class HostlinkTrackCatalog;
class HostlinkTrackSnapshots;
class HostlinkTrackPrepare;
class HostlinkTrackDirectories;
class HostlinkTrackPublication;
class HostlinkTrackConfirmation;
class HostlinkTrackRead;
class HostlinkGeocachingDrafts;
// Worker-owned request processing; response owns its bytes. Device-author proof
// is not client authorization. Optional worker-owned coordinators provide the
// explicitly advertised upload and immutable package-install operations.
::hostlink::ErrorCode process_hostlink_management(const uint8_t* request, size_t size,
                                                  std::vector<uint8_t>& response, HostlinkUploads* uploads = nullptr,
                                                  HostlinkPackageInstall* packages = nullptr,
                                                  HostlinkConfigSnapshot* config = nullptr,
                                                  HostlinkConfigTransfer* transfer = nullptr,
                                                  HostlinkIdentityBackup* identity = nullptr,
                                                  HostlinkRadioIdentity* radio = nullptr,
                                                  HostlinkTrackCatalog* tracks = nullptr,
                                                  HostlinkTrackSnapshots* track_snapshots = nullptr,
                                                  HostlinkTrackPrepare* track_prepare = nullptr,
                                                  HostlinkTrackDirectories* track_directories = nullptr,
                                                  HostlinkTrackPublication* track_publication = nullptr,
                                                  HostlinkTrackConfirmation* track_confirmation = nullptr,
                                                  HostlinkTrackRead* track_read = nullptr,
                                                  HostlinkGeocachingDrafts* geocaching_drafts = nullptr);
} // namespace platform::linux_runtime
