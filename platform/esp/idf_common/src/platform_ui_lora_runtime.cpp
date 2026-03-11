#include "platform/ui/lora_runtime.h"

#include <cmath>
#include <limits>

#include "platform/esp/boards/t_display_p4_board_profile.h"
#include "platform/esp/boards/tab5_board_profile.h"
#include "platform/esp/idf_common/sx126x_radio.h"

namespace
{

bool board_has_lora_capability()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return platform::esp::boards::tab5::kBoardProfile.has_lora ||
           platform::esp::boards::tab5::kBoardProfile.has_m5bus_lora_module_routing;
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return platform::esp::boards::t_display_p4::kBoardProfile.has_lora;
#else
    return false;
#endif
}

platform::esp::idf_common::Sx126xRadio& radio()
{
    return platform::esp::idf_common::Sx126xRadio::instance();
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
    return is_supported() && radio().acquire();
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

float read_rssi()
{
    const float rssi = radio().readRssi();
    return std::isfinite(rssi) ? rssi : std::numeric_limits<float>::quiet_NaN();
}

void release()
{
    radio().release();
}

} // namespace platform::ui::lora