#include "geocaching/protocol/cmp_reader.h"
#include <cassert>

int main()
{
    using geocaching::protocol::CmpReader;
    const std::uint8_t valid[] = {0xcc, 0x80, 0xcd, 1, 0, 0xd0, 0xdf,
                                  0xc4, 2, 0xaa, 0xbb, 0xc0};
    CmpReader reader({valid, sizeof(valid)});
    std::uint64_t u = 0;
    std::int64_t s = 0;
    geocaching::ByteView bytes;
    assert(reader.unsignedInteger(u) && u == 128);
    assert(reader.unsignedInteger(u) && u == 256);
    assert(reader.signedInteger(s) && s == -33);
    assert(reader.binary(bytes, 2) && bytes.size == 2 && bytes.data[1] == 0xbb);
    assert(reader.nil() && reader.finished());
    const std::uint8_t redundant[] = {0xcc, 0x7f, 1};
    CmpReader bad({redundant, sizeof(redundant)});
    assert(!bad.unsignedInteger(u));
    assert(!bad.unsignedInteger(u) && !bad.finished());
    const std::uint8_t truncated[] = {0xc5, 1};
    CmpReader short_input({truncated, sizeof(truncated)});
    assert(!short_input.binary(bytes, 4096));
    const std::uint8_t positive_signed[] = {0xd0, 1};
    CmpReader wrong({positive_signed, sizeof(positive_signed)});
    assert(!wrong.signedInteger(s));
    const std::uint8_t minimum[] = {0xd3, 0x80, 0, 0, 0, 0, 0, 0, 0};
    CmpReader min_reader({minimum, sizeof(minimum)});
    assert(min_reader.signedInteger(s) && s == INT64_MIN && min_reader.finished());
    CmpReader null_reader({nullptr, 1});
    assert(!null_reader.unsignedInteger(u));
}
