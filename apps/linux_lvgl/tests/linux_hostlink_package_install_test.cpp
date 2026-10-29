#include "hostlink/route_storage_lease.h"
#include "platform/linux/hostlink_package_install.h"
#include "platform/linux/hostlink_upload_io.h"
#include "platform/linux/hostlink_uploads.h"
#include "platform/linux/route_image_tasks.h"
#include "platform/ui/route_storage.h"
#include "signed_route_package_fixture.h"
#include "track_sharing/installed_package_binding.h"
#include "track_sharing/package_cache_paths.h"
#include "track_sharing/prepare_request.h"
#include <chrono>
#include <sqlite3.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
namespace native = platform::linux_runtime;
namespace ts = track_sharing;
namespace mgmt = hostlink::management;
using namespace package_fixture;
using Id = std::array<uint8_t, 16>;
static Id id(uint8_t value)
{
    Id output{};
    output[0] = value;
    return output;
}
static void write(const fs::path& path, const Bytes& bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    assert(stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}
static Bytes read(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    assert(stream);
    return Bytes(std::istreambuf_iterator<char>(stream), {});
}
static unsigned state(const Bytes& body)
{
    geocaching::protocol::CmpReader reader({body.data(), body.size()});
    size_t fields = 0;
    uint64_t value = 0;
    geocaching::ByteView operation;
    assert(reader.array(fields, 5) && fields == 5 && reader.binary(operation, 16) && operation.size == 16 && reader.unsignedInteger(value) && value <= 2);
    if (!value) assert(reader.nil() && reader.nil() && reader.nil());
    else
    {
        geocaching::ByteView object, revision;
        std::string_view path;
        assert(reader.binary(object, 32) && object.size == 32 && reader.binary(revision, 32) && revision.size == 32 && reader.text(path, 80));
    }
    assert(reader.finished());
    return unsigned(value);
}
int main(int argc, char** argv)
{
    assert(argc == 2);
    const auto root = fs::temp_directory_path() / ("trailmate-package-install-" + std::to_string(getpid()));
    const auto sd = root / "sd", database = root / "identity.sqlite3";
    fs::create_directories(sd);
    setenv("TRAIL_MATE_SD_ROOT", sd.c_str(), 1);
    auto padded = read(argv[1]);
    padded.resize(padded.size() + 200000, 0);
    const auto image = root / "padded.jpg";
    write(image, padded);
    auto fixture = make(image);
    native::HostlinkUploads uploads(sd, database);
    native::HostlinkPackageInstall installer(sd, database, uploads);
    uint8_t session[16]{1}, other[16]{2};
    uint64_t now = 1;
    const auto kml = id(1), signed_id = id(2), index = id(3), resource = id(4), mapping = id(5), operation = id(9);
    const auto upload = [&](const Id& transfer, unsigned kind, const Bytes& bytes, bool close = true)
    {
        const auto digest = hash(bytes);
        const auto call = [&](mgmt::Operation op, const Bytes& body)
        { mgmt::EnvelopeView request{session, nullptr, uint16_t(op), body.data(), body.size()}; uint8_t output[448]; size_t size = 0; assert(uploads.process(request, now++, output, sizeof(output), size) == mgmt::Status::Ok); return Bytes(output, output + size); };
        call(mgmt::Operation::TransferBegin, encode([&](Writer& w)
                                                    { w.array(4); w.binary({transfer.data(), 16}); w.unsignedInteger(kind); w.unsignedInteger(bytes.size()); w.binary({digest.data(), 32}); }));
        for (size_t offset = 0; offset < bytes.size(); offset += 320)
            call(mgmt::Operation::TransferChunk, encode([&](Writer& w)
                                                        { w.array(3); w.binary({transfer.data(), 16}); w.unsignedInteger(offset); w.binary({bytes.data() + offset, std::min<size_t>(320, bytes.size() - offset)}); }));
        const auto finish = encode([&](Writer& w)
                                   { w.array(1); w.binary({transfer.data(), 16}); });
        for (unsigned step = 0;; ++step)
        {
            assert(step < 1000);
            if (call(mgmt::Operation::TransferFinish, finish).back() == 1) break;
            uploads.advance(now++, true);
        }
        if (close) uploads.close(session);
    };
    upload(kml, 3, fixture.kml);
    upload(signed_id, 11, fixture.signed_manifest);
    upload(index, 8, fixture.index);
    upload(resource, 9, fixture.image);
    upload(mapping, 10, encode([&](Writer& w)
                               { w.array(3); w.unsignedInteger(1); w.binary({index.data(), 16}); w.binary({resource.data(), 16}); }),
           false);
    const auto request = [&](bool query, uint32_t generation = 1)
    {
        return encode([&](Writer& w)
                      { w.array(query ? 4 : 8); w.binary({operation.data(), 16}); if (!query) { w.binary({kml.data(), 16}); w.binary({signed_id.data(), 16}); }
            w.unsignedInteger(generation); if (!query) { w.nil(); w.binary({mapping.data(), 16}); } w.binary({fixture.object.data(), 32}); w.binary({fixture.revision.data(), 32}); });
    };
    const auto call = [&](native::HostlinkPackageInstall& target, bool query, mgmt::Status expected = mgmt::Status::Ok, const uint8_t* owner = nullptr, uint32_t generation = 1)
    { const auto body = request(query, generation); mgmt::EnvelopeView message{owner ? owner : session, nullptr, uint16_t(query ? mgmt::Operation::TrackPackageInstallStatus : mgmt::Operation::TrackPackageInstall), body.data(), body.size()}; uint8_t output[448]; size_t size = 0; assert(target.process(message, now++, output, sizeof(output), size) == expected); return Bytes(output, output + size); };
    char logical[80]{};
    assert(ts::routePackageTarget({fixture.revision.data(), 32}, ts::PackageRouteFile::Kml, logical, sizeof(logical)));
    const auto final = sd / (logical + 1);
    const auto name = final.filename().string();
    {
        hostlink::route_storage_lease::Guard external;
        assert(external.acquired());
        call(installer, false, mgmt::Status::Busy);
        hostlink::route_storage_lease::Guard blocked;
        assert(!blocked.acquired());
    }
    assert(state(call(installer, false)) == 0 && !fs::exists(final));
    assert(!platform::ui::route_storage::remove_route(name));
    call(installer, false, mgmt::Status::Conflict, other);
    const auto poll = [&](native::HostlinkPackageInstall& target, bool query, unsigned expected)
    {
        for (unsigned step = 0;; ++step)
        {
            assert(step < 1000);
            target.advance(now++, true);
            const auto current = state(call(target, query));
            if (fs::exists(final)) assert(fs::exists(final.parent_path() / (final.stem().string() + ".rpk")));
            if (current)
            {
                assert(current == expected);
                return;
            }
        }
    };
    poll(installer, false, 1);
    assert(read(final) == fixture.kml);
    {
        std::vector<std::string> names;
        assert(platform::ui::route_storage::list_routes(names, 64) && names == std::vector<std::string>{name});
        for (const char* hidden : {"metadata.rpk", "partial.rpt", "partial.ktm", "photo.jpg"}) write(final.parent_path() / hidden, Bytes{1});
        write(final.parent_path() / "UPPER.KML", fixture.kml);
        const auto outside = root / "outside.kml";
        write(outside, fixture.kml);
        fs::create_symlink(outside, final.parent_path() / "external.kml");
        assert(platform::ui::route_storage::list_routes(names, 64) && names.size() == 2 && std::find(names.begin(), names.end(), "UPPER.KML") != names.end());
        assert(platform::ui::route_storage::list_routes(names, 1) && names.size() == 1);
        assert(!platform::ui::route_storage::remove_route(outside.string()) && !platform::ui::route_storage::remove_route("../" + name));
        assert(!platform::ui::route_storage::remove_route("metadata.rpk") && !platform::ui::route_storage::remove_route("external.kml"));
        assert(read(outside) == fixture.kml);
        for (const char* hidden : {"metadata.rpk", "partial.rpt", "partial.ktm", "photo.jpg", "UPPER.KML", "external.kml"}) fs::remove(final.parent_path() / hidden);
    }
    // Installed public-cache bytes feed the same image worker used by P4 UI.
    std::string asset_root, error;
    const auto asset_id = final.stem().string();
    assert(platform::ui::route_storage::ensure_route_asset_dir(asset_id, asset_root));
    platform::ui::route_storage::RouteImageCacheItem image_item;
    image_item.source_path = (fs::path(asset_root) / "images/img-0001.jpg").string();
    image_item.source_hash_valid = platform::ui::route_storage::route_image_reference_hash("photos/example.jpg", image_item.source_hash);
    assert(platform::ui::route_storage::start_route_image_cache_build(asset_id, {image_item}, error,
                                                                      platform::ui::route_storage::RouteImageTaskPresentation::PageOnly, final.string()));
    const auto image_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (platform::ui::route_storage::route_image_download_status().busy && std::chrono::steady_clock::now() < image_deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(platform::ui::route_storage::route_image_download_status().phase == platform::ui::route_storage::RouteImageDownloadPhase::Done);
    assert(read(image_item.source_path) == fixture.image && fs::file_size(fs::path(asset_root) / "views/view-0001.bmp") > 54);
    native::stop_route_image_tasks();
    assert(state(call(installer, false)) == 1); // Transaction retry is idempotent.
    {
        hostlink::route_storage_lease::Guard guard;
        assert(guard.acquired());
        installer.close(session);
        hostlink::route_storage_lease::Guard blocked;
        assert(!blocked.acquired());
    }
    assert(state(call(installer, true)) == 0);
    poll(installer, true, 1);
    char resource_path[160]{};
    assert(ts::resolvePackageCachePath({fixture.revision.data(), 32}, ts::ObjectFile::AttachmentResource, {fixture.image_hash.data(), 32}, resource_path, sizeof(resource_path)));
    const auto retained = sd / (resource_path + 1);
    assert(read(retained) == fixture.image);
    auto damaged = fixture.image;
    damaged.back() ^= 1;
    write(retained, damaged);
    assert(state(call(installer, true)) == 0);
    bool rejected = false;
    for (unsigned step = 0; step < 1000 && !rejected; ++step)
    {
        installer.advance(now++, true);
        const auto body = request(true);
        mgmt::EnvelopeView message{session, nullptr, uint16_t(mgmt::Operation::TrackPackageInstallStatus), body.data(), body.size()};
        uint8_t output[448];
        size_t size = 0;
        const auto result = installer.process(message, now++, output, sizeof(output), size);
        if (result == mgmt::Status::HashMismatch) rejected = true;
        else assert(result == mgmt::Status::Ok && state(Bytes(output, output + size)) == 0);
    }
    assert(rejected);
    installer.close(session);
    assert(state(call(installer, false)) == 0);
    rejected = false;
    for (unsigned step = 0; step < 1000 && !rejected; ++step)
    {
        installer.advance(now++, true);
        const auto body = request(false);
        mgmt::EnvelopeView message{session, nullptr, uint16_t(mgmt::Operation::TrackPackageInstall), body.data(), body.size()};
        uint8_t output[448];
        size_t size = 0;
        const auto result = installer.process(message, now++, output, sizeof(output), size);
        if (result == mgmt::Status::HashMismatch) rejected = true;
        else assert(result == mgmt::Status::Ok && state(Bytes(output, output + size)) == 0);
    }
    assert(rejected && read(retained) == damaged && read(final) == fixture.kml);
    write(retained, fixture.image);
    assert(state(call(installer, true)) == 0);
    poll(installer, true, 1);
    assert(platform::ui::route_storage::remove_route(final.string()));
    assert(state(call(installer, true)) == 2); // Fresh disk query sees missing route.
    assert(state(call(installer, false)) == 0);
    poll(installer, false, 1);
    {
        native::HostlinkPackageInstall restarted(sd, database, uploads);
        assert(state(call(restarted, true)) == 0);
        poll(restarted, true, 1);
    }
    installer.close(session);
    assert(state(call(installer, false)) == 0);
    installer.advance(now++, false);
    {
        hostlink::route_storage_lease::Guard guard;
        assert(guard.acquired());
    }
    assert(state(call(installer, false)) == 0);
    now += 30000;
    installer.advance(now++, true);
    {
        hostlink::route_storage_lease::Guard guard;
        assert(guard.acquired());
    }
    assert(state(call(installer, false)) == 0);
    sqlite3* db = nullptr;
    assert(sqlite3_open(database.c_str(), &db) == SQLITE_OK);
    assert(sqlite3_exec(db, "UPDATE linux_reticulum_identity SET generation=generation+1", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
    installer.advance(now++, true);
    call(installer, false, mgmt::Status::StaleIdentity);
    installer.close(session);
    assert(state(call(installer, true, mgmt::Status::Ok, nullptr, 2)) == 0);
    // Query is independent of local upload ownership after installation.
    for (unsigned step = 0;; ++step)
    {
        assert(step < 1000);
        installer.advance(now++, true);
        if (state(call(installer, true, mgmt::Status::Ok, nullptr, 2)) == 1) break;
    }
    installer.close(session);
    {
        native::UploadIo io(sd);
        assert(io.lock() && io.mkdirs("/publish-test"));
        write(sd / "publish-test/source", Bytes{1, 2});
        write(sd / "publish-test/final", Bytes{3, 4});
        assert(!io.publish("/publish-test/source", "/publish-test/final"));
        assert(read(sd / "publish-test/final") == (Bytes{3, 4}) && read(sd / "publish-test/source") == (Bytes{1, 2}));
        fs::remove(sd / "publish-test/final");
        assert(io.publish("/publish-test/source", "/publish-test/final") && read(sd / "publish-test/final") == (Bytes{1, 2}));
        assert(io.mkdirs("/quota-test"));
        write(sd / "quota-test/a", Bytes(7, 1));
        assert(!io.admit_copy("/quota-test/tmp", "/quota-test/final", 4, 10, 3));
        assert(io.admit_copy("/quota-test/tmp", "/quota-test/final", 3, 10, 3));
        assert(io.mkdirs("/directory-budget/a") && io.mkdirs("/directory-budget/b"));
        assert(io.admit_directory("/directory-budget", "a", 2) && !io.admit_directory("/directory-budget", "c", 2));
    }
    fs::remove_all(root);
}
