#include "geocaching/gpx/write_record.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

struct FileSink : geocaching::gpx::OutputSink
{
    std::ofstream file;
    explicit FileSink(const char* path) : file(path, std::ios::binary) {}
    bool write(std::string_view bytes) override { file.write(bytes.data(), bytes.size()); return file.good(); }
};
struct FixtureCrypto : geocaching::protocol::RecordCrypto
{
    std::array<std::uint8_t, 32> author_hash{};
    bool sha256(geocaching::ByteView input, std::uint8_t output[32]) override
    {
        if (input.size != 64) return false;
        std::memcpy(output, author_hash.data(), 32); return true;
    }
    geocaching::protocol::VerificationResult verifyEd25519(geocaching::ByteView, geocaching::ByteView, geocaching::ByteView) override
    { return geocaching::protocol::VerificationResult::CryptoUnavailable; }
};
int main(int argc, char** argv)
{
    assert(argc == 4);
    std::ifstream f(argv[1], std::ios::binary), metadata(argv[2], std::ios::binary);
    assert(f.good() && metadata.good());
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
    geocaching::protocol::CmpReader reader({bytes.data(), bytes.size()});
    std::size_t count = 0;
    geocaching::ByteView record;
    geocaching::protocol::VerifiedRecordView verified;
    assert(reader.array(count, 2) && reader.binary(record, 4096) && reader.binary(verified.signature, 64));
    assert(geocaching::protocol::decodeGeocacheRecord(record, verified.record));
    metadata.read(reinterpret_cast<char*>(verified.id.bytes.data()), 32);
    FixtureCrypto crypto;
    metadata.read(reinterpret_cast<char*>(crypto.author_hash.data()), 32);
    assert(metadata.good());
    FileSink sink(argv[3]);
    assert(geocaching::gpx::writeGeocacheGpx(verified, crypto, sink));
}
