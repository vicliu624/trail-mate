#include "geocaching/storage/record_shape.h"

int main()
{
    using namespace geocaching;
    using namespace geocaching::storage;
    uint8_t key[64]{}, hash[32]{}, other[32]{}, bytes[512]{};
    other[0] = 1;
    DraftView draft;
    size_t size = 0;
    if (!encodeDraft({key, 16}, draft, bytes, sizeof(bytes), size) || !validStoredRowShape({4, {key, 16}, {bytes, size}, false}) ||
        validStoredRowShape({4, {key, 15}, {bytes, size}, false}) || validStoredRowShape({14, {key, 16}, {bytes, size}, false})) return 1;
    bytes[size] = 0;
    if (validStoredRowShape({4, {key, 16}, {bytes, size + 1}, false}) || validStoredRowShape({4, {key, 16}, {bytes, 1}, true}) ||
        !validStoredRowShape({4, {key, 16}, {}, true})) return 2;
    StoredTime time;
    {
        protocol::CmpWriter writer(bytes, sizeof(bytes));
        if (!writer.array(4) || !writer.unsignedInteger(1) || !writer.nil() || !writer.text("Found\nlocal note") || !encodeStoredTime(writer, time) ||
            !validStoredRowShape({7, {key, 32}, {bytes, writer.size()}, false})) return 3;
        bytes[1] = 3;
        if (validStoredRowShape({7, {key, 32}, {bytes, writer.size()}, false})) return 4;
    }
    for (unsigned hashes = 0; hashes <= 2; ++hashes)
    {
        protocol::CmpWriter writer(bytes, sizeof(bytes));
        if (!writer.array(2) || !writer.array(hashes)) return 5;
        if (hashes && !writer.binary({hash, 32})) return 6;
        if (hashes == 2 && !writer.binary({other, 32})) return 7;
        if (!writer.array(0) || validStoredRowShape({11, {key, 32}, {bytes, writer.size()}, false}) != (hashes != 1)) return 8;
        if (hashes == 2)
        {
            bytes[38] = 0; // Make the second hash identical to the first.
            if (validStoredRowShape({11, {key, 32}, {bytes, writer.size()}, false})) return 9;
        }
    }
    {
        protocol::CmpWriter writer(bytes, sizeof(bytes));
        if (!writer.array(8) || !writer.binary({key, 16}) || !writer.binary({key, 64}) || !writer.binary({key, 16}) ||
            !writer.unsignedInteger(1) || !writer.unsignedInteger(2) || !encodeStoredTime(writer, time) || !writer.nil() || !writer.nil() ||
            !validStoredRowShape({8, {key, 16}, {bytes, writer.size()}, false})) return 10;
    }
    for (bool trusted : {false, true})
    {
        protocol::CmpWriter writer(bytes, sizeof(bytes));
        StoredTime deadline;
        deadline.has_utc = true;
        deadline.utc_trusted = trusted;
        deadline.utc_seconds = 100;
        if (!writer.array(5) || !writer.unsignedInteger(1) || !writer.binary({hash, 32}) || !writer.binary({key, 1}) ||
            !encodeStoredTime(writer, time) || !encodeStoredTime(writer, deadline) ||
            validStoredRowShape({6, {key, 48}, {bytes, writer.size()}, false}) != trusted) return 11;
    }
    return 0;
}
