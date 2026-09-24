#include "geocaching/storage/volume_format.h"

int main()
{
    using namespace geocaching::storage;
    VolumeInstance id{}; id[0] = 9; id[15] = 17;
    auto header = encodeVolumeHeader(id);
    VolumeInstance decoded;
    if (decodeVolumeHeader({header.data(), header.size()}, decoded) != VolumeFormatResult::Supported || decoded != id) return 1;
    for (size_t length = 0; length < header.size(); ++length)
        if (decodeVolumeHeader({header.data(), length}, decoded) != VolumeFormatResult::Corrupt) return 2;
    for (size_t i = 8; i < header.size(); ++i)
    {
        header[i] ^= 1;
        if (decodeVolumeHeader({header.data(), header.size()}, decoded) != VolumeFormatResult::Corrupt || decoded != VolumeInstance{}) return 3;
        header[i] ^= 1;
    }
    header[5] = 2;
    if (decodeVolumeHeader({header.data(), header.size()}, decoded) != VolumeFormatResult::Unsupported) return 4;
    return 0;
}
