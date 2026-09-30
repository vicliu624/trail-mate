#include "geocaching/protocol/record_encoder.h"
#include <fstream>
#include <iterator>
#include <vector>
int main(int argc, char** argv)
{
    using namespace geocaching;
    if (argc != 2) return 1;
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), {});
    protocol::CmpReader reader({data.data(), data.size()});
    size_t fields = 0;
    ByteView encoded;
    if (!reader.array(fields, 2) || !reader.binary(encoded, 4096)) return 2;
    RecordView record;
    if (!protocol::decodeGeocacheRecord(encoded, record)) return 3;
    std::array<uint8_t, 4096> output{};
    size_t written = 0;
    if (!protocol::encodeGeocacheRecord(record, output.data(), output.size(), written) || written != encoded.size ||
        std::memcmp(output.data(), encoded.data, written)) return 4;
    record.name = "   ";
    if (protocol::encodeGeocacheRecord(record, output.data(), output.size(), written) || written) return 5;
    record.name = "A";
    record.longitude_e7 = 1800000000;
    if (protocol::encodeGeocacheRecord(record, output.data(), output.size(), written) || written) return 6;
    return 0;
}
