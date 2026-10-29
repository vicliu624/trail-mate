#include "platform/linux/hostlink_management.h"
#include "app/tms_config_codec.h"
#include "chat/ports/device_author.h"
#include "geocaching/protocol/cmp_reader.h"
#include "geocaching/protocol/cmp_writer.h"
#include "hostlink/management_envelope.h"
#include "platform/linux/hostlink_config_snapshot.h"
#include "platform/linux/hostlink_config_transfer.h"
#include "platform/linux/hostlink_geocaching_drafts.h"
#include "platform/linux/hostlink_identity_backup.h"
#include "platform/linux/hostlink_package_install.h"
#include "platform/linux/hostlink_radio_identity.h"
#include "platform/linux/hostlink_track_catalog.h"
#include "platform/linux/hostlink_track_confirmation.h"
#include "platform/linux/hostlink_track_directories.h"
#include "platform/linux/hostlink_track_prepare.h"
#include "platform/linux/hostlink_track_publication.h"
#include "platform/linux/hostlink_track_snapshots.h"
#include "platform/linux/hostlink_uploads.h"
#include "platform/linux/mesh/linux_reticulum_identity.h"
#include "platform/linux/runtime_paths.h"
#include "platform/linux/track_read_runtime.h"
#include <array>
#include <chrono>

namespace platform::linux_runtime
{
namespace
{
namespace management = ::hostlink::management;
using Status = management::Status;
struct ClearBytes
{
    uint8_t* bytes;
    size_t size;
    ~ClearBytes()
    {
        volatile uint8_t* clear = bytes;
        for (size_t i = 0; i < size; ++i) clear[i] = 0;
    }
};
Status author_response(const management::EnvelopeView& request, uint8_t* output, size_t capacity, size_t& written)
{
    const bool proof = request.code == uint16_t(management::Operation::AuthorProve);
    geocaching::ByteView challenge;
    uint64_t expected = 0;
    if (proof)
    {
        geocaching::protocol::CmpReader reader({request.body, request.body_size});
        size_t fields = 0;
        if (!reader.array(fields, 2) || fields != 2 || !reader.binary(challenge, 32) || challenge.size != 32 ||
            !reader.unsignedInteger(expected) || !expected || expected > UINT32_MAX || !reader.finished()) return Status::InvalidField;
    }
    else if (request.body_size != 1 || request.body[0] != 0x90) return Status::InvalidField;
    // A fresh owner reads the same durable identity as the mesh worker. Never
    // borrow that worker's private-key object across threads or make a new key
    // store for HostLink. Existing identity corruption is a storage failure.
    LinuxReticulumIdentity identity(sqlite_database_path());
    if (!identity.begin()) return Status::StorageUnavailable;
    const auto generation = identity.generation();
    std::array<uint8_t, 64> signature{};
    if (proof)
    {
        if (expected != generation) return Status::StaleIdentity;
        std::array<uint8_t, 160> message{};
        size_t message_size = 0;
        if (!chat::deviceAuthorProofMessage(request.session, challenge.data, identity.publicKey().data(), generation,
                                            message.data(), message.size(), message_size) ||
            !identity.sign(message.data(), message_size, signature.data())) return Status::Internal;
    }
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!writer.array(proof ? 5 : 4) || !writer.binary({identity.publicKey().data(), 64}) ||
        !writer.binary({identity.identityHash().data(), 16}) || !writer.binary({identity.destinationHash().data(), 16}) ||
        !writer.unsignedInteger(generation) || (proof && !writer.binary({signature.data(), 64}))) return Status::Internal;
    written = writer.size();
    return Status::Ok;
}
} // namespace

