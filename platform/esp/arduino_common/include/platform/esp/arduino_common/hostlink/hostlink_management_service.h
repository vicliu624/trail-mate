#pragma once

#include "app/app_facade_access.h"
#include "chat/infra/reticulum/reticulum_wire.h"
#include "chat/ports/i_mesh_adapter.h"
#include "geocaching/protocol/cmp_reader.h"
#include "geocaching/protocol/cmp_writer.h"
#include "hostlink/hostlink_service_codec.h"
#include "hostlink/identity_backup_request.h"
#include "hostlink/management_envelope.h"
#include "hostlink/sensitive_memory.h"
#include "platform/esp/arduino_common/app_config_sd_tms_runtime.h"
#include "platform/esp/arduino_common/geocaching/browse_runtime.h"
#include "platform/esp/arduino_common/hostlink/hostlink_chat_runtime.h"
#include "platform/esp/arduino_common/hostlink/hostlink_download_mode.h"
#include "platform/esp/arduino_common/hostlink/hostlink_service.h"
#include "platform/esp/arduino_common/hostlink/web_config_upload.h"
#include "platform/esp/arduino_common/hostlink/web_publication_commit.h"
#include "platform/esp/arduino_common/hostlink/web_publication_prepare.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/common/mbedtls_sha256_compat.h"
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
#include "hostlink/route_install_directory.h"
#include "hostlink/route_install_storage_policy.h"
#include "hostlink/route_storage_lease.h"
#include "platform/esp/arduino_common/hostlink/hostlink_identity_restore.h"
#include "platform/esp/arduino_common/hostlink/web_geocache_confirmation.h"
#include "platform/esp/arduino_common/hostlink/web_geocache_signature.h"
#include "platform/esp/arduino_common/storage/sd_borrowed_record_file_io.h"
#include "platform/esp/common/storage/track_snapshot_service.h"
#include "platform/esp/idf_common/hostlink_route_install_runtime.h"
#include "platform/esp/idf_common/hostlink_track_catalog.h"
#include "platform/esp/idf_common/hostlink_track_directories_runtime.h"
#include "platform/esp/idf_common/hostlink_track_ip_confirmation_runtime.h"
#include "platform/esp/idf_common/hostlink_track_prepare_owner.h"
#include "platform/esp/idf_common/hostlink_track_prepare_persistence.h"
#include "platform/esp/idf_common/hostlink_track_prepare_runtime.h"
#include "platform/esp/idf_common/hostlink_track_publication_runtime.h"
#include "platform/esp/idf_common/hostlink_track_read_service.h"
#include "platform/esp/idf_common/hostlink_track_snapshot_owner.h"
#include "platform/esp/idf_common/hostlink_track_upload_identity.h"
#include "platform/esp/idf_common/hostlink_track_upload_maintenance.h"
#include "platform/esp/idf_common/hostlink_track_upload_runtime.h"
#endif

