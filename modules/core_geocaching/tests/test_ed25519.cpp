#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "geocaching/protocol/cmp_reader.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv)
{
    assert(argc == 2);
    std::ifstream file(argv[1], std::ios::binary);
    assert(file.good());
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    geocaching::protocol::CmpReader reader({bytes.data(), bytes.size()});
    std::size_t count = 0;
    geocaching::ByteView record, signature;
    assert(reader.array(count, 2) && count == 2);
    assert(reader.binary(record, 4096) && reader.binary(signature, 64) && reader.finished());
    geocaching::protocol::CmpReader fields(record);
    std::uint64_t schema = 0;
    geocaching::ByteView key;
    assert(fields.array(count, 16) && fields.unsignedInteger(schema) && fields.binary(key, 64));
    constexpr char domain[] = "trailmate.geocache/sign/v1";
    std::vector<std::uint8_t> message(sizeof(domain) + record.size);
    std::memcpy(message.data(), domain, sizeof(domain));
    std::memcpy(message.data() + sizeof(domain), record.data, record.size);
    assert(ed25519_verify(signature.data, message.data(), message.size(), key.data + 32) == 1);
    message.back() ^= 1;
    assert(ed25519_verify(signature.data, message.data(), message.size(), key.data + 32) == 0);
}
