#include "platform/linux/sx126x_radio.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace
{

void print_config(const platform::linux_runtime::Sx126xRadioConfig& config)
{
    std::printf("spi=%s\n", config.spi_device.c_str());
    std::printf("gpiochip=%s\n", config.gpiochip.c_str());
    std::printf("power_gpio=%d\n", config.power_gpio);
    std::printf("reset_gpio=%d\n", config.reset_gpio);
    std::printf("busy_gpio=%d\n", config.busy_gpio);
    std::printf("irq_gpio=%d\n", config.irq_gpio);
    std::printf("spi_speed_hz=%lu\n",
                static_cast<unsigned long>(config.spi_speed_hz));
    std::printf("dio2_as_rf_switch=%s\n",
                config.dio2_as_rf_switch ? "true" : "false");
    std::printf("dio3_tcxo_1v8=%s\n",
                config.dio3_tcxo_1v8 ? "true" : "false");
}

void print_lora_config(const platform::linux_runtime::Sx126xLoRaConfig& config)
{
    std::printf("lora_freq_mhz=%.3f\n", static_cast<double>(config.freq_mhz));
    std::printf("lora_bw_khz=%.1f\n", static_cast<double>(config.bw_khz));
    std::printf("lora_sf=%u\n", static_cast<unsigned>(config.sf));
    std::printf("lora_cr=4/%u\n", static_cast<unsigned>(config.cr));
    std::printf("lora_tx_power_dbm=%d\n", static_cast<int>(config.tx_power_dbm));
    std::printf("lora_preamble=%u\n", static_cast<unsigned>(config.preamble_len));
    std::printf("lora_sync_word=0x%02X\n", static_cast<unsigned>(config.sync_word));
    std::printf("lora_crc_len=%u\n", static_cast<unsigned>(config.crc_len));
}

void print_stats(const platform::linux_runtime::Sx126xRadioStats& stats)
{
    std::printf("online=%s\n", stats.online ? "true" : "false");
    std::printf("rx_packets=%lu\n", static_cast<unsigned long>(stats.rx_packets));
    std::printf("tx_packets=%lu\n", static_cast<unsigned long>(stats.tx_packets));
    std::printf("rx_crc_errors=%lu\n",
                static_cast<unsigned long>(stats.rx_crc_errors));
    std::printf("rx_header_errors=%lu\n",
                static_cast<unsigned long>(stats.rx_header_errors));
    std::printf("rx_timeouts=%lu\n",
                static_cast<unsigned long>(stats.rx_timeouts));
    std::printf("rx_invalid_lengths=%lu\n",
                static_cast<unsigned long>(stats.rx_invalid_lengths));
    std::printf("rx_read_errors=%lu\n",
                static_cast<unsigned long>(stats.rx_read_errors));
    std::printf("last_irq_flags=0x%04lX\n",
                static_cast<unsigned long>(stats.last_irq_flags));
}

void usage()
{
    std::printf("usage: trailmate-sx1262-probe [--configure] [--rx-seconds N]\n");
}

} // namespace

int main(int argc, char** argv)
{
    bool configure = false;
    int rx_seconds = 0;

    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--configure") == 0)
        {
            configure = true;
        }
        else if (std::strcmp(argv[i], "--rx-seconds") == 0 && i + 1 < argc)
        {
            rx_seconds = std::atoi(argv[++i]);
            configure = true;
        }
        else if (std::strcmp(argv[i], "--help") == 0)
        {
            usage();
            return 0;
        }
        else
        {
            usage();
            return 1;
        }
    }

    const auto radio_config =
        platform::linux_runtime::Sx126xRadio::defaultConfigFromEnvironment();
    const auto lora_config =
        platform::linux_runtime::Sx126xRadio::defaultLoRaConfigFromEnvironment();
    auto& radio = platform::linux_runtime::Sx126xRadio::instance();

    std::printf("Trail Mate SX1262 probe\n");
    print_config(radio_config);
    std::printf("hardware_candidate=%s\n",
                platform::linux_runtime::Sx126xRadio::hardwareCandidatePresent()
                    ? "true"
                    : "false");

    if (!radio.acquire(radio_config))
    {
        std::printf("acquire=false\n");
        std::printf("last_error=%s\n", radio.lastError());
        return 2;
    }

    std::printf("acquire=true\n");

    if (configure)
    {
        print_lora_config(lora_config);
        if (!radio.configureLoRa(lora_config))
        {
            std::printf("configure=false\n");
            std::printf("last_error=%s\n", radio.lastError());
            radio.release();
            return 3;
        }
        std::printf("configure=true\n");
    }

    if (rx_seconds > 0)
    {
        if (!radio.startReceive())
        {
            std::printf("rx_start=false\n");
            std::printf("last_error=%s\n", radio.lastError());
            radio.release();
            return 4;
        }

        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(rx_seconds);
        while (std::chrono::steady_clock::now() < deadline)
        {
            platform::linux_runtime::Sx126xPacket packet{};
            if (radio.pollReceive(&packet))
            {
                std::printf("rx_packet len=%lu rssi=%.1f snr=%.1f\n",
                            static_cast<unsigned long>(packet.size),
                            static_cast<double>(packet.rssi_dbm),
                            static_cast<double>(packet.snr_db));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    print_stats(radio.stats());
    std::printf("last_error=%s\n", radio.lastError());
    radio.release();
    return 0;
}