namespace hostlink::management
{
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
inline route_install_runtime::RouteInstallOwner* route_install_owner = nullptr;
inline void release_route_install()
{
    if (!route_install_owner) return;
    route_install_runtime::releaseRouteInstall(route_install_owner);
    hostlink::route_storage_lease::release();
}
struct RouteInstallDirectoryPort
{
    uint32_t session() const { return platform::esp::arduino_common::storage::sd_media_session(); }
    bool ready() const { return platform::esp::arduino_common::storage::sd_card_ready(); }
    bool externalOwner() const { return platform::esp::arduino_common::storage::sd_external_block_owner_active(); }
    bool directory(const char* path, uint32_t expected) const { return session() == expected && platform::esp::arduino_common::storage::sd_is_directory(path) && session() == expected; }
    bool mkdir(const char* path, uint32_t expected) const { return platform::esp::arduino_common::storage::sd_mkdir(path, expected); }
};
inline Status route_install_policy(const RouteInstallView& request, bool install, uint8_t* scratch, size_t capacity, uint32_t expected_media)
{
    if (expected_media != platform::esp::arduino_common::storage::sd_media_session()) return Status::StorageUnavailable;
    RouteInstallStorageActivity activity;
    activity.restart = app::sd_tms::webConfigurationRestartPending();
    activity.snapshot = snapshot_runtime::owner != nullptr;
    activity.prepare = prepare_runtime::prepareHoldsStorage();
    activity.publication = publication_runtime::holdsStorage();
    if (install && app::hasAppFacade())
    {
        const auto& config = app::configFacade().readConfig();
        activity.active_route = routeInstallPathMatches(request.hash, config.route_path, sizeof(config.route_path));
    }
    const auto initial = routeInstallStorageAdmission(activity);
    if (initial != Status::Ok) return initial;
    if (!scratch || capacity < 448) return Status::Internal;
    const auto a = reinterpret_cast<uintptr_t>(request.hash.data), b = reinterpret_cast<uintptr_t>(scratch);
    // Admission still borrows the incoming command. Recheck its persistent
    // config journal only after the route owner has copied the binding.
    if (a <= b ? b - a < request.hash.size : a - b < capacity) return Status::Ok;
    const auto aligned = (b + 128 + alignof(web_config::Metadata) - 1) & ~uintptr_t(alignof(web_config::Metadata) - 1);
    auto* metadata = new (reinterpret_cast<void*>(aligned)) web_config::Metadata;
    const bool loaded = web_config::load(*metadata, scratch);
    activity.configuration = loaded && metadata->phase < (metadata->kind == 2 ? 4 : 2);
    metadata->~Metadata();
    namespace sd = platform::esp::arduino_common::storage;
    if (!loaded && (sd::sd_exists(web_config::kMetaPath) || sd::sd_exists(web_config::kMetaNew) || sd::sd_exists(web_config::kMetaBackup))) return Status::StorageUnavailable;
    const auto admitted = routeInstallStorageAdmission(activity);
    if (admitted != Status::Ok || !install) return admitted;
    RouteInstallDirectoryPort directory;
    return ensureRouteInstallDirectory(directory, expected_media);
}
template <class Action>
inline Status with_route_storage_gate(Action&& action, bool signature_recovery = false)
{
    struct Context
    {
        Action& action;
        Status status = Status::Busy;
    } context{action};
    const auto gate = signature_recovery
                          ? platform::esp::arduino_common::geocaching::browse_runtime::withSignatureInputRecovery
                          : platform::esp::arduino_common::geocaching::browse_runtime::withIdentityMaintenance;
    gate(
        [](void* opaque)
        {auto& state=*static_cast<Context*>(opaque);state.status=state.action();return false; },
        &context);
    // False deliberately avoids the identity-restoration side effect. Status
    // stays Busy when the storage gate did not invoke our action.
    return context.status;
}
inline Status package_route_policy(const char* target, uint8_t* scratch, size_t capacity)
{
    if (!target || !scratch || capacity < 448) return Status::InvalidField;
    RouteInstallStorageActivity activity;
    activity.restart = app::sd_tms::webConfigurationRestartPending();
    activity.snapshot = snapshot_runtime::owner != nullptr;
    activity.publication = publication_runtime::holdsStorage();
    if (app::hasAppFacade()) activity.active_route = !std::strcmp(app::configFacade().readConfig().route_path, target);
    const auto initial = routeInstallStorageAdmission(activity);
    if (initial != Status::Ok) return initial;
    const auto aligned = (reinterpret_cast<uintptr_t>(scratch) + 128 + alignof(web_config::Metadata) - 1) & ~uintptr_t(alignof(web_config::Metadata) - 1);
    auto* metadata = new (reinterpret_cast<void*>(aligned)) web_config::Metadata;
    const bool loaded = web_config::load(*metadata, scratch);
    activity.configuration = loaded && metadata->phase < (metadata->kind == 2 ? 4 : 2);
    metadata->~Metadata();
    namespace sd = platform::esp::arduino_common::storage;
    if (!loaded && (sd::sd_exists(web_config::kMetaPath) || sd::sd_exists(web_config::kMetaNew) || sd::sd_exists(web_config::kMetaBackup))) return Status::StorageUnavailable;
    return routeInstallStorageAdmission(activity);
}
inline void advance_route_install(uint8_t* scratch, size_t capacity, uint64_t now, bool connected)
{
    if (!route_install_owner) return;
    if (!connected || !route_install_owner->alive(now, connected))
    {
        release_route_install();
        return;
    }
    auto* adapter = app::hasAppFacade() ? app::messagingFacade().getMeshAdapter() : nullptr;
    const auto expected_media = route_install_owner->mediaSession();
    with_route_storage_gate([&]
                            { return route_install_runtime::advanceRouteInstallSlot(route_install_owner, adapter, scratch, capacity, now, true,
                                                                                    [&](const RouteInstallView& view, bool install)
                                                                                    { return route_install_policy(view, install, scratch, capacity, expected_media); }); });
    if (!route_install_owner) hostlink::route_storage_lease::release();
}
inline Status process_route_install(const EnvelopeView& request, uint8_t* output, size_t capacity, size_t& written)
{
    auto* adapter = app::hasAppFacade() ? app::messagingFacade().getMeshAdapter() : nullptr;
    if (!adapter) return Status::Busy;
    const auto expected_media = route_install_owner ? route_install_owner->mediaSession() : platform::esp::arduino_common::storage::sd_media_session();
    return with_route_storage_gate([&]
                                   {
        if(!route_install_owner&&!hostlink::route_storage_lease::acquire())return Status::Busy;
        const auto result=route_install_runtime::processRouteInstallRequest(route_install_owner,*adapter,request,snapshot_runtime::nowMs(),output,capacity,written,
            [&](const RouteInstallView& view,bool install){return route_install_policy(view,install,output,capacity,expected_media);});
        if(result==Status::Ok)route_install_runtime::retireDeliveredRouteInstall(route_install_owner);
        if(!route_install_owner)hostlink::route_storage_lease::release();
        return result; });
}
#endif
inline Status publication_job_get(const EnvelopeView& request, uint8_t* output, size_t capacity, size_t& written, bool stop = false)
{
    if (capacity < 448) return Status::Internal;
    geocaching::protocol::CmpReader reader({request.body, request.body_size});
    size_t fields = 0;
    uint64_t generation = 0;
    geocaching::ByteView operation;
    if (!reader.array(fields, 3) || (fields != 2 && fields != 3) || !reader.binary(operation, 16) || operation.size != 16 ||
        !reader.unsignedInteger(generation) || !generation || generation > UINT32_MAX) return Status::InvalidField;
    uint64_t kind = 0;
    if ((fields == 3 && (!reader.unsignedInteger(kind) || kind != 1)) || !reader.finished()) return Status::InvalidField;
    uint8_t id[16], bits = 0;
    std::memcpy(id, operation.data, sizeof(id));
    for (const auto byte : id) bits |= byte;
    if (!bits) return Status::InvalidField;
    auto* adapter = app::hasAppFacade() ? app::messagingFacade().getMeshAdapter() : nullptr;
    if (!adapter) return Status::Busy;
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
    if (kind == 1)
    {
        if (snapshot_runtime::owner || prepare_runtime::prepareHoldsStorage() || upload_runtime::owner) return Status::Busy;
        if (stop) return publication_runtime::cancelStatus(*adapter, operation, uint32_t(generation), output, capacity, written);
        return publication_runtime::getStatus(*adapter, operation, uint32_t(generation), output, capacity, written, request.session, uint64_t(esp_timer_get_time() / 1000));
    }
#else
    if (kind == 1) return Status::Unsupported;
#endif
    uint32_t actual = 0;
    const auto identity = adapter->readDeviceAuthorKey(output + 384, actual);
    if (identity == chat::DeviceAuthorResult::StorageError) return Status::StorageUnavailable;
    if (identity != chat::DeviceAuthorResult::Ok) return Status::Busy;
    if (generation != actual) return Status::StaleIdentity;
    namespace runtime = platform::esp::arduino_common::geocaching::browse_runtime;
    auto result = stop ? runtime::requestWebPublicationStop(id, output + 384) : runtime::WebReadResult::Ready;
    if (result == runtime::WebReadResult::Ready)
    {
        result = runtime::readWebPublicationJob(id, output + 384, output, 384, written);
        if (result == runtime::WebReadResult::NotFound)
            result = runtime::readWebPublicationHistory(id, output + 384, output, 384, written);
    }
    switch (result)
    {
    case runtime::WebReadResult::Ready:
        return Status::Ok;
    case runtime::WebReadResult::Pending:
        return Status::Busy;
    case runtime::WebReadResult::Forbidden:
        return Status::Unauthorized;
    case runtime::WebReadResult::StaleIdentity:
        return Status::StaleIdentity;
    case runtime::WebReadResult::NotFound:
        // Missing retained evidence does not authorize a repeated publication.
        return Status::Expired;
    default:
        return Status::StorageUnavailable;
    }
}
inline Status readDirectoryList(const EnvelopeView& request, uint8_t* output, size_t capacity, size_t& written)
{
    geocaching::protocol::CmpReader reader({request.body, request.body_size});
    size_t fields = 0;
    uint64_t offset = 0;
    if (!reader.array(fields, 1) || fields != 1 || !reader.unsignedInteger(offset) || offset > 4 || !reader.finished()) return Status::InvalidField;
    namespace runtime = platform::esp::arduino_common::geocaching::browse_runtime;
    switch (runtime::readWebDirectories(static_cast<uint32_t>(offset), output, capacity, written))
    {
    case runtime::WebReadResult::Ready:
        return Status::Ok;
    case runtime::WebReadResult::Pending:
        return Status::Busy;
    case runtime::WebReadResult::Invalid:
        return Status::InvalidField;
    default:
        return Status::StorageUnavailable;
    }
}
inline Status draft_put(const EnvelopeView& request, uint8_t* output, size_t capacity, size_t& written)
{
    if (capacity < 448) return Status::Internal;
    geocaching::protocol::CmpReader reader({request.body, request.body_size});
    size_t fields = 0;
    geocaching::ByteView transfer, draft;
    uint64_t expected = 0, author_generation = 0;
    if (!reader.array(fields, 4) || fields != 4 || !reader.binary(transfer, 16) || transfer.size != 16 ||
        !reader.binary(draft, 16) || draft.size != 16 || !reader.unsignedInteger(expected) || expected == UINT64_MAX ||
        !reader.unsignedInteger(author_generation) || !author_generation || author_generation > UINT32_MAX || !reader.finished()) return Status::InvalidField;
    uint8_t ids[32];
    std::memcpy(ids, transfer.data, 16);
    std::memcpy(ids + 16, draft.data, 16);
    auto* adapter = app::hasAppFacade() ? app::messagingFacade().getMeshAdapter() : nullptr;
    if (!adapter) return Status::Busy;
    uint32_t actual = 0;
    const auto author = adapter->readDeviceAuthorKey(output + 384, actual);
    if (author == chat::DeviceAuthorResult::StorageError) return Status::StorageUnavailable;
    if (author != chat::DeviceAuthorResult::Ok) return Status::Busy;
    if (actual != author_generation) return Status::StaleIdentity;
    web_config::Metadata meta;
    if (!web_config::load(meta, output)) return Status::Expired;
    if (std::memcmp(meta.owner, request.session, 16)) return Status::Unauthorized;
    if (meta.kind != 2 || !meta.phase || std::memcmp(meta.id, ids, 16)) return Status::Conflict;
    if (meta.phase == 1)
    {
        std::memcpy(meta.base_hash, ids + 16, 16);
        for (unsigned i = 0; i < 8; ++i) meta.base_hash[16 + i] = static_cast<uint8_t>(expected >> (8 * i));
        web_config::put32(meta.base_hash + 24, actual);
        meta.phase = 2;
        if (!web_config::save(meta, output)) return Status::StorageUnavailable;
    }
    else
    {
        if (std::memcmp(meta.base_hash, ids + 16, 16) || web_config::u32(meta.base_hash + 24) != actual) return Status::Conflict;
        for (unsigned i = 0; i < 8; ++i)
            if (meta.base_hash[16 + i] != static_cast<uint8_t>(expected >> (8 * i))) return Status::Conflict;
    }
    namespace runtime = platform::esp::arduino_common::geocaching::browse_runtime;
    if (meta.phase != 4)
    {
        auto result = runtime::checkWebDraftEditOwner(ids + 16, expected, output + 384, meta.hash);
        bool from_queue = false;
        if (result == runtime::WebReadResult::Ready)
        {
            uint8_t hash[32];
            if (!web_config::hash_file(runtime::kWebDraftCandidatePath, output, hash, meta.total)) return Status::StorageUnavailable;
            if (std::memcmp(hash, meta.hash, 32)) return Status::HashMismatch;
            if (meta.phase != 3)
            {
                meta.phase = 3;
                if (!web_config::save(meta, output)) return Status::StorageUnavailable;
            }
            result = runtime::queueWebDraftFile(ids + 16, meta.total, expected, meta.hash, output + 384, actual);
            from_queue = true;
            // Even a finished in-memory job is re-read on the next request;
            // only the committed row or a durable receipt proves success.
            if (result == runtime::WebReadResult::AlreadySaved) return Status::Busy;
        }
        else if (result == runtime::WebReadResult::AlreadySaved)
        {
            meta.phase = 4;
            if (!web_config::save(meta, output)) return Status::StorageUnavailable;
        }
        if (meta.phase != 4)
        {
            if (from_queue && (result == runtime::WebReadResult::Invalid || result == runtime::WebReadResult::HashMismatch || result == runtime::WebReadResult::StaleIdentity || result == runtime::WebReadResult::Unavailable) &&
                runtime::releaseWebDraftSave())
            {
                meta.phase = 2;
                if (!web_config::save(meta, output)) return Status::StorageUnavailable;
            }
            switch (result)
            {
            case runtime::WebReadResult::Pending:
                return Status::Busy;
            case runtime::WebReadResult::Forbidden:
                return Status::Unauthorized;
            case runtime::WebReadResult::Invalid:
                return Status::InvalidField;
            case runtime::WebReadResult::HashMismatch:
                return Status::HashMismatch;
            case runtime::WebReadResult::StaleIdentity:
                return Status::StaleIdentity;
            case runtime::WebReadResult::Unavailable:
                return Status::StorageUnavailable;
            default:
                return Status::Conflict;
            }
        }
    }
    geocaching::protocol::CmpWriter writer(output, capacity);
    if (!writer.array(3) || !writer.binary({meta.id, 16}) || !writer.binary({meta.base_hash, 16}) || !writer.unsignedInteger(expected + 1)) return Status::Internal;
    written = writer.size();
    return Status::Ok;
}
inline Status cache_list_owned(const EnvelopeView& request, uint8_t* output, size_t capacity, size_t& written)
{
    if (capacity < 448) return Status::Internal;
    geocaching::protocol::CmpReader reader({request.body, request.body_size});
    size_t fields = 0;
    uint64_t offset = 0, expected = 0, author_generation = 0;
    if (!reader.array(fields, 3) || fields != 3 || !reader.unsignedInteger(offset) || offset > UINT32_MAX ||
        reader.position() >= request.body_size) return Status::InvalidField;
    const bool bound = request.body[reader.position()] != 0xc0;
    if (!(bound ? reader.unsignedInteger(expected) : reader.nil()) || (offset && !bound) ||
        !reader.unsignedInteger(author_generation) || !author_generation || author_generation > UINT32_MAX || !reader.finished())
        return Status::InvalidField;
    auto* adapter = app::hasAppFacade() ? app::messagingFacade().getMeshAdapter() : nullptr;
    if (!adapter) return Status::Busy;
    uint32_t actual = 0;
    const auto author = adapter->readDeviceAuthorKey(output + 384, actual);
    if (author == chat::DeviceAuthorResult::StorageError) return Status::StorageUnavailable;
    if (author != chat::DeviceAuthorResult::Ok) return Status::Busy;
    if (actual != author_generation) return Status::StaleIdentity;
    namespace runtime = platform::esp::arduino_common::geocaching::browse_runtime;
    switch (runtime::readWebDraftCatalog(static_cast<uint32_t>(offset), bound, expected, output, 384, written))
    {
    case runtime::WebReadResult::Ready:
        return Status::Ok;
    case runtime::WebReadResult::Pending:
        return Status::Busy;
    case runtime::WebReadResult::Conflict:
    case runtime::WebReadResult::AlreadySaved:
    case runtime::WebReadResult::NotFound:
        return Status::Conflict;
    case runtime::WebReadResult::Invalid:
        return Status::InvalidField;
    case runtime::WebReadResult::Unavailable:
        return Status::StorageUnavailable;
    case runtime::WebReadResult::Forbidden:
        return Status::Unauthorized;
    case runtime::WebReadResult::HashMismatch:
        return Status::HashMismatch;
    case runtime::WebReadResult::StaleIdentity:
        return Status::StaleIdentity;
    case runtime::WebReadResult::Absent:
        return Status::Internal;
    }
    return Status::Internal;
}
inline Status cache_draft_get(const EnvelopeView& request, uint8_t* output, size_t capacity, size_t& written)
{
    if (capacity < 448) return Status::Internal;
    geocaching::protocol::CmpReader reader({request.body, request.body_size});
    size_t fields = 0;
    uint64_t offset = 0, count = 0, expected = 0, author_generation = 0;
    geocaching::ByteView id;
    if (!reader.array(fields, 5) || fields != 5 || !reader.binary(id, 16) || id.size != 16 ||
        !reader.unsignedInteger(offset) || offset > UINT32_MAX || !reader.unsignedInteger(count) || !count || count > 320 ||
        !reader.unsignedInteger(expected) || (offset && !expected) ||
        !reader.unsignedInteger(author_generation) || !author_generation || author_generation > UINT32_MAX || !reader.finished())
        return Status::InvalidField;
    uint8_t key[16];
    std::memcpy(key, id.data, 16);
    auto* adapter = app::hasAppFacade() ? app::messagingFacade().getMeshAdapter() : nullptr;
    if (!adapter) return Status::Busy;
    uint32_t actual = 0;
    const auto author = adapter->readDeviceAuthorKey(output + 384, actual);
    if (author == chat::DeviceAuthorResult::StorageError) return Status::StorageUnavailable;
    if (author != chat::DeviceAuthorResult::Ok) return Status::Busy;
    if (actual != author_generation) return Status::StaleIdentity;
    namespace runtime = platform::esp::arduino_common::geocaching::browse_runtime;
    switch (runtime::readWebDraftChunk(key, static_cast<uint32_t>(offset), static_cast<uint16_t>(count), expected, output, 384, written))
    {
    case runtime::WebReadResult::Ready:
        return Status::Ok;
    case runtime::WebReadResult::Pending:
        return Status::Busy;
    case runtime::WebReadResult::Conflict:
    case runtime::WebReadResult::AlreadySaved:
    case runtime::WebReadResult::NotFound:
        return Status::Conflict;
    case runtime::WebReadResult::Invalid:
        return Status::InvalidField;
    case runtime::WebReadResult::Unavailable:
        return Status::StorageUnavailable;
    case runtime::WebReadResult::Forbidden:
        return Status::Unauthorized;
    case runtime::WebReadResult::HashMismatch:
        return Status::HashMismatch;
    case runtime::WebReadResult::StaleIdentity:
        return Status::StaleIdentity;
    case runtime::WebReadResult::Absent:
        return Status::Internal;
    }
    return Status::Internal;
}
inline Status author_operation(const EnvelopeView& request, uint8_t* scratch, size_t capacity, size_t& body_size)
{
    if (capacity < 448) return Status::Internal;
    const bool proof = request.code == static_cast<uint16_t>(Operation::AuthorProve);
    uint32_t expected = 0;
    if (proof)
    {
        geocaching::protocol::CmpReader reader({request.body, request.body_size});
        size_t fields = 0;
        uint64_t generation = 0;
        geocaching::ByteView challenge;
        if (!reader.array(fields, 2) || fields != 2 || !reader.binary(challenge, 32) || challenge.size != 32 ||
            !reader.unsignedInteger(generation) || !generation || generation > UINT32_MAX || !reader.finished())
            return Status::InvalidField;
        expected = static_cast<uint32_t>(generation);
        // The request nonce can overlap the output arena. Move it out of the
        // fixed message and response regions before any encoding begins.
        std::memmove(scratch + 416, challenge.data, 32);
    }
    else if (request.body_size != 1 || request.body[0] != 0x90) return Status::InvalidField;
    auto* adapter = app::hasAppFacade() ? app::messagingFacade().getMeshAdapter() : nullptr;
    if (!adapter) return Status::Busy;
    auto* public_key = scratch + 256;
    auto* signature = scratch + 320;
    uint32_t generation = 0;
    const auto result = proof
                            ? adapter->proveDeviceAuthor(request.session, scratch + 416, expected,
                                                         scratch, 160, public_key, generation, signature)
                            : adapter->readDeviceAuthorKey(public_key, generation);
    switch (result)
    {
    case chat::DeviceAuthorResult::Ok:
        break;
    case chat::DeviceAuthorResult::Unsupported:
        return Status::Unsupported;
    case chat::DeviceAuthorResult::Unavailable:
        return Status::Busy;
    case chat::DeviceAuthorResult::StaleIdentity:
        return Status::StaleIdentity;
    case chat::DeviceAuthorResult::InvalidInput:
        return Status::InvalidField;
    case chat::DeviceAuthorResult::StorageError:
        return Status::StorageUnavailable;
    }
    if (!generation) return Status::Internal;
    auto* identity_hash = scratch + 384;
    auto* destination = scratch + 400;
    uint8_t name_hash[chat::reticulum::kNameHashSize];
    chat::reticulum::computeIdentityHash(public_key, identity_hash);
    chat::reticulum::computeNameHash("lxmf", "delivery", name_hash);
    chat::reticulum::computeDestinationHash(name_hash, identity_hash, destination);
    geocaching::protocol::CmpWriter writer(scratch, 256);
    if (!writer.array(proof ? 5 : 4) || !writer.binary({public_key, 64}) || !writer.binary({identity_hash, 16}) ||
        !writer.binary({destination, 16}) || !writer.unsignedInteger(generation) ||
        (proof && !writer.binary({signature, 64}))) return Status::Internal;
    body_size = writer.size();
    return Status::Ok;
}

// Runs on the existing application command consumer, not the CDC task. Reuses
// PendingCommand::payload as hash/read scratch and then response storage.
inline Status read_config_snapshot(const EnvelopeView& request, uint8_t* scratch,
                                   size_t capacity, size_t& body_size)
{
    geocaching::protocol::CmpReader reader({request.body, request.body_size});
    size_t fields;
    uint64_t offset, count;
    uint8_t expected[32];
    bool has_expected = false;
    if (!reader.array(fields, 3) || fields != 3 || !reader.unsignedInteger(offset) ||
        !reader.unsignedInteger(count) || count == 0 || count > 320 || offset > UINT32_MAX)
        return Status::InvalidField;
    if (!reader.nil())
    {
        // A failed cursor is poisoned; decode the expected hash using a fresh
        // cursor rather than treating a failed nil parse as a valid alternative.
        geocaching::protocol::CmpReader again({request.body, request.body_size});
        geocaching::ByteView hash;
        if (!again.array(fields, 3) || !again.unsignedInteger(offset) || !again.unsignedInteger(count) ||
            !again.binary(hash, 32) || hash.size != 32 || !again.finished()) return Status::InvalidField;
        std::memcpy(expected, hash.data, 32);
        has_expected = true;
    }
    else if (!reader.finished()) return Status::InvalidField;
    if (offset && !has_expected) return Status::InvalidField;
    if (app::sd_tms::syncPendingWorkingConfig() != app::sd_tms::WorkingConfigSyncResult::Synchronized)
        return Status::StorageUnavailable;
    using namespace platform::esp::arduino_common::storage;
    SdRuntimeFile file;
    if (!file.open("/trailmate/config.tms", "r")) return Status::StorageUnavailable;
    const uint64_t total = file.size();
    if (!total || total > 32768 || offset > total || capacity < 384) return Status::InvalidField;
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    uint8_t hash[32];
    bool ok = platform::esp::common::crypto::sha256_starts(&context, 0) == 0;
    uint64_t consumed = 0;
    while (ok && consumed < total)
    {
        const size_t wanted = static_cast<size_t>(total - consumed > 128 ? 128 : total - consumed);
        const int n = file.read(scratch, wanted);
        ok = n == static_cast<int>(wanted) &&
             platform::esp::common::crypto::sha256_update(&context, scratch, wanted) == 0;
        consumed += wanted;
    }
    ok = ok && platform::esp::common::crypto::sha256_finish(&context, hash) == 0;
    mbedtls_sha256_free(&context);
    if (!ok) return Status::StorageUnavailable;
    if (has_expected && std::memcmp(hash, expected, 32)) return Status::Conflict;
    const size_t length = static_cast<size_t>(total - offset < count ? total - offset : count);
    geocaching::protocol::CmpWriter writer(scratch, capacity);
    if (!writer.array(4) || !writer.unsignedInteger(offset) || !writer.unsignedInteger(total) ||
        !writer.binary({hash, 32}) || !writer.binaryHeader(length)) return Status::Internal;
    const size_t header = writer.size();
    if (length > capacity - header || !file.seek(offset) ||
        file.read(scratch + header, length) != static_cast<int>(length)) return Status::StorageUnavailable;
    body_size = header + length;
    return Status::Ok;
}

inline ErrorCode execute_management(PendingCommand& command)
{
    EnvelopeView request;
    if (!decodeEnvelope(command.payload, command.payload_len, request)) return ErrorCode::InvalidParam;
    // Copy only identifiers before overwriting the borrowed request arena.
    uint8_t ids[32];
    std::memcpy(ids, request.session, 16);
    std::memcpy(ids + 16, request.request, 16);
    uint8_t* body = command.payload + 41;
    size_t body_size = 1;
    Status status = Status::Unsupported;
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
    if (route_install_owner && routeInstallBlocksOperation(request.code) && route_install_owner->status() != Status::Busy && route_install_owner->status() != Status::Internal)
        release_route_install();
    if (download_mode::active()) status = Status::Busy;
    else if (route_install_owner && routeInstallBlocksOperation(request.code)) status = Status::Busy;
    else if (ip_confirmation_runtime::holdsStorage() && ip_confirmation_runtime::blocksOperation(request.code)) status = Status::Busy;
    else
#else
    if (download_mode::active()) status = Status::Busy;
    else
#endif
        if (request.code == static_cast<uint16_t>(Operation::Capabilities))
    {
        if (request.body_size != 1 || request.body[0] != 0x90) status = Status::InvalidField;
        else
        {
            // [version, frame bytes, read chunk bytes, pending window, operations, TMS schema]
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
            static constexpr uint8_t operations[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 17, 20, 21, 32, 33, 34, 35, 36, 37, 38, 48, 50, 64, 65, 66, 67, 68, 70, 71, 72, 73, 74, 75, 76, 77, 80, 81, 82, 83, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101};
#else
            static constexpr uint8_t operations[] = {0, 1, 2, 3, 17, 20, 21, 32, 33, 34, 35, 36, 48, 50, 64, 65, 66, 67, 68, 70};
#endif
            const bool bootloader = download_mode::supported();
            geocaching::protocol::CmpWriter writer(body, sizeof(command.payload) - 41);
            bool ok = writer.array(6) && writer.unsignedInteger(1) && writer.unsignedInteger(kMaxFrameLen) &&
                      writer.unsignedInteger(320) && writer.unsignedInteger(1) &&
                      writer.array(sizeof(operations) + (bootloader ? 1U : 0U) + (chat_runtime::supported() ? 3U : 0U));
            for (uint8_t operation : operations) ok = ok && writer.unsignedInteger(operation);
            if (bootloader) ok = ok && writer.unsignedInteger(static_cast<uint16_t>(Operation::EnterBootloader));
            if (chat_runtime::supported())
                for (const auto operation : {Operation::ChatCapabilities, Operation::ChatSend, Operation::ChatSendGet})
                    ok = ok && writer.unsignedInteger(static_cast<uint16_t>(operation));
            ok = ok && writer.unsignedInteger(7);
            body_size = writer.size();
            status = ok ? Status::Ok : Status::Internal;
        }
    }
    else if (request.code == static_cast<uint16_t>(Operation::EnterBootloader))
    {
        if (chat_runtime::busy()) status = Status::Busy;
        else
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
            if (route_install_owner || snapshot_runtime::owner || prepare_runtime::prepareHoldsStorage() ||
                publication_runtime::holdsStorage() || ip_confirmation_runtime::holdsStorage())
            status = Status::Busy;
        else
#endif
            status = download_mode::request(request, command.sequence);
        if (status == Status::Ok) return ErrorCode::Ok; // Correlated response follows storage quiescence.
    }
    else if (request.code >= uint16_t(Operation::ChatCapabilities) && request.code <= uint16_t(Operation::ChatSendGet))
    {
        bool deferred = false;
        status = chat_runtime::process(request, command.sequence, body, sizeof(command.payload) - 41, body_size, deferred);
        if (status == Status::Ok && deferred) return ErrorCode::Ok;
    }
    else if (request.code == static_cast<uint16_t>(Operation::SessionClose))
    {
        if (request.body_size != 1 || request.body[0] != 0x90) status = Status::InvalidField;
        else
        {
            platform::esp::arduino_common::geocaching::browse_runtime::webLease(false);
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
            platform::esp::arduino_common::geocaching::browse_runtime::releaseWebSignature(request.session);
            platform::esp::arduino_common::geocaching::browse_runtime::releaseWebConfirmation(request.session);
            upload_runtime::closeUploadSession(request.session);
            prepare_runtime::closePrepareSession(request.session);
            ip_confirmation_runtime::closeSession(request.session);
            if (route_install_owner && route_install_owner->matchesSession(request.session)) release_route_install();
            publication_runtime::closeSession(request.session);
#endif
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
            snapshot_runtime::closeSession(request.session);
            directory_runtime::closeSession(request.session);
#endif
            body[0] = 0x90;
            body_size = 1;
            status = Status::Ok;
        }
    }
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
    else if (request.code >= static_cast<uint16_t>(Operation::CacheSignBegin) && request.code <= static_cast<uint16_t>(Operation::CacheSignRelease))
        status = cache_signature(request, body, sizeof(command.payload) - 41, body_size);
    else if (request.code >= static_cast<uint16_t>(Operation::CacheConfirmBegin) && request.code <= static_cast<uint16_t>(Operation::CacheConfirmRelease))
        status = cache_confirmation(request, body, sizeof(command.payload) - 41, body_size);
#endif
    else if (request.code == static_cast<uint16_t>(Operation::AuthorInfo) ||
             request.code == static_cast<uint16_t>(Operation::AuthorProve))
        status = author_operation(request, body, sizeof(command.payload) - 41, body_size);
    else if (request.code == uint16_t(Operation::IdentityBackupExport))
    {
        auto* adapter = app::hasAppFacade() ? app::messagingFacade().getMeshAdapter() : nullptr;
        status = adapter ? processIdentityBackupExport(*adapter, request, body, sizeof(command.payload) - 41, body_size) : Status::Busy;
    }
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
    else if (request.code == uint16_t(Operation::RadioIdentityInfo) || request.code == uint16_t(Operation::RadioIdentityBackupExport))
    {
        auto* adapter = app::hasAppFacade() ? app::messagingFacade().getMeshAdapter() : nullptr;
        status = adapter ? radioIdentityReadOperation(*adapter, request, body, sizeof(command.payload) - 41, body_size) : Status::Busy;
    }
    else if (request.code == uint16_t(Operation::IdentityRestoreFormats))
    {
        if (request.body_size != 1 || request.body[0] != 0x90) status = Status::InvalidField;
        else
        {
            body[0] = 0x92;
            body[1] = 0;
            body[2] = 1;
            body_size = 3;
            status = Status::Ok;
        }
    }
    else if (request.code == uint16_t(Operation::IdentityBackupRestore) || request.code == uint16_t(Operation::RadioIdentityBackupRestore))
    {
        auto* adapter = app::hasAppFacade() ? app::messagingFacade().getMeshAdapter() : nullptr;
        IdentityRestoreActivity activity;
        activity.restart = app::sd_tms::webConfigurationRestartPending();
        activity.snapshot = snapshot_runtime::owner != nullptr || route_install_owner != nullptr;
        activity.upload = upload_runtime::owner != nullptr;
        activity.prepare = prepare_runtime::prepareHoldsStorage();
        activity.publication = publication_runtime::holdsStorage();
        // Directory callbacks can receive responses while the command consumer
        // runs. Require their own atomic lock before inspecting pending state.
        if (directory_runtime::owner)
        {
            auto& directory = *directory_runtime::owner;
            if (directory.locked.test_and_set(std::memory_order_acquire)) activity.network = true;
            else
            {
                activity.network = directory.pending;
                directory.locked.clear(std::memory_order_release);
            }
        }
        status = adapter ? restoreIdentityOperation(*adapter, request, activity, body, sizeof(command.payload) - 41, body_size) : Status::Busy;
        if (status == Status::Ok)
        {
            prepare_runtime::releasePrepare();
            publication_runtime::destroyController();
            directory_runtime::closeSession(ids);
            app::sd_tms::scheduleWebManagementRestart();
        }
    }
#endif
    else if (request.code == static_cast<uint16_t>(Operation::CacheDraftGet))
        status = cache_draft_get(request, body, sizeof(command.payload) - 41, body_size);
    else if (request.code == static_cast<uint16_t>(Operation::CacheDraftPut))
        status = draft_put(request, body, sizeof(command.payload) - 41, body_size);
    else if (request.code == static_cast<uint16_t>(Operation::CachePrepare))
        status = cache_prepare(request, body, sizeof(command.payload) - 41, body_size);
    else if (request.code == static_cast<uint16_t>(Operation::JobGet))
        status = publication_job_get(request, body, sizeof(command.payload) - 41, body_size);
    else if (request.code == static_cast<uint16_t>(Operation::JobCancel))
        status = publication_job_get(request, body, sizeof(command.payload) - 41, body_size, true);
    else if (request.code == static_cast<uint16_t>(Operation::CacheCommit))
        status = cache_commit(request, body, sizeof(command.payload) - 41, body_size);
    else if (request.code == static_cast<uint16_t>(Operation::CacheDirectoryList))
        status = readDirectoryList(request, body, sizeof(command.payload) - 41, body_size);
    else if (request.code == static_cast<uint16_t>(Operation::CacheListOwned))
        status = cache_list_owned(request, body, sizeof(command.payload) - 41, body_size);
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
    else if (request.code == uint16_t(Operation::RouteInstall) || request.code == uint16_t(Operation::RouteInstallStatus))
    {
        status = process_route_install(request, body, sizeof(command.payload) - 41, body_size);
    }
    else if (request.code == static_cast<uint16_t>(Operation::TrackList))
        status = readTrackCatalog(request, body, sizeof(command.payload) - 41, body_size);
    else if (request.code == uint16_t(Operation::TransferCleanup) || request.code == uint16_t(Operation::TransferListOwned))
    {
        auto* adapter = app::hasAppFacade() ? app::messagingFacade().getMeshAdapter() : nullptr;
        status = !snapshot_runtime::owner && !prepare_runtime::prepareHoldsStorage() && !publication_runtime::holdsStorage() && !route_install_owner && adapter
                     ? with_route_storage_gate([&]
                                               {
                                                   const size_t capacity = sizeof(command.payload) - 41;
                                                   if (request.code == uint16_t(Operation::TransferListOwned) && request.body_size == 3 &&
                                                       request.body[0] == 0x92 && (request.body[1] == 5 || request.body[1] == 6) && request.body[2] == 0xc0)
                                                   {
                                                       namespace storage = platform::esp::arduino_common::storage;
                                                       uint8_t author_hash[32];
                                                       uint32_t generation = 0;
                                                       const auto identity = upload_runtime::readUploadAuthor(*adapter, body + capacity - 64, 64, author_hash, generation);
                                                       if (identity != Status::Ok) return identity;
                                                       const auto media = storage::sd_media_session();
                                                       if (!storage::sd_card_ready() || storage::sd_external_block_owner_active()) return Status::StorageUnavailable;
                                                       const auto result = web_config::listSharedSignatureUpload(request, body + capacity - 64, generation, media, body, capacity, body_size);
                                                       if (media != storage::sd_media_session() || !storage::sd_card_ready() || storage::sd_external_block_owner_active())
                                                       {
                                                           body_size = 0;
                                                           return Status::StorageUnavailable;
                                                       }
                                                       return result;
                                                   }
                                                   return upload_runtime::processUploadMaintenance(*adapter, request, snapshot_runtime::nowMs(), body, capacity, body_size); },
                                               request.code == uint16_t(Operation::TransferListOwned) && request.body_size == 3 &&
                                                   request.body[0] == 0x92 && (request.body[1] == 5 || request.body[1] == 6) && request.body[2] == 0xc0)
                     : Status::Busy;
    }
    else if (request.code == static_cast<uint16_t>(Operation::TrackDirectoryList))
    {
        platform::esp::arduino_common::geocaching::browse_runtime::webLease(true, false);
        auto* adapter = app::hasAppFacade() ? app::messagingFacade().getMeshAdapter() : nullptr;
        status = adapter && !publication_runtime::holdsStorage() ? directory_runtime::list(*adapter, request, uint64_t(esp_timer_get_time() / 1000), body, sizeof(command.payload) - 41, body_size) : Status::Busy;
    }
    else if (request.code == static_cast<uint16_t>(Operation::TrackDirectoryDiscover))
    {
        platform::esp::arduino_common::geocaching::browse_runtime::webLease(true, false);
        auto* adapter = app::hasAppFacade() ? app::messagingFacade().getMeshAdapter() : nullptr;
        status = adapter && !publication_runtime::holdsStorage() ? directory_runtime::discover(*adapter, request, uint64_t(esp_timer_get_time() / 1000), body, sizeof(command.payload) - 41, body_size) : Status::Busy;
    }
    else if (request.code == uint16_t(Operation::TrackReadBegin))
    {
        auto* adapter = app::hasAppFacade() ? app::messagingFacade().getMeshAdapter() : nullptr;
        status = adapter && !snapshot_runtime::owner && !upload_runtime::owner && !prepare_runtime::prepareHoldsStorage() ? track_read_runtime::processReadBegin(*adapter, request, snapshot_runtime::nowMs(), body, sizeof(command.payload) - 41, body_size) : Status::Busy;
    }
    else if (request.code == uint16_t(Operation::TrackReadStatus) || request.code == uint16_t(Operation::TrackReadChunk) || request.code == uint16_t(Operation::TrackReadRelease))
    {
        auto* adapter = app::hasAppFacade() ? app::messagingFacade().getMeshAdapter() : nullptr;
        status = adapter ? track_read_runtime::processReadResult(*adapter, request, snapshot_runtime::nowMs(), body, sizeof(command.payload) - 41, body_size) : Status::Busy;
    }
#endif
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
    else if (request.code == static_cast<uint16_t>(Operation::TrackConfirmPublication) || request.code == static_cast<uint16_t>(Operation::TrackImportHistory))
    {
        auto* adapter = app::hasAppFacade() ? app::messagingFacade().getMeshAdapter() : nullptr;
        if (!adapter || snapshot_runtime::owner || prepare_runtime::prepareHoldsStorage() || publication_runtime::holdsStorage() || app::sd_tms::webConfigurationRestartPending()) status = Status::Busy;
        else status = with_route_storage_gate([&]
                                              { return ip_confirmation_runtime::processConfirmation(*adapter, request, snapshot_runtime::nowMs(), body, sizeof(command.payload) - 41, body_size); });
    }
    else if (request.code == static_cast<uint16_t>(Operation::TrackPrepare) || request.code == static_cast<uint16_t>(Operation::TrackSignPrepare) || request.code == uint16_t(Operation::TrackPackageSignPrepare) || request.code == uint16_t(Operation::TrackPackageInstall) || request.code == uint16_t(Operation::TrackPackageInstallStatus))
    {
        auto* adapter = app::hasAppFacade() ? app::messagingFacade().getMeshAdapter() : nullptr;
        if (snapshot_runtime::owner || publication_runtime::holdsStorage() || route_install_owner || !adapter) status = Status::Busy;
        else if (request.code == uint16_t(Operation::TrackPackageInstall) || request.code == uint16_t(Operation::TrackPackageInstallStatus))
            status = with_route_storage_gate([&]
                                             { return prepare_runtime::processPrepareRequest(*adapter, request, snapshot_runtime::nowMs(), body, sizeof(command.payload) - 41, body_size, package_route_policy); });
        else status = prepare_runtime::processPrepareRequest(*adapter, request, snapshot_runtime::nowMs(), body, sizeof(command.payload) - 41, body_size);
    }
    else if (request.code == static_cast<uint16_t>(Operation::TrackCommit))
    {
        auto* adapter = app::hasAppFacade() ? app::messagingFacade().getMeshAdapter() : nullptr;
        status = !snapshot_runtime::owner && !prepare_runtime::prepareHoldsStorage() && !upload_runtime::owner && adapter ? publication_runtime::processCommit(*adapter, request, snapshot_runtime::nowMs(), body, sizeof(command.payload) - 41, body_size) : Status::Busy;
    }
#endif
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
    else if (request.code == static_cast<uint16_t>(Operation::TrackSnapshotBuild))
    {
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
        if (upload_runtime::owner || prepare_runtime::prepareHoldsStorage() || publication_runtime::holdsStorage()) status = Status::Busy;
        else
#endif
            status = snapshot_runtime::build(request, body, sizeof(command.payload) - 41, body_size);
    }
    else if (request.code == static_cast<uint16_t>(Operation::TrackSnapshotCancel))
        status = snapshot_runtime::cancel(request, body, sizeof(command.payload) - 41, body_size);
    else if (request.code == static_cast<uint16_t>(Operation::TrackSnapshotRelease))
    {
        namespace sd = platform::esp::arduino_common::storage;
        if (snapshot_runtime::owner)
            status = snapshot_runtime::releaseTrackSnapshot(request, snapshot_runtime::owner->io, snapshot_runtime::owner->media, body, sizeof(command.payload) - 41, body_size);
        else
        {
            const auto media = sd::sd_media_session();
            sd::SdRuntimeFile file(sd::SdRuntimeMemoryPolicy::PsramOnly);
            sd::SdBorrowedRecordFileIo io(file, media);
            status = file.has_backing_storage() ? snapshot_runtime::releaseTrackSnapshot(request, io, media, body, sizeof(command.payload) - 41, body_size) : Status::Busy;
        }
    }
    else if (request.code == static_cast<uint16_t>(Operation::TrackSnapshot))
    {
        namespace sd = platform::esp::arduino_common::storage;
        sd::SdRuntimeFile file(sd::SdRuntimeMemoryPolicy::PsramOnly);
        sd::SdBorrowedRecordFileIo io(file, sd::sd_media_session());
        status = file.has_backing_storage() ? readTrackSnapshotService(request, io, body, sizeof(command.payload) - 41, body_size) : Status::Busy;
    }
#endif
    else if (request.code == static_cast<uint16_t>(Operation::ConfigSnapshot))
        status = read_config_snapshot(request, body, sizeof(command.payload) - 41, body_size);
    else if (request.code == static_cast<uint16_t>(Operation::ConfigValidate) ||
             request.code == static_cast<uint16_t>(Operation::ConfigCommit) ||
             (request.code >= 32 && request.code <= 36))
    {
#if (defined(ESP_PLATFORM) && !defined(ARDUINO)) || defined(TRAIL_MATE_USB_HOSTLINK_AUTOSTART)
        if (upload_runtime::routeTrackUpload(request))
        {
            auto* adapter = app::hasAppFacade() ? app::messagingFacade().getMeshAdapter() : nullptr;
            status = !snapshot_runtime::owner && !prepare_runtime::prepareHoldsStorage() && !publication_runtime::holdsStorage() && adapter ? upload_runtime::processAuthenticatedUpload(*adapter, request, snapshot_runtime::nowMs(), body, sizeof(command.payload) - 41, body_size) : Status::Busy;
        }
        else
#endif
            status = web_config::process(request, body, sizeof(command.payload) - 41, body_size);
    }
    if (status != Status::Ok)
    {
        body[0] = 0x90;
        body_size = 1;
    }
    size_t written;
    if (request.code == uint16_t(Operation::IdentityBackupRestore) || request.code == uint16_t(Operation::RadioIdentityBackupRestore))
        std::memset(body + body_size, 0, sizeof(command.payload) - 41 - body_size);
    const bool sensitive = request.code == uint16_t(Operation::IdentityBackupExport) || request.code == uint16_t(Operation::IdentityBackupRestore) ||
                           request.code == uint16_t(Operation::RadioIdentityBackupExport) || request.code == uint16_t(Operation::RadioIdentityBackupRestore);
    if (!encodeEnvelope(ids, ids + 16, static_cast<uint16_t>(status), body, body_size,
                        command.payload, sizeof(command.payload), written))
    {
        if (sensitive) clearSensitiveBytes(command.payload, sizeof(command.payload));
        return ErrorCode::Internal;
    }
    const auto result = enqueue_event(static_cast<uint8_t>(FrameType::ManagementResponse), command.payload,
                                      written, false, command.sequence)
                            ? ErrorCode::Ok
                            : ErrorCode::Busy;
    if (sensitive) clearSensitiveBytes(command.payload, sizeof(command.payload));
    return result;
}
} // namespace hostlink::management
