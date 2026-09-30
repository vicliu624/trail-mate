#include "geocaching/protocol/record_decoder.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv)
{
    assert(argc == 2);
    std::ifstream stream(argv[1], std::ios::binary);
    assert(stream.good());
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)), {});
    geocaching::RecordView record;
    using namespace geocaching::protocol;
    assert(decodeGeocacheRecord({bytes.data(), bytes.size()}, record));
    assert(record.name == "Test" && record.description == "Demo" && record.hint == "Root");
    assert(record.latitude_e7 == 302500000 && record.longitude_e7 == 1201500000);
    assert(record.revision == 1 && record.previous_hash.size == 0);
    assert(record.encoded.data == bytes.data());
    for (std::size_t n = 0; n < bytes.size(); ++n)
    {
        assert(!decodeGeocacheRecord({bytes.data(), n}, record));
        assert(record.encoded.data == nullptr);
    }
    bytes.push_back(0);
    assert(!decodeGeocacheRecord({bytes.data(), bytes.size()}, record));
    assert(!validRecordText("\xc0\xaf", false, true));
    assert(!validRecordText("\xed\xa0\x80", false, true));
    assert(!validRecordText("\xef\xbf\xbe", false, true));
    assert(!validRecordText("\xe3\x80\x80", false, true));
    assert(!validRecordText("a\nb", false, true));
    assert(validRecordText("a\nb\t", true, true));
    assert(validRecordText("\xe4\xb8\xad\xe6\x96\x87", false, true));
}
