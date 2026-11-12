#include "esp32-hal-tinyusb.h"
#include "platform/esp/arduino_common/hostlink/hostlink_download_mode.h"
#include "platform/esp/arduino_common/hostlink/hostlink_service.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

namespace dm = hostlink::download_mode;
namespace hm = hostlink::management;
namespace
{
uint32_t now = 1, queued_cookie = 0;
bool connected = true, stopped = false, external = false;
unsigned stops = 0, resumes = 0, reboots = 0;
std::vector<uint8_t> response;
} // namespace
namespace sys
{
uint32_t millis_now() { return now; }
} // namespace sys
namespace app::sd_tms
{
bool webConfigurationRestartPending() { return false; }
} // namespace app::sd_tms
namespace usb_cdc
{
bool is_connected() { return connected; }
} // namespace usb_cdc
namespace platform::esp::arduino_common::storage
{
bool sd_card_ready() { return true; }
bool sd_external_block_owner_active() { return external; }
bool sd_set_external_block_owner_active(bool active)
{
    external = active;
    return true;
}
bool deferred_storage_stopped() { return stopped; }
void stop_deferred_storage() { ++stops; }
void resume_deferred_storage()
{
    ++resumes;
    stopped = false;
}
} // namespace platform::esp::arduino_common::storage
namespace hostlink
{
bool enqueue_event(uint8_t type, const uint8_t* bytes, size_t length, bool, uint16_t sequence, uint32_t cookie)
{
    assert(type == uint8_t(FrameType::ManagementResponse) && sequence == 701);
    response.assign(bytes, bytes + length);
    queued_cookie = cookie;
    return true;
}
} // namespace hostlink
void usb_persist_restart(restart_type_t mode)
{
    assert(mode == RESTART_BOOTLOADER);
    ++reboots;
}

int main()
{
    std::array<uint8_t, 32> ids{};
    for (size_t i = 0; i < ids.size(); ++i) ids[i] = uint8_t(i + 1);
    const uint8_t body[]{0x91, 0xc3};
    const hm::EnvelopeView request{ids.data(), ids.data() + 16, 10, body, sizeof(body)};
#if CONFIG_IDF_TARGET_ESP32S3
    assert(dm::supported() && dm::request(request, 701) == hm::Status::Ok);
    dm::service();
    assert(stops == 1 && response.empty() && !reboots && !external);
    stopped = true;
    ++now;
    dm::service();
    assert(external && queued_cookie && !response.empty());
    hm::EnvelopeView reply;
    assert(hm::decodeEnvelope(response.data(), response.size(), reply));
    assert(reply.code == uint16_t(hm::Status::Ok) && reply.body_size == 2);
    assert(std::memcmp(reply.session, ids.data(), 16) == 0 && std::memcmp(reply.request, ids.data() + 16, 16) == 0);
    dm::reply_completed(queued_cookie, true);
    ++now;
    dm::service();
    connected = false;
    now += 99;
    dm::service();
    assert(!reboots);
    ++now;
    dm::service();
    assert(reboots == 1);
    dm::service();
    assert(!dm::active() && resumes == 1 && !external && !stopped);
    connected = true;
    ++now;
    assert(dm::request(request, 701) == hm::Status::Ok);
    stopped = true;
    dm::service();
    dm::reply_completed(queued_cookie, false);
    ++now;
    dm::service();
    assert(!dm::active() && resumes == 2 && !external && reboots == 1);
#else
    assert(!dm::supported() && dm::request(request, 701) == hm::Status::Unsupported);
    dm::service();
    assert(!dm::active() && stops == 0 && reboots == 0 && response.empty());
#endif
    std::puts("Arduino download binding: confirmation, deferred envelope, physical restart and restore PASS");
}
