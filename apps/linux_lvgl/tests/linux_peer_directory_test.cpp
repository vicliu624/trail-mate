#include "platform/linux/peer_directory.h"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <future>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;
using Directory = platform::linux_runtime::LinuxPeerDirectory;
using Record = chat::MeshPeerRecord;
struct Blob final : chat::IMeshPeerDirectoryBlobStore
{
    std::mutex mutex;
    std::condition_variable wake;
    std::vector<uint8_t> bytes;
    bool blocked = false, entered = false, fail = false, ui_io = false;
    std::thread::id ui = std::this_thread::get_id();
    unsigned writes = 0;
    chat::MeshPeerDirectoryBlobLoadResult loadBlob(std::vector<uint8_t>& out) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        ui_io |= std::this_thread::get_id() == ui;
        out = bytes;
        return bytes.empty() ? chat::MeshPeerDirectoryBlobLoadResult::Missing : chat::MeshPeerDirectoryBlobLoadResult::Loaded;
    }
    bool saveBlob(const uint8_t* data, size_t size) override
    {
        std::unique_lock<std::mutex> lock(mutex);
        ui_io |= std::this_thread::get_id() == ui;
        entered = true;
        ++writes;
        wake.notify_all();
        wake.wait(lock, [&]
                  { return !blocked; });
        if (fail) return false;
        bytes.assign(data, data + size);
        return true;
    }
    void clearBlob() override { assert(false); }
};
Record record(uint32_t node)
{
    Record value;
    value.valid = true;
    value.identity = chat::makeMeshPeerNodeIdentity(chat::MeshProtocol::Meshtastic, node);
    value.source = chat::MeshPeerSource::RuntimeRx;
    value.first_seen_s = value.last_seen_s = 100;
    std::strcpy(value.display_name, "Native peer");
    return value;
}
int main()
{
    {
        Blob draining;
        Directory directory(draining);
        assert(directory.begin().succeeded());
        {
            std::lock_guard<std::mutex> lock(draining.mutex);
            draining.blocked = true;
        }
        assert(directory.record(record(10)).succeeded());
        {
            std::unique_lock<std::mutex> lock(draining.mutex);
            assert(draining.wake.wait_for(lock, 3s, [&]
                                          { return draining.entered; }));
        }
        assert(directory.record(record(11)).succeeded());
        auto stopped = std::async(std::launch::async, [&]
                                  { directory.shutdown(); });
        assert(stopped.wait_for(20ms) == std::future_status::timeout);
        {
            std::lock_guard<std::mutex> lock(draining.mutex);
            draining.blocked = false;
            draining.wake.notify_all();
        }
        stopped.get();
        Directory reopened(draining);
        assert(reopened.begin().succeeded());
        Record out;
        assert(reopened.find(record(11).identity, out).succeeded());
    }
    Blob blob;
    {
        Directory directory(blob);
        assert(directory.begin().succeeded());
        {
            std::lock_guard<std::mutex> lock(blob.mutex);
            blob.blocked = true;
        }
        assert(directory.record(record(1)).succeeded());
        {
            std::unique_lock<std::mutex> lock(blob.mutex);
            assert(blob.wake.wait_for(lock, 3s, [&]
                                      { return blob.entered; }));
        }
        auto read = std::async(std::launch::async, [&]
                               {
            Record out;
            assert(directory.find(record(1).identity, out).succeeded());
            assert(directory.record(record(2)).succeeded());
            assert(directory.find(record(2).identity, out).succeeded()); });
        assert(read.wait_for(100ms) == std::future_status::ready);
        read.get();
        auto barrier = std::async(std::launch::async, [&]
                                  { return directory.flush(); });
        assert(barrier.wait_for(20ms) == std::future_status::timeout);
        {
            std::lock_guard<std::mutex> lock(blob.mutex);
            blob.blocked = false;
            blob.wake.notify_all();
        }
        assert(barrier.get().succeeded());
        std::vector<Record> persisted;
        {
            std::lock_guard<std::mutex> lock(blob.mutex);
            assert(chat::MeshPeerDirectoryCore::decodeBlob(persisted, blob.bytes.data(), blob.bytes.size()));
            assert(persisted.size() == 2 && !blob.ui_io);
        }
        chat::MeshPeerUserFlags flags;
        flags.favorite = flags.ignored = true;
        assert(directory.setUserFlags(record(1).identity, flags).succeeded());
        assert(directory.setUserAlias(record(1).identity, "My peer").succeeded());
        auto update = record(1);
        std::strcpy(update.display_name, "Radio observation");
        assert(directory.record(update).succeeded());
        Record found;
        assert(directory.find(update.identity, found).succeeded() && found.flags.favorite && found.flags.ignored && !std::strcmp(found.user_alias, "My peer"));
        struct Visitor final : chat::IMeshPeerDirectoryVisitor
        {
            Directory& directory;
            explicit Visitor(Directory& value) : directory(value) {}
            bool visit(const Record& record) override
            {
                Record found;
                assert(directory.find(record.identity, found).succeeded());
                assert(directory.remove(record.identity).code == chat::MeshPeerDirectoryStatusCode::Busy);
                assert(directory.flush().code == chat::MeshPeerDirectoryStatusCode::Busy);
                return true;
            }
        } visitor(directory);
        assert(directory.visit(chat::MeshProtocol::Meshtastic, chat::MeshPeerDirectoryView::All, visitor).succeeded());
        assert(directory.flush().succeeded());
        {
            std::lock_guard<std::mutex> lock(blob.mutex);
            blob.fail = true;
        }
        assert(directory.record(record(3)).succeeded());
        assert(!directory.flush().succeeded());
        {
            std::lock_guard<std::mutex> lock(blob.mutex);
            blob.fail = false;
        }
        assert(directory.flush().succeeded()); // A fresh barrier retries an earlier failed revision.
        assert(directory.record(record(4)).succeeded());
        directory.shutdown();
        assert(!directory.record(record(5)).succeeded());
    }
    {
        Directory reopened(blob);
        assert(reopened.begin().succeeded());
        Record out;
        assert(reopened.find(record(4).identity, out).succeeded()); // Shutdown drained the accepted observation.
        assert(reopened.find(record(1).identity, out).succeeded() && out.flags.ignored && !std::strcmp(out.user_alias, "My peer"));
    }
    {
        std::lock_guard<std::mutex> lock(blob.mutex);
        blob.bytes.assign(16, 0xff);
        blob.writes = 0;
    }
    {
        Directory corrupt(blob);
        assert(!corrupt.begin().succeeded() && !corrupt.record(record(1)).succeeded());
    }
    {
        std::lock_guard<std::mutex> lock(blob.mutex);
        assert(blob.bytes.size() == 16 && blob.writes == 0 && !blob.ui_io);
    }
}
