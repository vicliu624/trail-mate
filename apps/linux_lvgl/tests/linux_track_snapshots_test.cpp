#include "geocaching/protocol/cmp_reader.h"
#include "geocaching/protocol/cmp_writer.h"
#include "platform/linux/hostlink_track_catalog.h"
#include "platform/linux/hostlink_track_snapshots.h"
#include "platform/linux/hostlink_upload_io.h"
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sys/file.h>
#include <unistd.h>
#include <vector>

// Link-time interposition is confined to this test executable. Change only
// the directory at the exact source-lock admission boundary, without adding
// a test hook to the production storage adapter.
unsigned change_on_lock = 0;
std::filesystem::path changed_directory;
extern "C" int __real_flock(int, int);
extern "C" int __wrap_flock(int file, int operation)
{
    if (change_on_lock && --change_on_lock == 0)
    {
        const auto stamp = std::filesystem::last_write_time(changed_directory);
        std::ofstream(changed_directory / "concurrent-entry") << "changed directory";
        std::filesystem::last_write_time(changed_directory, stamp + std::chrono::seconds(2));
    }
    return __real_flock(file, operation);
}

namespace mgmt = hostlink::management;
namespace native = platform::linux_runtime;
using Writer = geocaching::protocol::CmpWriter;
using Reader = geocaching::protocol::CmpReader;
using Status = mgmt::Status;
using Operation = mgmt::Operation;
std::vector<uint8_t> encode(const std::function<void(Writer&)>& body)
{
    std::array<uint8_t, 448> output{};
    Writer writer(output.data(), output.size());
    body(writer);
    return {output.begin(), output.begin() + writer.size()};
}
struct Fixture
{
    Fixture() : root(std::filesystem::temp_directory_path() / ("trailmate-snapshot-" + std::to_string(getpid()))), snapshots(root)
    {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "tracks");
        session.fill(1);
        id.fill(2);
        std::ofstream(root / "tracks/track.gpx") << std::string(100000, 'a');
        media = token();
    }
    ~Fixture()
    {
        std::filesystem::remove_all(root);
        std::filesystem::remove_all(root.string() + "-old");
    }
    uint32_t token()
    {
        native::UploadIo io(root);
        std::array<uint8_t, 32> author{};
        return io.media_token(session.data(), 0, author.data(), "/tracks");
    }
    std::vector<uint8_t> selection(uint32_t bytes = 100000)
    {
        return encode([&](Writer& w)
                      { assert(w.array(5) && w.binary({id.data(), 16}) && w.unsignedInteger(media) && w.text("track.gpx") && w.unsignedInteger(bytes) && w.unsignedInteger(1)); });
    }
    std::vector<uint8_t> id_body()
    {
        return encode([&](Writer& w)
                      { assert(w.array(1) && w.binary({id.data(), 16})); });
    }
    std::vector<uint8_t> read_body(const uint8_t* hash = nullptr)
    {
        return encode([&](Writer& w)
                      { assert(w.array(4) && w.binary({id.data(), 16}) && w.unsignedInteger(0) && w.unsignedInteger(320) && (hash ? w.binary({hash, 32}) : w.nil())); });
    }
    std::vector<uint8_t> call(Operation op, const std::vector<uint8_t>& body, Status expected = Status::Ok)
    {
        mgmt::EnvelopeView request;
        request.session = session.data();
        request.request = id.data();
        request.code = uint16_t(op);
        request.body = body.data();
        request.body_size = body.size();
        std::array<uint8_t, 448> output{};
        size_t written = 0;
        const auto status = snapshots.process(request, now, output.data(), output.size(), written);
        assert(status == expected);
        return {output.begin(), output.begin() + written};
    }
    unsigned state()
    {
        const auto output = call(Operation::TrackSnapshotBuild, selection());
        Reader reader({output.data(), output.size()});
        size_t count;
        geocaching::ByteView key;
        uint64_t state;
        assert(reader.array(count, 5) && reader.binary(key, 16) && reader.unsignedInteger(state));
        return unsigned(state);
    }
    void complete()
    {
        for (unsigned step = 0; step < 30 && state() == 1; ++step)
        {
            now += 100;
            snapshots.advance(now, true);
        }
        assert(state() == 3);
        assert(!snapshots.identityBusy());
    }
    std::filesystem::path root;
    native::HostlinkTrackSnapshots snapshots;
    std::array<uint8_t, 16> session{}, id{};
    uint32_t media = 0;
    uint64_t now = 100;
};
int main(int argc, char** argv)
{
    assert(argc == 2 && native::HostlinkTrackSnapshots::supported());
    Fixture f;
    const std::string scenario(argv[1]);
    if (scenario == "admission")
    {
        changed_directory = f.root / "tracks";
        change_on_lock = 2; // Global storage lease, then the source file lock.
        assert(f.state() == 1);
        for (unsigned step = 0; step < 20 && f.state() == 1; ++step) f.snapshots.advance(++f.now, true);
        assert(change_on_lock == 0 && f.state() == 4 && !f.snapshots.identityBusy());
        f.call(Operation::TrackSnapshot, f.read_body(), Status::Expired);
    }
    else if (scenario == "lease")
    {
        native::UploadIo recorder(f.root);
        const auto file = recorder.open("/tracks/track.gpx", native::UploadIo::Mode::Read);
        assert(file.handle && recorder.lock_open(file.handle));
        f.call(Operation::TrackSnapshotBuild, f.selection(), Status::Busy);
        assert(!f.snapshots.identityBusy());
        recorder.close(nullptr);
        assert(f.state() == 1);
        f.complete();
    }
    else if (scenario == "mutation")
    {
        assert(f.state() == 1);
        assert(f.snapshots.identityBusy());
        f.snapshots.advance(f.now, true);
        std::ofstream(f.root / "tracks/track.gpx") << std::string(100000, 'b');
        // Timestamp equality can defer detection until the bounded content
        // verification phase. Drive the scheduler to its terminal result.
        for (unsigned step = 0; step < 20 && f.state() == 1; ++step) f.snapshots.advance(++f.now, true);
        assert(f.state() == 4);
        assert(!f.snapshots.identityBusy());
        f.call(Operation::TrackSnapshot, f.read_body(), Status::Expired);
    }
    else if (scenario == "cancel")
    {
        assert(f.state() == 1);
        f.session.fill(3);
        f.call(Operation::TrackSnapshotCancel, f.id_body(), Status::Unauthorized);
        f.session.fill(1);
        f.call(Operation::TrackSnapshotCancel, f.id_body());
        f.snapshots.advance(++f.now, true);
        assert(f.state() == 5 && !f.snapshots.identityBusy());
        f.call(Operation::TrackSnapshot, f.read_body(), Status::Expired);
    }
    else if (scenario == "disconnect")
    {
        assert(f.state() == 1);
        f.snapshots.advance(++f.now, false);
        assert(!f.snapshots.identityBusy());
        f.call(Operation::TrackSnapshot, f.read_body(), Status::Expired);
        assert(f.state() == 1);
        f.complete();
    }
    else if (scenario == "media")
    {
        assert(f.state() == 1);
        std::filesystem::rename(f.root, f.root.string() + "-old");
        std::filesystem::create_directories(f.root / "tracks");
        std::ofstream(f.root / "sentinel") << "new medium";
        f.snapshots.advance(++f.now, true);
        assert(f.state() == 4);
        assert(std::filesystem::exists(f.root / "sentinel"));
        f.call(Operation::TrackSnapshot, f.read_body(), Status::Expired);
    }
    else if (scenario == "expiry")
    {
        assert(f.state() == 1);
        f.now += 30001;
        f.snapshots.advance(f.now, true);
        assert(!f.snapshots.identityBusy());
        f.call(Operation::TrackSnapshot, f.read_body(), Status::Expired);
    }
    else if (scenario == "quota")
    {
        std::filesystem::create_directories(f.root / "trailmate/web-tracks");
        for (unsigned index = 0; index < 8; ++index) std::ofstream(f.root / ("trailmate/web-tracks/unknown-" + std::to_string(index))) << "retain";
        assert(f.state() == 1);
        f.snapshots.advance(++f.now, true);
        assert(f.state() == 4 && !f.snapshots.identityBusy());
        assert(std::distance(std::filesystem::directory_iterator(f.root / "trailmate/web-tracks"), std::filesystem::directory_iterator{}) == 8);
    }
    else
    {
        assert(scenario == "commit");
        assert(f.state() == 1);
        f.complete();
        auto read = f.call(Operation::TrackSnapshot, f.read_body());
        assert(read.size() > 320);
        std::array<uint8_t, 32> wrong{};
        f.call(Operation::TrackSnapshot, f.read_body(wrong.data()), Status::Conflict);
        f.session.fill(3);
        f.call(Operation::TrackSnapshotRelease, f.id_body(), Status::Unauthorized);
        f.session.fill(1);
        f.snapshots.advance(f.now, false); // Sealed data survives disconnection.
        f.call(Operation::TrackSnapshot, f.read_body());
        std::ofstream(f.root / "tracks/track.gpx") << "changed after seal";
        assert(f.call(Operation::TrackSnapshot, f.read_body()) == read);
        f.call(Operation::TrackSnapshotRelease, f.id_body());
        f.call(Operation::TrackSnapshotRelease, f.id_body());
        f.call(Operation::TrackSnapshot, f.read_body(), Status::Expired);
    }
}
