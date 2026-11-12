#include "fake_receiver.h"
#include "platform/esp/arduino_common/gps/GPS.h"
#include <cstring>

class VersionReceiver : public FakeReceiver
{
  public:
    size_t write(uint8_t value) override
    {
        const size_t before = commands.size();
        const size_t count = FakeReceiver::write(value);
        if (commands.size() != before)
        {
            assert(commands.back()[2] == 0x0a && commands.back()[3] == 0x04);
            input.clear();
            std::vector<uint8_t> version(70, 0);
            const char* extension = "MOD=MIA-M10Q";
            std::memcpy(version.data() + 40, extension, std::strlen(extension));
            enqueue(ubxFrame(0x0a, 0x04, version));
        }
        return count;
    }
};

int main()
{
    GPS gps;
    VersionReceiver receiver;
    assert(gps.init(&receiver));
    assert(gps.getModel() == "MIA-M10Q");
    assert(receiver.commands.size() == 1);
    assert(receiver.input.empty());
}
