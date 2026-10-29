#include "platform/linux/packet_radio.h"
#include "platform/linux/vmp_radio.h"
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>

int main(int argc, char** argv)
{
    try
    {
        platform::linux_runtime::PacketRadioHardware hardware;
        hardware.driver = "lr2021";
        hardware.bus.power_gpio = -1;
        hardware.bus.irq_gpio = -1;
        platform::linux_runtime::RadioSettings settings;
        unsigned duration = 500;
        bool vmp = false;
        platform::linux_runtime::FskSettings media;
        for (int index = 1; index < argc; ++index)
        {
            const std::string argument = argv[index];
            if (argument == "--help")
            {
                std::puts("LR2021 RX-only probe: --spi PATH --gpiochip PATH --reset N --busy N --tcxo V [--frequency MHz] [--vmp-channel 0..39] [--duration-ms N]");
                return 0;
            }
            if (++index == argc) throw std::runtime_error("Missing argument value");
            const std::string value = argv[index];
            if (argument == "--spi") hardware.bus.spi_device = value;
            else if (argument == "--gpiochip") hardware.bus.gpiochip = value;
            else if (argument == "--reset") hardware.bus.reset_gpio = std::stoi(value);
            else if (argument == "--busy") hardware.bus.busy_gpio = std::stoi(value);
            else if (argument == "--tcxo") hardware.tcxo_voltage = std::stof(value);
            else if (argument == "--frequency") settings.freq_mhz = std::stof(value);
            else if (argument == "--vmp-channel")
            {
                const int channel = std::stoi(value);
                if (channel < 0 || channel > 39 || !platform::linux_runtime::vmp_media_settings(static_cast<uint8_t>(channel), media))
                    throw std::runtime_error("Invalid VMP channel");
                vmp = true;
            }
            else if (argument == "--duration-ms") duration = std::stoul(value);
            else throw std::runtime_error("Unknown argument: " + argument);
        }
        auto radio = platform::linux_runtime::make_lr2021_radio(hardware);
        if (!radio->acquire() || !radio->configureLoRa(settings))
            throw std::runtime_error(radio->lastError());
        if (vmp && (!radio->supportsFskConfiguration(media) || !radio->configureFsk(media)))
            throw std::runtime_error(radio->lastError());
        std::printf("driver=%s online=%d frequency=%.3f modulation=%s\n", radio->name(), radio->isOnline(), vmp ? media.freq_mhz : settings.freq_mhz, vmp ? "VMP-GFSK" : "LoRa");
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(duration);
        platform::linux_runtime::RadioPacket packet;
        while (std::chrono::steady_clock::now() < end)
        {
            if (radio->pollReceive(&packet)) std::printf("RX size=%zu RSSI=%.1f SNR=%.1f\n", packet.size, packet.rssi_dbm, packet.snr_db);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::printf("instant_rssi=%.1f\n", radio->readRssi());
        if (vmp && !radio->configureLoRa(settings)) throw std::runtime_error(radio->lastError());
        radio->release();
        std::printf("released online=%d\n", radio->isOnline());
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "radio-probe: %s\n", error.what());
        return 1;
    }
}
