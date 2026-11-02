#include "platform/linux/oneseg_ofdm_wire.h"
#include <cassert>
#include <iomanip>
#include <sstream>

using namespace platform::linux_runtime;

static OneSegCarrierChunk chunk(unsigned sequence, unsigned first)
{
    OneSegCarrierChunk result;
    auto& m = result.metadata;
    m.sequence = sequence;
    m.burst = (sequence - 1) / 2048 + 1;
    m.slot = (sequence - 1) % 2048;
    m.mode = 1;
    m.fft = 256;
    m.cp = 16;
    m.phase = sequence % 4;
    m.flags = 1;
    m.count = 108;
    m.sample_rate = 64000000. / 63;
    result.first = first;
    result.count = std::min(16u, 108 - first);
    for (unsigned i = 0; i < result.count; ++i)
        result.values[i].values = {float(sequence), 0, 1, 0, 1, 0, 1.33333337f, 0};
    return result;
}

int main()
{
    OneSegCarrierCollector collector;
    using R = OneSegCarrierCollector::Result;
    for (unsigned sequence = 1; sequence <= 4; ++sequence)
        for (unsigned first = 0; first < 108; first += 16)
        {
            const auto c = chunk(sequence, first);
            const auto& m = c.metadata;
            std::ostringstream line;
            line.imbue(std::locale::classic());
            line << std::setprecision(17) << "\x1eTM_CARR1 0 " << m.sequence << ' ' << m.burst << ' ' << m.slot << " 1 256 16 " << m.phase << " 1 " << m.sample_rate << " 0 108 " << first << ' ' << c.count;
            for (unsigned i = 0; i < c.count; ++i)
                for (auto component : c.values[i].values) line << ' ' << component;
            line << '\x1f';
            OneSegOfdmWire wire;
            OneSegPhaseObservation phase;
            OneSegCarrierChunk parsed;
            auto status = OneSegOfdmWire::Result::Ordinary;
            for (auto byte : line.str()) status = wire.feed(byte, phase, nullptr, nullptr, nullptr, &parsed);
            assert(status == OneSegOfdmWire::Result::Carrier && parsed.metadata.same(m));
            const bool final = sequence == 4 && first == 96;
            assert(collector.accept(parsed, OneSegLabSource::RecordedIq, 100) == (final ? R::Burst : R::Partial));
            assert(bool(collector.latest()) == final);
        }
    auto retained = collector.latest();
    assert(retained->symbols[3].values[107].values[0] == 4);
    assert(retained->fresh(1600) && !retained->fresh(1601));
    assert(collector.accept(chunk(4, 0), OneSegLabSource::RecordedIq, 200) == R::Rejected);
    collector.reset();
    assert(!collector.latest());
    assert(collector.accept(chunk(1, 0), OneSegLabSource::Synthetic, 200) == R::Rejected);
    assert(collector.accept(chunk(1, 0), OneSegLabSource::RecordedIq, 200) == R::Partial);
    assert(collector.accept(chunk(1, 16), OneSegLabSource::LiveReceiver, 200) == R::Rejected);
    collector.reset();
    assert(collector.accept(chunk(1, 0), OneSegLabSource::RecordedIq, 200) == R::Partial);
    assert(collector.accept(chunk(1, 32), OneSegLabSource::RecordedIq, 200) == R::Rejected);
    assert(collector.accept(chunk(1, 16), OneSegLabSource::RecordedIq, 200) == R::Rejected);
    assert(retained->symbols[3].values[107].values[0] == 4); // Immutable prior generation.
    OneSegCarrierChunk parsed;
    assert(!oneseg_parse_carrier_chunk("0 1 1 0 1 256 16 0 1 450000 0 108 0 16", parsed));
    auto bad = chunk(1, 0);
    bad.metadata.cp = 15;
    assert(!bad.valid());
    bad = chunk(1, 0);
    bad.values[0].values[0] = NAN;
    assert(!bad.valid());
    // V2 keeps original float32 bits, with strict canonical RFC4648 padding.
    std::string payload(((64 * 8 * 4 + 2) / 3) * 4, 'A');
    payload.back() = '=';
    payload.replace(0, 8, "AACAPwAA"); // Little-endian IEEE754 1.0, then zeros.
    const std::string header = "0 1 1 0 1 256 16 0 1 1015873.0158730159 0 108 0 64 ";
    assert(oneseg_parse_carrier_chunk((header + payload).c_str(), parsed, true));
    assert(parsed.stride == 64 && parsed.count == 64 && parsed.values[0].values[0] == 1);
    assert(parsed.values[63].values[7] == 0);
    {
        OneSegOfdmWire wire;
        OneSegPhaseObservation phase;
        auto result = OneSegOfdmWire::Result::Ordinary;
        for (auto byte : std::string("\x1eTM_CARR2 ") + header + payload + '\x1f') result = wire.feed(byte, phase, nullptr, nullptr, nullptr, &parsed);
        assert(result == OneSegOfdmWire::Result::Carrier && parsed.stride == 64);
    }
    auto invalid = payload;
    invalid.back() = 'A';
    assert(!oneseg_parse_carrier_chunk((header + invalid).c_str(), parsed, true));
    invalid = payload;
    invalid[invalid.size() - 2] = 'B';
    assert(!oneseg_parse_carrier_chunk((header + invalid).c_str(), parsed, true));
    assert(!oneseg_parse_carrier_chunk((header + payload.substr(1)).c_str(), parsed, true));
    invalid = payload;
    invalid.replace(0, 8, "AADAfwAA"); // Quiet NaN must not become a published point.
    assert(!oneseg_parse_carrier_chunk((header + invalid).c_str(), parsed, true));
    std::string tail(((44 * 8 * 4 + 2) / 3) * 4, 'A');
    tail[tail.size() - 1] = tail[tail.size() - 2] = '=';
    const std::string tail_header = "0 1 1 0 1 256 16 0 1 1015873.0158730159 0 108 64 44 ";
    assert(oneseg_parse_carrier_chunk((tail_header + tail).c_str(), parsed, true));
    tail[tail.size() - 3] = 'B';
    assert(!oneseg_parse_carrier_chunk((tail_header + tail).c_str(), parsed, true));
}
