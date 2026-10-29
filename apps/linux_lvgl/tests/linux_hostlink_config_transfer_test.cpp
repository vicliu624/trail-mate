#include "app/linux_app_services.h"
#include "app/linux_tms_store.h"
#include "chat/linux_reticulum_network_store.h"
#include "geocaching/protocol/cmp_reader.h"
#include "geocaching/protocol/cmp_writer.h"
#include "platform/linux/application_lifecycle.h"
#include "platform/linux/geocaching_crypto.h"
#include "platform/linux/hostlink_config_snapshot.h"
#include "platform/linux/hostlink_config_transfer.h"
#include "platform/linux/hostlink_management.h"
#include "platform/linux/runtime_paths.h"
#include "platform/linux/tms_import_bridge.h"
#include "platform/linux/tms_snapshot_bridge.h"
#include "platform/ui/reticulum_call_runtime.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sqlite3.h>
#include <sys/wait.h>
#include <unistd.h>

namespace native = platform::linux_runtime;
namespace tms = app::tms;
namespace mgmt = hostlink::management;
namespace appstore = trailmate::linux_app;
using Status = mgmt::Status;
using Operation = mgmt::Operation;
using Bytes = std::vector<uint8_t>;
using Writer = geocaching::protocol::CmpWriter;
using Reader = geocaching::protocol::CmpReader;
struct Fixture
{
    native::HostlinkConfigSnapshot snapshot;
    native::HostlinkConfigTransfer transfer;
    std::array<uint8_t, 16> session{1}, id{3};
    std::array<uint8_t, 32> base{}, candidate_hash{};
    Bytes send(Operation operation, const Bytes& body, Status expected)
    {
        std::array<uint8_t, 512> encoded{};
        const uint8_t request_id[16]{2};
        size_t size = 0;
        assert(mgmt::encodeEnvelope(session.data(), request_id, uint16_t(operation), body.data(), body.size(), encoded.data(), encoded.size(), size));
        Bytes response;
        assert(native::process_hostlink_management(encoded.data(), size, response, nullptr, nullptr, &snapshot, &transfer) == hostlink::ErrorCode::Ok);
        mgmt::EnvelopeView decoded;
        assert(mgmt::decodeEnvelope(response.data(), response.size(), decoded));
        if (decoded.code != uint16_t(expected)) std::fprintf(stderr, "operation %u: expected %u, received %u\n", unsigned(operation), unsigned(expected), decoded.code);
        assert(decoded.code == uint16_t(expected));
        return {decoded.body, decoded.body + decoded.body_size};
    }
    template <class Encode>
    Bytes body(Encode encode)
    {
        Bytes bytes(448);
        Writer writer(bytes.data(), bytes.size());
        assert(encode(writer));
        bytes.resize(writer.size());
        return bytes;
    }
    Bytes simple(Operation operation, Status expected)
    {
        return send(operation, body([&](Writer& w)
                                    { return w.array(1) && w.binary({id.data(), id.size()}); }),
                    expected);
    }
    Bytes begin(const std::string& candidate, Status expected)
    {
        return send(Operation::TransferBegin, body([&](Writer& w)
                                                   { return w.array(4) && w.binary({id.data(), id.size()}) &&
                                                            w.unsignedInteger(1) && w.unsignedInteger(candidate.size()) && w.binary({candidate_hash.data(), candidate_hash.size()}); }),
                    expected);
    }
    void chunk(const std::string& candidate, size_t offset, size_t size, Status expected = Status::Ok)
    {
        send(Operation::TransferChunk, body([&](Writer& w)
                                            { return w.array(3) && w.binary({id.data(), id.size()}) &&
                                                     w.unsignedInteger(offset) && w.binary({reinterpret_cast<const uint8_t*>(candidate.data() + offset), size}); }),
             expected);
    }
    Bytes commit(Status expected)
    {
        return send(Operation::ConfigCommit, body([&](Writer& w)
                                                  { return w.array(2) && w.binary({id.data(), id.size()}) && w.binary({base.data(), base.size()}); }),
                    expected);
    }
    void upload(const std::string& candidate, appstore::LinuxAppServices& services)
    {
        begin(candidate, Status::Busy);
        services.updateCoreServices();
        begin(candidate, Status::Ok);
        for (size_t offset = 0; offset < candidate.size(); offset += 320) chunk(candidate, offset, std::min<size_t>(320, candidate.size() - offset));
        simple(Operation::ConfigValidate, Status::Ok);
    }
};
static std::array<uint8_t, 32> digest(const std::string& bytes)
{
    std::array<uint8_t, 32> hash{};
    native::LinuxGeocachingCrypto crypto;
    assert(crypto.sha256({reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()}, hash.data()));
    return hash;
}
static void child_check(char* executable, const char* mode)
{
    const auto child = fork();
    assert(child >= 0);
    if (!child)
    {
        execl(executable, executable, mode, static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
int main(int argc, char** argv)
{
#if !defined(TRAIL_MATE_HAS_OPENSSL)
    assert(!native::HostlinkConfigTransfer::supported());
    return 0;
#endif
    native::ApplicationLifecycleOwner lifecycle;
    Fixture fixture;
    const bool interop = argc == 2 && !std::strcmp(argv[1], "--interop");
    FILE* wire = nullptr;
    if (interop)
    {
        wire = fdopen(dup(STDOUT_FILENO), "wb");
        assert(wire && dup2(STDERR_FILENO, STDOUT_FILENO) >= 0);
    }
    if (argc == 2 && !interop)
    {
        int owner = 0;
        native::bind_tms_snapshot(&owner);
        native::bind_tms_import(&owner);
        if (!std::strcmp(argv[1], "--partial"))
        {
            auto body = fixture.simple(Operation::TransferStatus, Status::Ok);
            Reader reader({body.data(), body.size()});
            size_t fields = 0;
            uint64_t value = 0;
            geocaching::ByteView id;
            assert(reader.array(fields, 4) && reader.binary(id, 16) && reader.unsignedInteger(value) && value == 320);
            assert(reader.unsignedInteger(value) && value == 320 && reader.unsignedInteger(value) && value == 0);
        }
        else
        {
            fixture.id[0] = 4;
            // The base hash is supplied by the parent through a small test file.
            FILE* file = std::fopen((native::resolve_paths().cache_root / "base").string().c_str(), "rb");
            assert(file && std::fread(fixture.base.data(), 1, 32, file) == 32);
            std::fclose(file);
            auto response = fixture.commit(Status::Ok);
            Reader reader({response.data(), response.size()});
            size_t fields = 0;
            uint64_t restart = 1;
            geocaching::ByteView id, hash;
            assert(reader.array(fields, 3) && reader.binary(id, 16) && reader.binary(hash, 32) && reader.unsignedInteger(restart) && restart == 0);
            assert(!native::settings_import_committed());
            std::string document;
            assert(appstore::export_linux_tms_snapshot(tms::DocumentKind::Working, document));
            auto parsed = appstore::LinuxTmsDocument::parse(document.data(), document.size(), tms::DocumentKind::Working);
            assert(parsed && !std::strcmp(parsed->config.node_name, "uploaded replacement"));
        }
        native::unbind_tms_import(&owner);
        native::unbind_tms_snapshot(&owner);
        return 0;
    }
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-config-transfer-" + std::to_string(getpid()));
    setenv("TRAIL_MATE_SETTINGS_ROOT", (root / "settings").c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", (root / "cache").c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    setenv("TRAIL_MATE_LORA_DISABLE", "1", 1);
    assert(native::LinuxReticulumNetworkStore::save(chat::reticulum::ReticulumNetworkConfig{}, app::AppConfig{}));
    appstore::LinuxAppServicesOptions options;
    options.runtime_mode = native::LinuxRuntimeMode::DeviceLocal;
    appstore::LinuxAppServices services(options);
    assert(services.initialize() && fixture.transfer.supported());
    if (interop)
    {
        for (;;)
        {
            uint8_t header[4]{};
            const auto read = std::fread(header, 1, 4, stdin);
            if (!read && std::feof(stdin)) break;
            assert(read == 4);
            const size_t size = uint32_t(header[0]) | uint32_t(header[1]) << 8 | uint32_t(header[2]) << 16 | uint32_t(header[3]) << 24;
            assert(size && size <= 512);
            Bytes request(size), response;
            assert(std::fread(request.data(), 1, size, stdin) == size);
            assert(native::process_hostlink_management(request.data(), size, response, nullptr, nullptr, &fixture.snapshot, &fixture.transfer) == hostlink::ErrorCode::Ok);
            services.updateCoreServices();
            for (unsigned i = 0; i < 4; ++i) header[i] = uint8_t(response.size() >> (8 * i));
            assert(std::fwrite(header, 1, 4, wire) == 4 && std::fwrite(response.data(), 1, response.size(), wire) == response.size());
            assert(std::fflush(wire) == 0);
        }
        services.shutdown();
        platform::ui::reticulum_call::end_configuration_change();
        std::fclose(wire);
        std::filesystem::remove_all(root);
        return 0;
    }
    std::string baseline;
    assert(services.exportSettingsDocument(tms::DocumentKind::Working, baseline));
    fixture.base = digest(baseline);
    auto candidate = appstore::LinuxTmsDocument::parse(baseline.data(), baseline.size(), tms::DocumentKind::Working);
    assert(candidate);
    std::strcpy(candidate->config.node_name, "uploaded replacement");
    candidate->settings.speaker_volume = 82;
    std::string document;
    assert(candidate->write(tms::DocumentKind::Working, document));
    fixture.candidate_hash = digest(document);
    fixture.begin(document, Status::Busy);
    services.updateCoreServices();
    fixture.begin(document, Status::Ok);
    fixture.chunk(document, 0, 320);
    fixture.chunk(document, 0, 320); // Lost chunk ACK is idempotent.
    auto altered = document;
    altered[1] ^= 1;
    fixture.chunk(altered, 0, 320, Status::Conflict);
    child_check(argv[0], "--partial");
    fixture.session[0] = 8;
    fixture.simple(Operation::TransferStatus, Status::Unauthorized);
    fixture.session[0] = 1;
    fixture.simple(Operation::ConfigValidate, Status::Conflict);
    fixture.begin(document, Status::Ok);
    for (size_t offset = 320; offset < document.size(); offset += 320) fixture.chunk(document, offset, std::min<size_t>(320, document.size() - offset));
    fixture.simple(Operation::TransferFinish, Status::Ok);
    fixture.base[0] ^= 1;
    fixture.commit(Status::Conflict);
    fixture.base[0] ^= 1;
    // An explicit abort cancels a queued commit before the frontend applies it.
    fixture.commit(Status::Busy);
    fixture.simple(Operation::TransferAbort, Status::Ok);
    services.updateCoreServices();
    assert(!native::settings_import_committed());
    fixture.upload(document, services);
    std::strcpy(services.config().node_name, "local edit after download");
    services.saveConfig();
    fixture.commit(Status::Busy);
    services.updateCoreServices();
    fixture.commit(Status::Conflict);
    assert(!native::settings_import_committed() && !native::application_restart_requested());
    fixture.simple(Operation::TransferAbort, Status::Ok);
    // Capture a new base, preserving the local edit until this new import wins.
    assert(services.exportSettingsDocument(tms::DocumentKind::Working, baseline));
    fixture.base = digest(baseline);
    fixture.id[0] = 4;
    fixture.upload(document, services);
    sqlite3* db = nullptr;
    assert(sqlite3_open(native::sqlite_database_path().string().c_str(), &db) == SQLITE_OK);
    assert(sqlite3_busy_timeout(db, 5000) == SQLITE_OK);
    assert(sqlite3_exec(db, "CREATE TRIGGER reject_receipt BEFORE INSERT ON settings WHEN NEW.namespace='linux_config_transfer' BEGIN SELECT RAISE(ABORT,'receipt'); END;", nullptr, nullptr, nullptr) == SQLITE_OK);
    fixture.commit(Status::Busy);
    services.updateCoreServices();
    fixture.commit(Status::StorageUnavailable);
    assert(!native::settings_import_committed() && !native::application_restart_requested());
    std::string unchanged;
    assert(services.exportSettingsDocument(tms::DocumentKind::Working, unchanged) && unchanged == baseline);
    assert(sqlite3_exec(db, "DROP TRIGGER reject_receipt;", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
    fixture.commit(Status::Busy);
    services.updateCoreServices();
    assert(native::settings_import_committed() && native::application_restart_requested());
    fixture.commit(Status::Ok); // Durable receipt wins even if frontend reply is lost.
    fixture.simple(Operation::TransferAbort, Status::Conflict);
    std::filesystem::create_directories(native::resolve_paths().cache_root);
    FILE* file = std::fopen((native::resolve_paths().cache_root / "base").string().c_str(), "wb");
    assert(file && std::fwrite(fixture.base.data(), 1, 32, file) == 32);
    std::fclose(file);
    child_check(argv[0], "--replay");
    services.shutdown();
    platform::ui::reticulum_call::end_configuration_change();
    std::filesystem::remove_all(root);
    std::puts("HostLink config: durable resume, duplicate chunks, validation, queued abort, atomic base conflict, receipt rollback, restart and fresh-exec commit replay passed");
}
