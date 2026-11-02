#include "platform/linux/oneseg_iq_replay.h"
#include <cassert>
#include <cstdlib>
#include <fstream>
using namespace platform::linux_runtime;
struct Replay
{
    std::vector<uint8_t> bytes;
    size_t stop = 0;
    bool reject = false;
};
int main()
{
    char directory[] = "/tmp/trailmate-iq-archive-XXXXXX";
    assert(mkdtemp(directory));
    const std::string path = std::string(directory) + "/capture.tmiq";
    std::string error;
    OneSegIqArchive archive;
    std::vector<uint8_t> data(90000);
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(i * 37);
    assert(!archive.append(data.data(), data.size(), 0));
    assert(!archive.begin({}, 3));
    assert(archive.begin({}, 1024));
    assert(!archive.append(data.data(), 100, 1));
    assert(archive.append(data.data(), 100, 0));
    assert(archive.begin({}, data.size())); // New generation resets continuity.
    assert(archive.append(data.data(), data.size(), 0));
    assert(archive.size() == data.size() && archive.metadata().full);
    assert(archive.save(path, error));
    assert(!archive.save(path, error)); // Never replace a previous recording.
    OneSegIqArchive loaded;
    assert(loaded.load(path, error));
    assert(loaded.size() == data.size() && loaded.metadata().rate == 450000);
    assert(!std::memcmp(loaded.data(), data.data(), data.size()));
    Replay output;
    std::array<uint8_t, 2048> staging{};
    OneSegIqRequest request;
    request.context = &output;
    request.staging = staging.data();
    request.staging_bytes = staging.size();
    request.accept = [](void* context, const uint8_t* bytes, size_t count, uint64_t first)
    {
        auto& s = *static_cast<Replay*>(context);
        assert(first * 2 == s.bytes.size());
        if (s.reject) return false;
        s.bytes.insert(s.bytes.end(), bytes, bytes + count);
        return true;
    };
    request.cancelled = [](void* context)
    {
        const auto& s = *static_cast<Replay*>(context);
        return s.stop && s.bytes.size() >= s.stop;
    };
    OneSegIqResult result;
    assert(replay_oneseg_iq(loaded, request, result, false));
    assert(output.bytes == data && !result.cancelled && !result.sink_rejected);
    assert(result.elapsed_us >= 80000 && result.elapsed_us < 2000000);
    output = {};
    output.stop = data.size() + 4096;
    assert(replay_oneseg_iq(loaded, request, result, true));
    assert(result.cancelled && output.bytes.size() >= output.stop);
    assert(!std::memcmp(output.bytes.data() + data.size(), data.data(), output.bytes.size() - data.size()));
    output = {};
    output.reject = true;
    assert(!replay_oneseg_iq(loaded, request, result, false) && result.sink_rejected);
    {
        std::fstream corrupted(path, std::ios::in | std::ios::out | std::ios::binary);
        corrupted.seekp(70);
        corrupted.put('\x7f');
    }
    assert(!loaded.load(path, error) && !loaded.size());
    assert(!loaded.load("relative.tmiq", error));
    assert(::unlink(path.c_str()) == 0);
    const auto pipe = std::string(directory) + "/pipe";
    assert(::mkfifo(pipe.c_str(), 0600) == 0);
    assert(!loaded.load(pipe, error)); // Invalid inputs cannot block on a FIFO.
    assert(::unlink(pipe.c_str()) == 0);
    assert(::rmdir(directory) == 0);
}
