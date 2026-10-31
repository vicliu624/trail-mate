#include "platform/ui/lora_runtime.h"

#include <cmath>
#include <limits>
#include <mutex>

#include "app/app_facade_access.h"
#include "boards/t_display_p4/board_profile.h"
#include "boards/tab5/tab5_board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform/esp/arduino_common/app_tasks.h"
#include "platform/esp/idf_common/selected_radio.h"
#include "sys/clock.h"

namespace
{

constexpr uint32_t kIrqRxDone = 0x0002u;
constexpr uint32_t kIrqPreambleDetected = 0x0004u;
constexpr uint32_t kIrqSyncWordValid = 0x0008u;
constexpr uint32_t kIrqHeaderValid = 0x0010u;
constexpr uint32_t kIrqHeaderError = 0x0020u;
constexpr uint32_t kIrqCrcError = 0x0040u;
constexpr uint32_t kIrqTimeout = 0x0200u;
constexpr uint32_t kIrqReceiveTerminal =
    kIrqRxDone | kIrqHeaderError | kIrqCrcError | kIrqTimeout;

bool s_radio_tasks_paused_by_probe = false;
std::mutex s_foreground_mutex;
bool s_foreground_acquired = false;

static_assert((kIrqReceiveTerminal &
               (kIrqPreambleDetected | kIrqSyncWordValid | kIrqHeaderValid)) == 0u,
              "SX126x receive progress IRQs must not restart the radio");

bool board_has_lora_capability()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return ::boards::tab5::Tab5Board::hasLora() ||
           ::boards::tab5::Tab5Board::hasM5BusLoraRouting();
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return ::boards::t_display_p4::kBoardProfile.has_lora;
#else
    return false;
#endif
}

platform::esp::idf_common::SelectedRadio& radio()
{
    return platform::esp::idf_common::SelectedRadio::instance();
}

} // namespace

namespace platform::ui::lora
{

bool is_supported()
{
    return board_has_lora_capability();
}

bool acquire()
{
    std::unique_lock<std::mutex> lock(s_foreground_mutex, std::try_to_lock);
    if (!lock.owns_lock() || s_foreground_acquired) return false;
    if (!is_supported())
    {
        return false;
    }

    if (!::app::AppTasks::areRadioTasksPaused())
    {
        if (!::app::AppTasks::pauseRadioTasks())
        {
            return false;
        }
        s_radio_tasks_paused_by_probe = true;
    }

    if (radio().acquire())
    {
        s_foreground_acquired = true;
        return true;
    }

    if (s_radio_tasks_paused_by_probe)
    {
        ::app::AppTasks::resumeRadioTasks();
        s_radio_tasks_paused_by_probe = false;
    }
    return false;
}

bool is_online()
{
    return radio().isOnline();
}

bool configure_receive(float freq_mhz, const ReceiveConfig& config)
{
    return radio().configureLoRaReceive(freq_mhz,
                                        config.bw_khz,
                                        config.sf,
                                        config.cr,
                                        config.tx_power,
                                        config.preamble_len,
                                        config.sync_word,
                                        config.crc_len);
}

bool transmit_packet(const uint8_t* data, std::size_t size)
{
    constexpr uint32_t kIrqTxDone = 0x0001u;
    constexpr uint32_t kTxTimeoutMs = 5000;
    if (!data || size == 0 || !radio().isOnline() || radio().startTransmit(data, size) != 0)
    {
        return false;
    }

    const uint32_t started_ms = sys::millis_now();
    while ((sys::millis_now() - started_ms) < kTxTimeoutMs)
    {
        const uint32_t irq = radio().getIrqFlags();
        if ((irq & (kIrqTxDone | kIrqTimeout)) != 0u)
        {
            radio().clearIrqFlags(irq);
            const bool complete = (irq & kIrqTxDone) != 0u;
            const bool receiving = radio().startReceive();
            return complete && receiving;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    radio().clearIrqFlags(kIrqTxDone | kIrqTimeout);
    (void)radio().startReceive();
    return false;
}

float read_instant_rssi()
{
    const float rssi = radio().readRssi();
    return std::isfinite(rssi) ? rssi : std::numeric_limits<float>::quiet_NaN();
}

bool poll_received_packet(uint8_t* buffer, std::size_t capacity, ReceivedPacket* out_packet)
{
    if (!buffer || capacity == 0 || !out_packet || !radio().isOnline())
    {
        return false;
    }

    const uint32_t irq = radio().getIrqFlags();
    if ((irq & kIrqRxDone) == 0u ||
        (irq & (kIrqHeaderError | kIrqCrcError | kIrqTimeout)) != 0u)
    {
        if (irq != 0u)
        {
            radio().clearIrqFlags(irq);
            // Do not abort an in-progress LoRa frame when the chip reports a
            // preamble, sync word, or valid header before RX_DONE.
            if ((irq & kIrqReceiveTerminal) != 0u)
            {
                (void)radio().startReceive();
            }
        }
        return false;
    }

    const int packet_size = radio().getPacketLength(true);
    const bool packet_fits = packet_size > 0 &&
                             static_cast<std::size_t>(packet_size) <= capacity;
    const int read_state = packet_fits
                               ? radio().readPacket(buffer, static_cast<std::size_t>(packet_size))
                               : -1;
    const float rssi_dbm = packet_fits ? radio().readRssi() : 0.0f;

    radio().clearIrqFlags(irq);
    const bool receiving = radio().startReceive();

    if (!packet_fits || read_state != 0 || !receiving)
    {
        return false;
    }

    out_packet->size = static_cast<std::size_t>(packet_size);
    out_packet->rssi_dbm = rssi_dbm;
    out_packet->snr_db = std::numeric_limits<float>::quiet_NaN();
    return true;
}

void release()
{
    std::lock_guard<std::mutex> lock(s_foreground_mutex);
    if (!s_foreground_acquired) return;
    // Restore the configured protocol PHY before the background adapter owns
    // the SX126x again. The probe may have left it on any candidate profile.
    ::app::configFacade().applyMeshConfig();
    radio().release();
    if (s_radio_tasks_paused_by_probe)
    {
        ::app::AppTasks::resumeRadioTasks();
        s_radio_tasks_paused_by_probe = false;
    }
    s_foreground_acquired = false;
}

} // namespace platform::ui::lora
