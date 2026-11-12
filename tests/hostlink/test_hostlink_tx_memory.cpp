#include "hostlink/hostlink_codec.h"
#include "hostlink/sensitive_memory.h"
#include "platform/esp/arduino_common/hostlink/hostlink_service.h"
#include <cassert>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <map>
namespace hostlink
{
constexpr uint32_t MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_INTERNAL = 2, MALLOC_CAP_8BIT = 4;
constexpr int pdPASS = 1;
#include "hostlink_tx_item_actual.inc"
namespace download_mode
{
unsigned discarded = 0;
uint32_t last_cookie = 0;
void reply_completed(uint32_t cookie, bool delivered)
{
    assert(cookie && !delivered);
    ++discarded;
    last_cookie = cookie;
}
} // namespace download_mode
TxItem queued;
void* s_tx_queue = &queued;
using QueueHandle_t = void*;
std::map<void*, size_t> allocation_sizes;
bool full = false, ready = true, fail_allocation = false;
unsigned allocations = 0, frees = 0;
int s_session;
bool is_ready(int) { return ready; }
uint16_t next_seq() { return 17; }
void* heap_caps_malloc_prefer(size_t size, size_t count, ...)
{
    assert(count == 2);
    va_list args;
    va_start(args, count);
    assert(va_arg(args, uint32_t) == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    assert(va_arg(args, uint32_t) == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    va_end(args);
    if (fail_allocation) return nullptr;
    ++allocations;
    auto* value = std::malloc(size);
    allocation_sizes[value] = size;
    return value;
}
void test_free(void* value)
{
    if (value)
    {
        const auto size = allocation_sizes.at(value);
        for (size_t i = 0; i < size; ++i) assert(static_cast<uint8_t*>(value)[i] == 0);
        allocation_sizes.erase(value);
        ++frees;
    }
    std::free(value);
}
int xQueueSend(void*, const TxItem* item, unsigned)
{
    if (full) return 0;
    queued = *item;
    full = true;
    return pdPASS;
}
int xQueueReceive(void*, TxItem* item, unsigned)
{
    if (!full) return 0;
    *item = queued;
    full = false;
    return pdPASS;
}
#define free test_free
#include "hostlink_tx_actual.inc"
#undef free
} // namespace hostlink
int main()
{
    using namespace hostlink;
    uint8_t source[kMaxFrameLen];
    std::memset(source, 42, sizeof(source));
    assert(enqueue_event(0x21, source, sizeof(source), false, 123, 701));
    assert(queued.completion_cookie == 701 && download_mode::discarded == 0);
    assert(full && allocations == 1 && !frees && queued.len == kMaxFrameLen + kHeaderSize + kCrcSize);
    Decoder decoder;
    decoder.push(queued.data, queued.len);
    Frame frame;
    assert(decoder.next(frame) && frame.type == 0x21 && frame.seq == 123 && frame.payload.size() == sizeof(source) && !std::memcmp(frame.payload.data(), source, sizeof(source)));
    const auto* original = queued.data;
    assert(!enqueue_event(0x82, source, 1, false, 0, 702));
    assert(download_mode::discarded == 1 && download_mode::last_cookie == 702);
    assert(queued.data == original && frees == 1);
    assert(enqueue_event(0x21, source, 1, true, 124));
    assert(download_mode::discarded == 2 && download_mode::last_cookie == 701);
    assert(queued.data != original && frees == 2);
    fail_allocation = true;
    assert(!enqueue_event(0x21, source, 1, true, 0) && frees == 2);
    fail_allocation = false;
    ready = false;
    assert(!enqueue_event(0x21, source, 1, false, 0) && allocations == 3);
    ready = true;
    assert(!enqueue_event(0x21, nullptr, 1, false, 0) && allocations == 3);
    assert(!enqueue_event(0x21, source, kMaxFrameLen + 1, false, 0) && allocations == 3);
    TxItem final;
    assert(xQueueReceive(s_tx_queue, &final, 0) == pdPASS);
    release_tx_item(final);
    assert(!final.data && !final.len);
    assert(enqueue_event(0x21, source, 96, false, 125, 703));
    ready = false;
    drain_tx_queue(s_tx_queue);
    assert(!full && allocations == frees);
    assert(download_mode::discarded == 3 && download_mode::last_cookie == 703);
    drain_tx_queue(s_tx_queue);
    drain_tx_queue(nullptr);
    assert(allocations == frees);
}
