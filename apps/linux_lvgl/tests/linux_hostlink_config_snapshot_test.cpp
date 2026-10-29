#include "app/linux_app_services.h"
#include "app/linux_tms_document.h"
#include "chat/linux_reticulum_network_store.h"
#include "geocaching/protocol/cmp_reader.h"
#include "geocaching/protocol/cmp_writer.h"
#include "platform/linux/geocaching_crypto.h"
#include "platform/linux/hostlink_config_snapshot.h"
#include "platform/linux/hostlink_management.h"
#include "platform/linux/tms_snapshot_bridge.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <unistd.h>

namespace native = platform::linux_runtime;
namespace mgmt = hostlink::management;
using Bytes = std::vector<uint8_t>;
using Writer = geocaching::protocol::CmpWriter;
using Reader = geocaching::protocol::CmpReader;

int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("trailmate-config-snapshot-" + std::to_string(getpid()));
    setenv("TRAIL_MATE_SETTINGS_ROOT", (root / "settings").c_str(), 1);
    setenv("TRAIL_MATE_CACHE_ROOT", (root / "cache").c_str(), 1);
    setenv("TRAIL_MATE_SD_ROOT", (root / "sd").c_str(), 1);
    setenv("TRAIL_MATE_LORA_DISABLE", "1", 1);
    native::HostlinkConfigSnapshot snapshot;
    assert(!snapshot.supported());
#if !defined(TRAIL_MATE_HAS_OPENSSL)
    int owner = 0;
    native::bind_tms_snapshot(&owner);
    assert(!snapshot.supported());
    native::unbind_tms_snapshot(&owner);
    std::puts("HostLink config snapshot remains unavailable without SHA-256 support");
    return 0;
