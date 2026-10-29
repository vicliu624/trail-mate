#include "platform/linux/vmp_radio.h"
#include <cassert>
#include <cstring>

using namespace platform::linux_runtime;
struct State
{
    RadioSettings lora;
    FskSettings fsk;
    unsigned acquired = 0, released = 0, transmissions = 0;
    bool wide = true, online = false, media = false, fail_fsk = false, fail_tx = false;
    float fail_lora_at = 0;
    std::string error;
};
class Radio final : public IPacketRadio
{
  public:
    explicit Radio(std::shared_ptr<State> state) : state(std::move(state)) {}
    const char* name() const override { return "test radio"; }
    bool hardwareCandidatePresent() const override { return true; }
    bool acquire() override
    {
        ++state->acquired;
        state->online = true;
        return true;
    }
    void release() override
    {
        ++state->released;
        state->online = false;
    }
    bool configureLoRa(const RadioSettings& value) override
    {
        if (state->fail_lora_at == value.freq_mhz)
        {
            state->error = "restore failure";
            return false;
        }
        state->lora = value;
        state->media = false;
        state->error.clear();
        return true;
    }
    bool supportsFsk() const override { return true; }
    bool supportsFskConfiguration(const FskSettings& value) const override
    {
        return state->wide ? valid_lr2021_fsk_settings(value) : valid_sx126x_fsk_settings(value);
    }
    bool configureFsk(const FskSettings& value) override
    {
        state->media = true;
        state->fsk = value;
        if (state->fail_fsk)
        {
            state->error = "partial FSK failure";
            return false;
        }
        return supportsFskConfiguration(value);
    }
    bool startReceive() override { return true; }
    bool transmit(const uint8_t*, size_t) override
    {
        if (state->fail_tx)
        {
            state->error = "TX failure";
            return false;
        }
        ++state->transmissions;
        return true;
    }
    bool pollReceive(RadioPacket* out) override
    {
        out->size = 1;
        out->data[0] = 7;
        return true;
    }
    float readRssi() override { return -100; }
    bool isOnline() const override { return state->online; }
    const char* lastError() const override { return state->error.c_str(); }
    RadioSettings appliedLoRaConfig() const override { return state->lora; }
    RadioStatistics stats() const override { return {}; }
    std::shared_ptr<State> state;
};
int main()
{
    FskSettings profile;
    assert(vmp_media_settings(0, profile) && profile.freq_mhz == 2402 && profile.sync_word_size == 4);
    assert(profile.bitrate_kbps == 500 && profile.deviation_khz == 250 && profile.rx_bandwidth_khz == 800);
    assert(std::memcmp(profile.sync_word.data(), "VMP\x01", 4) == 0);
    assert(vmp_media_settings(39, profile) && profile.freq_mhz == 2480);
    assert(!vmp_media_settings(40, profile));
    auto state = std::make_shared<State>();
    PacketRadioCoordinator coordinator(std::make_unique<Radio>(state));
    auto& mesh = coordinator.mesh();
    auto& tool = coordinator.foreground();
    assert(mesh.acquire());
    RadioSettings settings;
    settings.freq_mhz = 433.175f;
    assert(mesh.configureLoRa(settings));
    VmpRadioLease lease(tool);
    assert(lease.supports(0) && !lease.supports(40) && !mesh.supportsFskConfiguration(profile));
    assert(lease.begin(5));
    assert(!lease.begin(5) && lease.active());
    uint8_t data[] = {1, 2, 3};
    assert(!mesh.transmit(data, sizeof(data)));
    assert(lease.transmit(data, sizeof(data)));
    assert(lease.switchToMedia() && lease.mediaActive() && state->media && state->fsk.freq_mhz == 2412);
    RadioPacket packet;
    assert(lease.receive(packet) && packet.size == 1);
    settings.freq_mhz = 868;
    assert(mesh.configureLoRa(settings)); // Deferred while the VMP lease owns RF.
    assert(state->lora.freq_mhz == 433.175f);
    assert(lease.end() && !lease.active() && !state->media && state->lora.freq_mhz == 868);
    assert(mesh.isOnline() && mesh.transmit(data, sizeof(data)));
    assert(!lease.receive(packet) && packet.size == 0);
    state->fail_fsk = true;
    assert(lease.begin(0) && !lease.switchToMedia());
    assert(!lease.active() && !state->media && mesh.isOnline());
    assert(std::strstr(lease.lastError(), "partial FSK failure"));
    state->fail_fsk = false;
    assert(lease.begin(0) && lease.switchToMedia());
    state->fail_tx = true;
    assert(!lease.transmit(data, sizeof(data)) && !lease.active() && mesh.isOnline());
    state->fail_tx = false;
    assert(lease.begin(0) && lease.switchToMedia());
    settings.freq_mhz = 915;
    assert(mesh.configureLoRa(settings));
    state->fail_lora_at = 915;
    assert(!lease.end() && !lease.end() && !mesh.isOnline());
    state->fail_lora_at = 0;
    assert(mesh.configureLoRa(settings) && mesh.isOnline());
    mesh.release();
    assert(state->released == 1 && !state->online);
    state->wide = false;
    assert(!lease.supports(0) && !lease.begin(0) && state->acquired == 1);
    // A plain radio's void release cannot be mistaken for verified restoration.
    state->wide = true;
    Radio raw(state);
    VmpRadioLease unchecked(raw);
    assert(unchecked.begin(0) && unchecked.switchToMedia() && !unchecked.end());
    assert(!state->online && !state->media);
}
