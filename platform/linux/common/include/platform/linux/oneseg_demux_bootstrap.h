#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace platform::linux_runtime
{
// A local FFmpeg demux view of a programme already identified from received PSI.
// Never feed this bootstrap back into RF programme discovery or its counters.
inline bool make_oneseg_demux_pat(std::array<uint8_t, 188>& packet, uint16_t programme,
                                  uint16_t pmt_pid, uint16_t transport_id = 0)
{
    if (!programme || pmt_pid < 0x10 || pmt_pid >= 0x1fff) return false;
    packet.fill(0xff);
    packet[0] = 0x47;
    packet[1] = 0x40;
    packet[2] = 0;
    packet[3] = 0x10;
    packet[4] = 0; // Pointer to one complete section.
    const std::array<uint8_t, 12> section{
        0, 0xb0, 13, static_cast<uint8_t>(transport_id >> 8), static_cast<uint8_t>(transport_id),
        0xc1, 0, 0, static_cast<uint8_t>(programme >> 8), static_cast<uint8_t>(programme),
        static_cast<uint8_t>(0xe0 | (pmt_pid >> 8)), static_cast<uint8_t>(pmt_pid)};
    uint32_t crc = 0xffffffff;
    for (std::size_t i = 0; i < section.size(); ++i)
    {
        packet[5 + i] = section[i];
        crc ^= uint32_t(section[i]) << 24;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc << 1) ^ ((crc & 0x80000000) ? 0x04c11db7 : 0);
    }
    for (unsigned i = 0; i < 4; ++i) packet[17 + i] = static_cast<uint8_t>(crc >> (24 - i * 8));
    return true;
}
} // namespace platform::linux_runtime
