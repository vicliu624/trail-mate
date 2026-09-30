#include "gps/gpx/text_writer.h"
#include <cassert>
#include <string>

struct Sink : gps::gpx::OutputSink
{
    std::string text;
    bool fail = false;
    bool write(std::string_view bytes) override
    {
        if (fail) return false;
        text.append(bytes);
        return true;
    }
};
int main()
{
    using gps::gpx::GpxTextWriter;
    Sink sink;
    GpxTextWriter writer(sink);
    assert(writer.escaped("<&>\"'\n"));
    assert(sink.text == "&lt;&amp;&gt;&quot;&apos;\n");
    sink.text.clear();
    assert(writer.coordinate(-1));
    assert(sink.text == "-0.0000001");
    sink.text.clear();
    assert(writer.coordinate(1201500000));
    assert(sink.text == "120.1500000");
    const std::uint8_t data[] = {'f', 'o', 'o'};
    for (std::size_t n = 1; n <= 3; ++n)
    {
        sink.text.clear();
        assert(writer.base64({data, n}));
        assert(sink.text == (n == 1 ? "Zg==" : n == 2 ? "Zm8="
                                                      : "Zm9v"));
    }
    GpxTextWriter limited(sink, 3);
    assert(!limited.raw("four") && !limited.good());
    assert(!limited.raw("x"));
    sink.fail = true;
    assert(!writer.raw("x") && !writer.good());
}