#endif
    assert(native::LinuxReticulumNetworkStore::save(chat::reticulum::ReticulumNetworkConfig{}, app::AppConfig{}));
    trailmate::linux_app::LinuxAppServicesOptions options;
    options.runtime_mode = native::LinuxRuntimeMode::DeviceLocal;
    trailmate::linux_app::LinuxAppServices services(options);
    assert(services.initialize() && snapshot.supported());
    uint8_t session[16]{1}, request_id[16]{2};
    auto send = [&](mgmt::Operation operation, const Bytes& body, mgmt::Status expected)
    {
        std::array<uint8_t, 512> encoded{};
        size_t length = 0;
        assert(mgmt::encodeEnvelope(session, request_id, uint16_t(operation), body.data(), body.size(), encoded.data(), encoded.size(), length));
        Bytes response;
        assert(native::process_hostlink_management(encoded.data(), length, response, nullptr, nullptr, &snapshot) == hostlink::ErrorCode::Ok);
        mgmt::EnvelopeView decoded;
        assert(mgmt::decodeEnvelope(response.data(), response.size(), decoded));
        assert(decoded.code == uint16_t(expected));
        return Bytes(decoded.body, decoded.body + decoded.body_size);
    };
    auto page = [&](size_t offset, size_t count, const uint8_t* hash, mgmt::Status expected)
    {
        Bytes body(100);
        Writer writer(body.data(), body.size());
        assert(writer.array(3) && writer.unsignedInteger(offset) && writer.unsignedInteger(count));
        assert(hash ? writer.binary({hash, 32}) : writer.nil());
        body.resize(writer.size());
        return send(mgmt::Operation::ConfigSnapshot, body, expected);
    };
    const Bytes empty{0x90};
    auto capabilities = send(mgmt::Operation::Capabilities, empty, mgmt::Status::Ok);
    {
        Reader reader({capabilities.data(), capabilities.size()});
        size_t count = 0;
        uint64_t value = 0;
        assert(reader.array(count, 6));
        for (unsigned i = 0; i < 4; ++i) assert(reader.unsignedInteger(value));
        assert(reader.array(count, 5) && count == 5);
        for (uint64_t code : {0, 1, 2, 3, 17}) assert(reader.unsignedInteger(value) && value == code);
        assert(reader.unsignedInteger(value) && value == 7 && reader.finished());
    }
    std::strcpy(services.config().node_name, "pending at first snapshot");
    services.saveConfig();
    assert(page(0, 320, nullptr, mgmt::Status::Busy) == empty);
    services.updateCoreServices();
    auto body = page(0, 320, nullptr, mgmt::Status::Ok);
    std::array<uint8_t, 32> expected_hash{};
    std::string complete;
    uint64_t total = 0;
    auto append = [&](const Bytes& response)
    {
        Reader reader({response.data(), response.size()});
        size_t fields = 0;
        uint64_t offset = 0, size = 0;
        geocaching::ByteView hash, chunk;
        assert(reader.array(fields, 4) && fields == 4 && reader.unsignedInteger(offset) && reader.unsignedInteger(size));
        assert(offset == complete.size() && reader.binary(hash, 32) && hash.size == 32);
        if (complete.empty())
        {
            std::copy_n(hash.data, 32, expected_hash.begin());
            total = size;
        }
        assert(size == total && !std::memcmp(hash.data, expected_hash.data(), 32));
        assert(reader.binary(chunk, 320) && reader.finished());
        complete.append(reinterpret_cast<const char*>(chunk.data), chunk.size);
    };
    append(body);
    std::strcpy(services.config().node_name, "changed during pagination");
    services.saveConfig();
    services.updateCoreServices();
    page(320, 320, nullptr, mgmt::Status::InvalidField);
    std::array<uint8_t, 32> wrong_hash{};
    page(320, 320, wrong_hash.data(), mgmt::Status::Conflict);
    session[0] = 9;
    page(320, 320, expected_hash.data(), mgmt::Status::Unauthorized);
    send(mgmt::Operation::SessionClose, empty, mgmt::Status::Ok);
    session[0] = 1;
    while (complete.size() < total) append(page(complete.size(), 320, expected_hash.data(), mgmt::Status::Ok));
    native::LinuxGeocachingCrypto crypto;
    std::array<uint8_t, 32> actual{};
    assert(crypto.sha256({reinterpret_cast<const uint8_t*>(complete.data()), complete.size()}, actual.data()) && actual == expected_hash);
    auto document = trailmate::linux_app::LinuxTmsDocument::parse(complete.data(), complete.size(), app::tms::DocumentKind::Working);
    assert(document && !std::strcmp(document->config.node_name, "pending at first snapshot"));
    page(0, 0, nullptr, mgmt::Status::InvalidField);
    page(0, 321, nullptr, mgmt::Status::InvalidField);
    send(mgmt::Operation::ConfigCommit, empty, mgmt::Status::Unsupported);
    send(mgmt::Operation::SessionClose, empty, mgmt::Status::Ok);
    page(320, 320, expected_hash.data(), mgmt::Status::Expired);
    page(0, 320, nullptr, mgmt::Status::Busy);
    native::service_tms_snapshot(&services, [](void*, std::string&)
                                 { return false; });
    page(0, 320, nullptr, mgmt::Status::StorageUnavailable);
    page(0, 320, nullptr, mgmt::Status::Busy);
    snapshot.close(); // Cancel a queued frontend read before disconnect.
    std::string discarded;
    assert(native::request_tms_snapshot(discarded) == native::TmsSnapshotRead::Busy);
    services.updateCoreServices();
    assert(native::request_tms_snapshot(discarded) == native::TmsSnapshotRead::Ready);
    document = trailmate::linux_app::LinuxTmsDocument::parse(discarded.data(), discarded.size(), app::tms::DocumentKind::Working);
    assert(document && !std::strcmp(document->config.node_name, "changed during pagination"));
    services.shutdown();
    assert(!snapshot.supported());
    std::filesystem::remove_all(root);
    std::puts("HostLink config snapshot: frontend flush, immutable paged TMSET7, hashes, ownership, cancellation, errors and exact capability advertisement passed");
}
