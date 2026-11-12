#include "fake_receiver.h"
#include "platform/esp/arduino_common/gps/GPS.h"
#include <cstdio>

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::string scenario = argv[1];
    FakeReceiver receiver;
    GPS gps;
    gps.attach(&receiver);
    if (scenario == "fragmented") receiver.fragmented = true;
    if (scenario == "checksum") receiver.reply = FakeReceiver::CorruptThenAck;
    if (scenario == "unrelated") receiver.reply = FakeReceiver::UnrelatedThenAck;
    if (scenario == "nmea")
        receiver.nmea_before_ack = nmeaSentence("GPGSV,1,1,02,05,56,125,45,16,35,220,38") +
                                   nmeaSentence("GNRMC,123519,A,2502.3340,N,10243.0980,E,0.0,0.0,121126,,,A");
    if (scenario == "continue")
    {
        receiver.reply = FakeReceiver::FirstNak;
        assert(!gps.configureNmeaOutput(1, 0));
        assert(receiver.commands.size() == 4);
    }
    else
    {
        assert(gps.setReceiverMode(0, 0x0d));
        assert(receiver.commands.size() == 1);
        const auto expected = ubxFrame(0x06, 0x11, {0, 0});
        assert(receiver.commands.front() == expected);
        assert(receiver.input.empty());
        if (scenario == "nmea")
        {
            assert(gps.passedChecksum() == 2);
            assert(gps.location.isValid());
            gps::GnssSatInfo satellites[4]{};
            assert(gps.getSatellites(satellites, 4) == 2);
        }
    }
    std::printf("PASS %s\n", scenario.c_str());
}
