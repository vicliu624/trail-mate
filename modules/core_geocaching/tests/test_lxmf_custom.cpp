#include "chat/infra/lxmf/lxmf_wire.h"
#include "geocaching/protocol/cmp_reader.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <vector>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

int main()
{
#ifdef _MSC_VER
    _set_error_mode(_OUT_TO_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    std::vector<std::uint8_t> data(8192), output(8400);
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<std::uint8_t>(i);
    std::size_t size = output.size();
    assert(chat::lxmf::encodeCustomDataPayload(1234, "title", "content", "trailmate.geocache",
                                               {data.data(), data.size()}, output.data(), &size));
    chat::lxmf::DecodedTextPayload decoded;
    assert(chat::lxmf::unpackTextPayload(output.data(), size, &decoded));
    assert(decoded.timestamp == 1234 && decoded.title == "title" && decoded.content == "content");
    const auto* type = chat::lxmf::findField(decoded, 0xfb);
    const auto* payload = chat::lxmf::findField(decoded, 0xfc);
    assert(type && payload && decoded.fields.size() == 2);
    geocaching::protocol::CmpReader type_reader({type->encoded_value.data(), type->encoded_value.size()});
    std::string_view name;
    assert(type_reader.text(name, 64) && name == "trailmate.geocache" && type_reader.finished());
    geocaching::protocol::CmpReader data_reader({payload->encoded_value.data(), payload->encoded_value.size()});
    geocaching::ByteView view;
    assert(data_reader.binary(view, 8192) && view.size == data.size() && data_reader.finished());
    assert(std::memcmp(view.data, data.data(), data.size()) == 0);
    std::size_t short_size = size - 1;
    assert(!chat::lxmf::encodeCustomDataPayload(1234, "title", "content", "trailmate.geocache",
                                                {data.data(), data.size()}, output.data(), &short_size));
    assert(short_size == 0);
}
