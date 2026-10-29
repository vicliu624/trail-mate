#include "platform/linux/packet_radio.h"
#include "platform/ui/lora_runtime.h"
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace
{
namespace port = platform::linux_runtime;
struct Radio final : port::IPacketRadio
{
    bool fail_config = false;
    unsigned samples = 0, polls = 0, sends = 0, releases = 0;
    const char* name() const override { return "receive-state fixture"; }
    bool hardwareCandidatePresent() const override { return true; }
    bool acquire() override { return true; }
    void release() override { ++releases; }
    bool configureLoRa(const port::RadioSettings&) override { return !fail_config; }
    bool startReceive() override { return true; }
    bool transmit(const uint8_t*, size_t) override
    {
        ++sends;
        return true;
    }
    bool pollReceive(port::RadioPacket* packet) override
    {
        ++polls;
        packet->size = 1;
        packet->data[0] = 0x42;
        return true;
    }
    float readRssi() override
    {
        ++samples;
        return -77;
    }
    bool isOnline() const override { return true; } // Deliberately optimistic legacy backend.
    const char* lastError() const override { return "injected configuration failure"; }
    port::RadioSettings appliedLoRaConfig() const override { return {}; }
    port::RadioStatistics stats() const override { return {}; }
} radio;
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
} // namespace

namespace platform::linux_runtime
{
IPacketRadio& foreground_packet_radio() { return radio; }
IPacketRadio& mesh_packet_radio() { return radio; }
Sx126xLoRaConfig Sx126xRadio::defaultLoRaConfigFromEnvironment() { return {}; }
} // namespace platform::linux_runtime

int main()
{
    namespace lora = platform::ui::lora;
    setenv("TRAIL_MATE_RUNTIME_MODE", "mesh", 1);
    setenv("TRAIL_MATE_LORA_SIMULATED", "0", 1);
    require(lora::acquire(), "lease acquisition failed");
    require(std::isnan(lora::read_instant_rssi()) && !radio.samples, "unconfigured lease sampled hardware");
    lora::ReceiveConfig config;
    uint8_t bytes[8]{};
    lora::ReceivedPacket packet;
    require(lora::configure_receive(433.5f, config), "valid configuration failed");
    require(lora::read_instant_rssi() == -77 && radio.samples == 1, "configured RSSI missing");
    require(lora::poll_received_packet(bytes, sizeof(bytes), &packet) && bytes[0] == 0x42, "configured packet missing");
    radio.fail_config = true;
    require(!lora::configure_receive(434.5f, config), "failed configuration reported success");
    require(std::isnan(lora::read_instant_rssi()) && radio.samples == 1, "failed configuration sampled hardware");
    require(!lora::poll_received_packet(bytes, sizeof(bytes), &packet) && radio.polls == 1, "failed configuration admitted old packet");
    require(!lora::transmit_packet(bytes, 1) && !radio.sends, "failed configuration admitted transmit");
    radio.fail_config = false;
    require(lora::configure_receive(433.5f, config), "configuration recovery failed");
    require(lora::read_instant_rssi() == -77 && radio.samples == 2, "recovery did not restore sampling");
    require(!lora::configure_receive(std::numeric_limits<float>::quiet_NaN(), config), "invalid frequency accepted");
    require(std::isnan(lora::read_instant_rssi()) && radio.samples == 2, "invalid retune retained old context");
    lora::release();
    require(std::isnan(lora::read_instant_rssi()) && radio.releases == 1, "released lease sampled hardware");
}
