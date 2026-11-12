#include "fake_receiver.h"
#include "platform/esp/arduino_common/gps/GPS.h"
#include <cstdio>

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::string scenario = argv[1];
    FakeReceiver receiver;
    GPS gps;
    gps.attach(&receiver, GPS::ConfigProtocol::UbxValset);
    std::vector<uint8_t> payload = {0, 1, 0, 0}; // Current RAM layer only.
    if (scenario == "power")
    {
        assert(gps.setReceiverMode(0, 0x0d));
        payload.insert(payload.end(), {0x01, 0x00, 0xd0, 0x20, 0});
    }
    else if (scenario == "systems")
    {
        const uint32_t started = test_millis;
        assert(gps.configureGnss(0x0d));
        assert(test_millis - started >= 600);
        payload.insert(payload.end(), {0x1f, 0x00, 0x31, 0x10, 1,
                                       0x25, 0x00, 0x31, 0x10, 0,
                                       0x21, 0x00, 0x31, 0x10, 1,
                                       0x22, 0x00, 0x31, 0x10, 1});
    }
    else
    {
        const uint8_t mask = scenario == "compact" ? 2 : 0;
        const uint8_t rate = scenario == "off" ? 0 : 1;
        if (scenario == "nak") receiver.reply = FakeReceiver::FirstNak;
        assert(gps.configureNmeaOutput(rate, mask) == (scenario != "nak"));
        const uint8_t detail_rate = mask == 2 ? 0 : rate;
        payload.insert(payload.end(), {0xbb, 0x00, 0x91, 0x20, rate,
                                       0xc0, 0x00, 0x91, 0x20, detail_rate,
                                       0xac, 0x00, 0x91, 0x20, rate,
                                       0xc5, 0x00, 0x91, 0x20, detail_rate});
    }
    assert(receiver.commands.size() == 1);
    assert(receiver.commands.front() == ubxFrame(0x06, 0x8a, payload));
    assert(receiver.input.empty());
    std::printf("PASS M10 %s\n", scenario.c_str());
}
