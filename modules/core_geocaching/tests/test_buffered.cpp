#include "gps/gpx/buffered_sink.h"
#include <cassert>
#include <memory>
#include <string>
struct Sink : gps::gpx::OutputSink
{
    std::string data;
    unsigned writes = 0;
    bool fail = false;
    bool write(std::string_view bytes) override
    {
        ++writes;
        if (fail) return false;
        data.append(bytes);
        return true;
    }
};
int main()
{
    Sink sink;
    auto buffered = std::make_unique<gps::gpx::BufferedOutputSink>(sink);
    for (unsigned i = 0; i < 1100; ++i) assert(buffered->write("abc"));
    assert(sink.writes == 3 && sink.data.size() == 3072);
    assert(buffered->finish());
    assert(sink.data.size() == 3300 && sink.writes == 4);
    for (std::size_t i = 0; i < sink.data.size(); ++i) assert(sink.data[i] == "abc"[i % 3]);
    assert(buffered->finish() && sink.writes == 4 && !buffered->write("x"));
    Sink bad;
    auto failing = std::make_unique<gps::gpx::BufferedOutputSink>(bad);
    assert(failing->write("x"));
    bad.fail = true;
    assert(!failing->finish());
    assert(!failing->write("y") && !failing->finish());
}
