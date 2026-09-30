#include "platform/esp/arduino_common/chat/infra/reticulum/gateway_persistence.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static std::map<std::string, std::vector<uint8_t>> files;
static bool ready = true, external = false, busy = false, partial = false, flush_ok = true, swap_on_read = false;
static uint32_t session = 1;
static unsigned reads = 0, writes = 0;
namespace platform::esp::arduino_common::storage
{
bool sd_card_ready() { return ready; }
bool sd_external_block_owner_active() { return external; }
uint32_t sd_media_session() { return session; }
bool sd_is_directory(const char*) { return true; }
bool sd_mkdir(const char*) { return true; }
SdFileReadResult sd_read_file(const char* path, uint8_t* out, size_t capacity)
{
    ++reads;
    if (busy) return {SdFileReadStatus::Busy};
    const auto found = files.find(path);
    if (found == files.end()) return {SdFileReadStatus::Missing};
    const auto count = std::min(capacity, found->second.size());
    std::memcpy(out, found->second.data(), count);
    if (swap_on_read)
    {
        ++session;
        swap_on_read = false;
    }
    return {SdFileReadStatus::Ready, count, found->second.size()};
}
bool SdRuntimeFile::open(const char* path, const char*, uint32_t expected)
{
    if (!ready || external || expected != session) return false;
    path_ = path;
    files[path_].clear();
    return true;
}
size_t SdRuntimeFile::write(const void* data, size_t size)
{
    ++writes;
    const auto count = partial ? size / 2 : size;
    const auto* bytes = static_cast<const uint8_t*>(data);
    files[path_] = {bytes, bytes + count};
    return count;
}
bool SdRuntimeFile::flush()
{
    if (!flush_ok) files[path_].resize(8); // Simulate lost unflushed sectors.
    return flush_ok;
}
} // namespace platform::esp::arduino_common::storage

int main()
{
    using Cache = chat::reticulum::GatewayPersistence;
    Cache::Endpoint endpoint;
    std::strcpy(endpoint.host, "stable.example.org");
    endpoint.port = 4242;
    endpoint.network_identity[0] = 1;
    Cache cache;
    cache.poll(0, false);
    assert(!cache.restored() && reads == 2);
    cache.installed(endpoint);
    cache.poll(60000, false);
    assert(writes == 0); // Observing an announcement is not enough to persist it.
    cache.poll(60000, true);
    assert(writes == 1 && cache.restored());
    for (unsigned i = 0; i < 100; ++i) cache.poll(60000 + i, true);
    assert(writes == 1 && reads == 2); // No repeated I/O for unchanged state.
    Cache reboot;
    reboot.poll(0, false);
    assert(reboot.restored() && std::strcmp(reboot.restored()->host, endpoint.host) == 0);
    assert(reboot.restored()->network_identity[0] == 1);
    const auto good = files;
    std::strcpy(endpoint.host, "replacement.example.org");
    cache.installed(endpoint);
    cache.poll(120000, true);
    assert(writes == 1); // Ten-minute successful-write rate limit.
    partial = true;
    cache.poll(660000, true);
    partial = false;
    Cache interrupted;
    interrupted.poll(0, false);
    assert(interrupted.restored() && std::strcmp(interrupted.restored()->host, "stable.example.org") == 0);
    cache.poll(665000, true);
    Cache newest;
    newest.poll(0, false);
    assert(newest.restored() && std::strcmp(newest.restored()->host, endpoint.host) == 0);
    files = good;
    Cache retry;
    busy = true;
    retry.poll(0, false);
    assert(!retry.restored());
    busy = false;
    const auto busy_reads = reads;
    retry.poll(4999, false);
    assert(reads == busy_reads);
    retry.poll(5000, false);
    assert(retry.restored());
    retry.installed(endpoint);
    flush_ok = false;
    retry.poll(10000, true);
    flush_ok = true;
    Cache unflushed;
    unflushed.poll(0, false);
    assert(unflushed.restored() && std::strcmp(unflushed.restored()->host, "stable.example.org") == 0);
    files = good;
    Cache swapped;
    swap_on_read = true;
    swapped.poll(0, false);
    assert(!swapped.restored());
    files.clear();
    swapped.poll(1, false);
    assert(!swapped.restored()); // Old card's endpoint does not leak into restore.
    files = good;
    for (auto& file : files) file.second[12] ^= 1;
    Cache corrupt;
    corrupt.poll(0, false);
    assert(!corrupt.restored());
    ready = false;
    const auto before = reads;
    Cache absent;
    absent.poll(0, false);
    assert(reads == before);
    ready = true;
    external = true;
    absent.poll(6000, false);
    assert(reads == before);
    return 0;
}
