#include "platform/esp/arduino_common/hostlink/hostlink_download_mode.h"

#include "hostlink/download_mode_owner.h"
#include "platform/esp/arduino_common/app_config_sd_tms_runtime.h"
#include "platform/esp/arduino_common/hostlink/hostlink_service.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/arduino_common/storage/storage_runtime.h"
#include "sys/clock.h"
#include "usb/usb_cdc_transport.h"
#include <atomic>
#include <esp32-hal-tinyusb.h>

namespace hostlink::download_mode
{
namespace
{
namespace sd = platform::esp::arduino_common::storage;
using management::DownloadModeOwner;
using management::Status;

std::atomic<uint32_t> s_completion{0};

class Port final : public management::DownloadModePort
{
  public:
    bool supported() const override { return download_mode::supported(); }
    bool connected() const override { return usb_cdc::is_connected(); }
    bool canBegin() const override
    {
        return !app::sd_tms::webConfigurationRestartPending() && !sd::sd_external_block_owner_active();
    }
    void stopStorage() override
    {
        was_armed_ = !sd::deferred_storage_stopped();
        sd::stop_deferred_storage();
    }
    bool storageStopped() const override { return sd::deferred_storage_stopped(); }
    bool claimStorage() override
    {
        if (!sd::sd_card_ready()) return true;
        if (sd::sd_external_block_owner_active()) return false;
        owns_sd_ = sd::sd_set_external_block_owner_active(true);
        return owns_sd_;
    }
    bool releaseStorage() override
    {
        if (!owns_sd_) return true;
        if (!sd::sd_set_external_block_owner_active(false)) return false;
        owns_sd_ = false;
        return true;
    }
    void resumeStorage() override
    {
        if (was_armed_) sd::resume_deferred_storage();
        was_armed_ = false;
    }
    bool reply(const uint8_t* ids, uint16_t sequence, Status status, uint32_t cookie,
               uint8_t* scratch, size_t capacity) override
    {
        const uint8_t body[] = {status == Status::Ok ? uint8_t(0x91) : uint8_t(0x90), 0xc3};
        size_t written = 0;
        if (!management::encodeEnvelope(ids, ids + 16, static_cast<uint16_t>(status),
                                        body, status == Status::Ok ? 2U : 1U, scratch, capacity, written)) return false;
        return enqueue_event(static_cast<uint8_t>(FrameType::ManagementResponse), scratch,
                             written, false, sequence, cookie);
    }
    void enterBootloader() override
    {
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32S2
        usb_persist_restart(RESTART_BOOTLOADER);
#endif
    }

  private:
    bool was_armed_ = false;
    bool owns_sd_ = false;
};

Port s_port;
DownloadModeOwner s_owner(s_port);
} // namespace

bool supported()
{
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32S2
    return true;
#else
    return false;
#endif
}

bool active() { return s_owner.active(); }

management::Status request(const management::EnvelopeView& request, uint16_t sequence)
{
    const auto result = s_owner.request(request, sequence, sys::millis_now());
    if (result == Status::Ok) s_completion.store(0, std::memory_order_release);
    return result;
}

void service()
{
    if (!s_owner.active()) return;
    // A complete management envelope fits in 43 bytes. This bounded reply
    // storage lives only in the existing Arduino service pump, never a new task.
    uint8_t reply[64];
    s_owner.advance(sys::millis_now(), s_completion.exchange(0, std::memory_order_acq_rel), reply, sizeof(reply));
}

void reply_completed(uint32_t cookie, bool delivered)
{
    if (cookie) s_completion.store(cookie | (delivered ? 0U : DownloadModeOwner::kDiscardedBit), std::memory_order_release);
}
} // namespace hostlink::download_mode
