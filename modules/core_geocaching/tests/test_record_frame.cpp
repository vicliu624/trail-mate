#include "geocaching/storage/record_frame.h"
#include <vector>

int main()
{
    using namespace geocaching::storage;
    const uint8_t payload[] = {0x93, 1, 0, 0x90};
    RecordHeader header;
    if (!makeRecordHeader(RecordKind::Transaction, 0x0102030405060708ULL, {payload, sizeof(payload)}, header)) return 1;
    if (header[12] != 1 || header[19] != 8 || header[11] != 4 || header[7] != 24) return 2;
    std::vector<uint8_t> frame(header.begin(), header.end());
    frame.insert(frame.end(), payload, payload + sizeof(payload));
    RecordFrameView decoded;
    if (!decodeRecordFrame({frame.data(), frame.size()}, decoded) || decoded.sequence != 0x0102030405060708ULL ||
        decoded.payload.size != sizeof(payload)) return 3;
    for (size_t i = 0; i < frame.size(); ++i)
    {
        frame[i] ^= 1;
        if (decodeRecordFrame({frame.data(), frame.size()}, decoded) || decoded.payload.data) return 4;
        frame[i] ^= 1;
    }
    for (size_t size = 0; size < frame.size(); ++size)
        if (decodeRecordFrame({frame.data(), size}, decoded)) return 5;
    frame.push_back(0);
    if (decodeRecordFrame({frame.data(), frame.size()}, decoded)) return 6;
    std::vector<uint8_t> large(65537, 0);
    if (makeRecordHeader(RecordKind::Transaction, 1, {large.data(), large.size()}, header)) return 7;
    if (!makeRecordHeader(RecordKind::Transaction, 1, {large.data(), 65536}, header)) return 8;
    return 0;
}