::hostlink::ErrorCode process_hostlink_management(const uint8_t* input, size_t size, std::vector<uint8_t>& response, HostlinkUploads* uploads, HostlinkPackageInstall* packages, HostlinkConfigSnapshot* config, HostlinkConfigTransfer* transfer, HostlinkIdentityBackup* identity, HostlinkRadioIdentity* radio, HostlinkTrackCatalog* tracks, HostlinkTrackSnapshots* track_snapshots, HostlinkTrackPrepare* track_prepare, HostlinkTrackDirectories* track_directories, HostlinkTrackPublication* track_publication, HostlinkTrackConfirmation* track_confirmation, HostlinkTrackRead* track_read, HostlinkGeocachingDrafts* geocaching_drafts)
try
{
    response.clear();
    management::EnvelopeView request;
    if (!management::decodeEnvelope(input, size, request)) return ::hostlink::ErrorCode::InvalidParam;
    std::array<uint8_t, 448> body{};
    ClearBytes clear_body{body.data(), body.size()};
    body[0] = 0x90;
    size_t body_size = 1;
    auto status = Status::Unsupported;
    if (request.code == uint16_t(management::Operation::TransferBegin) && geocaching_drafts)
    {
        geocaching::protocol::CmpReader reader({request.body, request.body_size});
        size_t fields = 0;
        uint64_t kind = 0;
        geocaching::ByteView id;
        if (reader.array(fields, 4) && fields == 4 && reader.binary(id, 16) && id.size == 16 && reader.unsignedInteger(kind))
        {
            auto probe = request;
            probe.code = uint16_t(management::Operation::TransferStatus);
            // Opaque IDs cannot route to two upload kinds. Completed evidence
            // must be explicitly aborted before reusing an ID across kinds.
            if (kind == 2) status = transfer && transfer->routes(probe) ? Status::Conflict : (transfer && transfer->identityBusy()) || (uploads && uploads->identityBusy()) ? Status::Busy
                                                                                                                                                                            : Status::Unsupported;
            if (kind == 1) status = geocaching_drafts->routes(probe) ? Status::Conflict : geocaching_drafts->identityBusy() ? Status::Busy
                                                                                                                            : Status::Unsupported;
            if (kind >= 3) status = geocaching_drafts->routes(probe) ? Status::Conflict : geocaching_drafts->identityBusy() ? Status::Busy
                                                                                                                            : Status::Unsupported;
        }
    }
    if (status == Status::Unsupported)
        switch (static_cast<management::Operation>(request.code))
        {
        case management::Operation::Capabilities:
            if (request.body_size != 1 || request.body[0] != 0x90) status = Status::InvalidField;
            else
            {
                geocaching::protocol::CmpWriter writer(body.data(), body.size());
                const bool snapshots = config && config->supported();
                const bool mutations = transfer && transfer->supported();
                const bool upload_operations = uploads && uploads->supported();
                const bool identity_backup = identity && identity->supported();
                const bool identity_restore = identity && identity->restoreSupported();
                const bool radio_read = radio && radio->supported();
                const bool radio_restore = radio && radio->restoreSupported();
                const bool directory_operations = track_directories && track_directories->supported();
                const bool publication_operations = track_publication && track_publication->supported();
                const bool confirmation_operations = track_confirmation && track_confirmation->supported();
                const bool read_operations = track_read && track_read->supported();
                const bool draft_operations = geocaching_drafts && geocaching_drafts->supported();
                if (writer.array(6) && writer.unsignedInteger(1) && writer.unsignedInteger(::hostlink::kMaxFrameLen) &&
                    writer.unsignedInteger(320) && writer.unsignedInteger(1) && writer.array(4 + (identity_backup ? 1 : 0) + (identity_restore ? 2 : 0) + (radio_read ? 2 : 0) + (radio_restore ? 1 : 0) + (snapshots ? 1 : 0) + (mutations ? 2 : 0) + (upload_operations ? 7 : mutations || draft_operations ? 5
                                                                                                                                                                                                                                                                                                              : 0) +
                                                                                             (tracks ? 1 : 0) + (track_snapshots && track_snapshots->supported() ? 4 : 0) + (track_prepare && track_prepare->supported() ? 3 : 0) + (directory_operations ? 2 : 0) + (publication_operations ? 3 : 0) + (confirmation_operations ? 2 : 0) + (read_operations ? 4 : 0) + (draft_operations ? 3 : 0) + (packages && packages->supported() ? 2 : 0)) &&
                    writer.unsignedInteger(0) && writer.unsignedInteger(1) && writer.unsignedInteger(2) && writer.unsignedInteger(3))
                {
                    if (identity_backup) writer.unsignedInteger(uint16_t(management::Operation::IdentityBackupExport));
                    if (identity_restore)
                    {
                        writer.unsignedInteger(uint16_t(management::Operation::IdentityBackupRestore));
                        writer.unsignedInteger(uint16_t(management::Operation::IdentityRestoreFormats));
                    }
                    if (radio_read)
                    {
                        writer.unsignedInteger(uint16_t(management::Operation::RadioIdentityInfo));
                        writer.unsignedInteger(uint16_t(management::Operation::RadioIdentityBackupExport));
                    }
                    if (radio_restore) writer.unsignedInteger(uint16_t(management::Operation::RadioIdentityBackupRestore));
                    if (snapshots) writer.unsignedInteger(uint16_t(management::Operation::ConfigSnapshot));
                    if (mutations)
                    {
                        writer.unsignedInteger(20);
                        writer.unsignedInteger(21);
                    }
                    if (upload_operations)
                        for (unsigned operation = 32; operation <= 38; ++operation) writer.unsignedInteger(operation);
                    else if (mutations || draft_operations)
                        for (unsigned operation = 32; operation <= 36; ++operation) writer.unsignedInteger(operation);
                    if (publication_operations)
                    {
                        writer.unsignedInteger(uint16_t(management::Operation::JobGet));
                        writer.unsignedInteger(uint16_t(management::Operation::JobCancel));
                    }
                    if (draft_operations)
                        for (unsigned operation = 64; operation <= 66; ++operation) writer.unsignedInteger(operation);
                    if (tracks) writer.unsignedInteger(uint16_t(management::Operation::TrackList));
                    if (track_snapshots && track_snapshots->supported()) writer.unsignedInteger(uint16_t(management::Operation::TrackSnapshot));
                    if (track_prepare && track_prepare->supported()) writer.unsignedInteger(uint16_t(management::Operation::TrackPrepare));
                    if (publication_operations) writer.unsignedInteger(uint16_t(management::Operation::TrackCommit));
                    if (track_snapshots && track_snapshots->supported())
                    {
                        writer.unsignedInteger(uint16_t(management::Operation::TrackSnapshotBuild));
                        writer.unsignedInteger(uint16_t(management::Operation::TrackSnapshotCancel));
                        writer.unsignedInteger(uint16_t(management::Operation::TrackSnapshotRelease));
                    }
                    if (directory_operations)
                    {
                        writer.unsignedInteger(uint16_t(management::Operation::TrackDirectoryList));
                        writer.unsignedInteger(uint16_t(management::Operation::TrackDirectoryDiscover));
                    }
                    if (read_operations)
                    {
                        for (unsigned operation = 91; operation <= 94; ++operation) writer.unsignedInteger(operation);
                    }
                    if (track_prepare && track_prepare->supported()) writer.unsignedInteger(uint16_t(management::Operation::TrackSignPrepare));
                    if (confirmation_operations)
                    {
                        writer.unsignedInteger(uint16_t(management::Operation::TrackConfirmPublication));
                        writer.unsignedInteger(uint16_t(management::Operation::TrackImportHistory));
                    }
                    if (track_prepare && track_prepare->supported())
                    {
                        writer.unsignedInteger(uint16_t(management::Operation::TrackPackageSignPrepare));
                    }
                    if (packages && packages->supported())
                    {
                        writer.unsignedInteger(100);
                        writer.unsignedInteger(101);
                    }
                    if (writer.unsignedInteger(snapshots || mutations ? app::tms::kSchemaVersion : 0))
                    {
                        status = Status::Ok;
                        body_size = writer.size();
                    }
                    else status = Status::Internal;
                }
                else status = Status::Internal;
            }
            break;
        case management::Operation::CacheListOwned:
        case management::Operation::CacheDraftGet:
        case management::Operation::CacheDraftPut:
            if (geocaching_drafts)
            {
                const auto now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
                status = geocaching_drafts->process(request, now, body.data(), body.size(), body_size);
            }
            break;
        case management::Operation::TrackList:
            status = tracks ? tracks->read(request, body.data(), body.size(), body_size) : Status::Unsupported;
            break;
        case management::Operation::TrackSnapshot:
        case management::Operation::TrackSnapshotBuild:
        case management::Operation::TrackSnapshotCancel:
        case management::Operation::TrackSnapshotRelease:
            if (track_snapshots)
            {
                const auto now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
                status = track_snapshots->process(request, now, body.data(), body.size(), body_size);
            }
            break;
        case management::Operation::TrackPrepare:
        case management::Operation::TrackSignPrepare:
        case management::Operation::TrackPackageSignPrepare:
            if (track_prepare)
            {
                const auto now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
                status = track_prepare->process(request, now, body.data(), body.size(), body_size);
            }
            break;
        case management::Operation::TrackDirectoryList:
        case management::Operation::TrackDirectoryDiscover:
            if (track_directories) status = track_directories->process(request, body.data(), body.size(), body_size);
            break;
        case management::Operation::TrackCommit:
        case management::Operation::JobGet:
        case management::Operation::JobCancel:
            if (track_publication) status = track_publication->process(request, body.data(), body.size(), body_size);
            break;
        case management::Operation::AuthorInfo:
        case management::Operation::AuthorProve:
            status = author_response(request, body.data(), body.size(), body_size);
            break;
        case management::Operation::IdentityBackupExport:
            if (identity) status = identity->process(request, body.data(), body.size(), body_size);
            break;
        case management::Operation::RadioIdentityInfo:
        case management::Operation::RadioIdentityBackupExport:
            if (radio) status = radio->process(request, body.data(), body.size(), body_size);
            break;
        case management::Operation::IdentityRestoreFormats:
            if (identity && identity->restoreSupported())
            {
                if (request.body_size != 1 || request.body[0] != 0x90) status = Status::InvalidField;
                else
                {
                    geocaching::protocol::CmpWriter writer(body.data(), body.size());
                    if (writer.array(2) && writer.unsignedInteger(0) && writer.unsignedInteger(1))
                    {
                        status = Status::Ok;
                        body_size = writer.size();
                    }
                    else status = Status::Internal;
                }
            }
            break;
        case management::Operation::IdentityBackupRestore:
        case management::Operation::RadioIdentityBackupRestore:
        {
            const bool wireless = request.code == uint16_t(management::Operation::RadioIdentityBackupRestore);
            if (wireless ? !radio || !radio->restoreSupported() : !identity || !identity->restoreSupported()) break;
            if ((uploads && uploads->identityBusy()) || (packages && packages->identityBusy()) ||
                (config && config->identityBusy()) || (transfer && transfer->identityBusy()) ||
                (track_snapshots && track_snapshots->identityBusy()) || (track_prepare && track_prepare->identityBusy()) || (track_publication && track_publication->identityBusy()) || (track_confirmation && track_confirmation->identityBusy()) || (track_read && track_read->identityBusy()) || (geocaching_drafts && geocaching_drafts->identityBusy())) status = Status::Busy;
            else status = wireless ? radio->process(request, body.data(), body.size(), body_size) : identity->process(request, body.data(), body.size(), body_size);
            break;
        }
        case management::Operation::TrackReadBegin:
        case management::Operation::TrackReadStatus:
        case management::Operation::TrackReadChunk:
        case management::Operation::TrackReadRelease:
            if (track_read) status = track_read->process(request, body.data(), body.size(), body_size);
            break;
        case management::Operation::TrackConfirmPublication:
        case management::Operation::TrackImportHistory:
            if (track_confirmation)
            {
                const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                status = track_confirmation->process(request, now, body.data(), body.size(), body_size);
            }
            break;
        case management::Operation::SessionClose:
            // Close releases only this session's transient owner. Durable files
            // require explicit abort or a fresh identity-bound Begin.
            status = request.body_size == 1 && request.body[0] == 0x90 ? Status::Ok : Status::InvalidField;
            if (status == Status::Ok && packages) packages->close(request.session);
            if (status == Status::Ok && uploads) uploads->close(request.session);
            if (status == Status::Ok && config) config->close(request.session);
            if (status == Status::Ok && track_snapshots) track_snapshots->close(request.session);
            if (status == Status::Ok && track_prepare) track_prepare->close(request.session);
            if (status == Status::Ok && track_directories) track_directories->close(request.session);
            if (status == Status::Ok && track_publication) track_publication->close(request.session);
            if (status == Status::Ok && track_confirmation) track_confirmation->close(request.session);
            if (status == Status::Ok && track_read) track_read->close(request.session);
            if (status == Status::Ok && geocaching_drafts) geocaching_drafts->close(request.session);
            break;
        case management::Operation::ConfigSnapshot:
            if (config) status = config->process(request, body.data(), body.size(), body_size);
            break;
        default:
            if (geocaching_drafts && geocaching_drafts->routes(request))
            {
                const auto now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
                status = geocaching_drafts->process(request, now, body.data(), body.size(), body_size);
            }
            else if (transfer && transfer->routes(request)) status = transfer->process(request, body.data(), body.size(), body_size);
            else if (packages && (request.code == uint16_t(management::Operation::TrackPackageInstall) || request.code == uint16_t(management::Operation::TrackPackageInstallStatus)))
            {
                const auto now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
                status = packages->process(request, now, body.data(), body.size(), body_size);
            }
            else if (uploads && request.code >= uint16_t(management::Operation::TransferBegin) && request.code <= uint16_t(management::Operation::TransferListOwned))
            {
                const auto now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
                status = uploads->process(request, now, body.data(), body.size(), body_size);
            }
            break;
        }
    if (status != Status::Ok)
    {
        body[0] = 0x90;
        body_size = 1;
    }
    std::array<uint8_t, ::hostlink::kMaxFrameLen> encoded{};
    ClearBytes clear_encoded{encoded.data(), encoded.size()};
    size_t written = 0;
    if (!management::encodeEnvelope(request.session, request.request, uint16_t(status), body.data(), body_size,
                                    encoded.data(), encoded.size(), written)) return ::hostlink::ErrorCode::Internal;
    response.assign(encoded.begin(), encoded.begin() + written);
    return ::hostlink::ErrorCode::Ok;
}
catch (...)
{
    response.clear();
    return ::hostlink::ErrorCode::Internal;
}
} // namespace platform::linux_runtime
