#include "platform/linux/oneseg_demux_bootstrap.h"
#include "platform/shared/radio/transport_stream_programs.h"
#include <cassert>

int main()
{
    using platform::linux_runtime::make_oneseg_demux_pat;
    using Programs = platform::shared::radio::TransportStreamPrograms;
    std::array<uint8_t, 188> packet{};
    assert(make_oneseg_demux_pat(packet, 8, 0x1fc8, 37));
    Programs programs;
    programs.accept(packet.data(), packet.size());
    const auto& table = programs.snapshot();
    assert(table.pat_valid && !table.pmt_valid && !table.crc_errors);
    assert(table.transport_id == 37 && table.program_count == 1);
    assert(table.programs[0].number == 8 && table.programs[0].pmt_pid == 0x1fc8);
    packet[14] ^= 1;
    Programs corrupt;
    corrupt.accept(packet.data(), packet.size());
    assert(!corrupt.snapshot().pat_valid && corrupt.snapshot().crc_errors == 1);
    const auto before = packet;
    assert(!make_oneseg_demux_pat(packet, 0, 0x1fc8));
    assert(!make_oneseg_demux_pat(packet, 8, 0));
    assert(!make_oneseg_demux_pat(packet, 8, 0x1fff));
    assert(!make_oneseg_demux_pat(packet, 8, 0x2000));
    assert(packet == before);
}
