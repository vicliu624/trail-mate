#include "platform/linux/fsk_settings.h"
#include "sx126x_calibration.h"
#include <limits>
#include <stdexcept>
void require(bool value)
{
    if (!value) throw std::runtime_error("radio parameter contract failed");
}
int main()
{
    using namespace platform::linux_runtime;
    require(detail::sx126x_image_calibration(433.175f) == std::array<uint8_t, 2>{0x6B, 0x6F});
    require(detail::sx126x_image_calibration(470) == std::array<uint8_t, 2>{0x75, 0x81});
    require(detail::sx126x_image_calibration(780) == std::array<uint8_t, 2>{0xC1, 0xC5});
    require(detail::sx126x_image_calibration(868) == std::array<uint8_t, 2>{0xD7, 0xDB});
    require(detail::sx126x_image_calibration(915) == std::array<uint8_t, 2>{0xE1, 0xE9});
    require(detail::sx126x_image_calibration(450) == std::array<uint8_t, 2>{0x6F, 0x73});
    FskSettings settings;
    settings.freq_mhz = 433.175f;
    require(valid_fsk_settings(settings));
    require(valid_sx126x_fsk_settings(settings) && valid_lr2021_fsk_settings(settings));
    require(settings.sync_word_size == 2 && settings.sync_word[0] == 0x2d && settings.sync_word[1] == 1);
    auto vmp = settings;
    vmp.freq_mhz = 2402;
    vmp.bitrate_kbps = 500;
    vmp.deviation_khz = 250;
    vmp.rx_bandwidth_khz = 800;
    vmp.tx_power_dbm = 10;
    vmp.sync_word = {'V', 'M', 'P', 1};
    vmp.sync_word_size = 4;
    require(valid_fsk_settings(vmp) && valid_lr2021_fsk_settings(vmp) && !valid_sx126x_fsk_settings(vmp));
    vmp.tx_power_dbm = 13;
    require(!valid_lr2021_fsk_settings(vmp));
    vmp.tx_power_dbm = 10;
    vmp.freq_mhz = 1800;
    require(!valid_lr2021_fsk_settings(vmp));
    vmp.freq_mhz = 2402;
    vmp.sync_word_size = 9;
    require(!valid_fsk_settings(vmp));
    vmp.sync_word_size = 0;
    require(!valid_fsk_settings(vmp));
    settings.bitrate_kbps = std::numeric_limits<float>::quiet_NaN();
    require(!valid_fsk_settings(settings));
    settings.bitrate_kbps = 9.6;
    settings.preamble_bits = 0;
    require(!valid_fsk_settings(settings));
}
