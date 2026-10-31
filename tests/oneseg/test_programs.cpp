#include "platform/shared/radio/transport_stream_programs.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

using Parser = platform::shared::radio::TransportStreamPrograms;
using Bytes = std::vector<uint8_t>;
using Packet = std::array<uint8_t, 188>;

static Bytes seal(Bytes bytes)
{
    const size_t length = bytes.size() + 1;
    bytes[1] = 0xb0 | (length >> 8);
    bytes[2] = length;
    uint32_t crc = 0xffffffff;
    for (uint8_t byte : bytes)
    {
        crc ^= uint32_t(byte) << 24;
        for (unsigned i = 0; i < 8; ++i) crc = (crc << 1) ^ ((crc & 0x80000000) ? 0x04c11db7 : 0);
    }
    for (int shift : {24, 16, 8, 0}) bytes.push_back(crc >> shift);
    return bytes;
}
static Bytes pat(std::initializer_list<Parser::Program> programs, uint8_t version = 0,
                 uint8_t section = 0, uint8_t last = 0, uint16_t transport = 42)
{
    Bytes data{0, 0, 0, uint8_t(transport >> 8), uint8_t(transport), uint8_t(0xc1 | (version << 1)), section, last};
    for (auto program : programs)
    {
        data.push_back(program.number >> 8);
        data.push_back(program.number);
        data.push_back(0xe0 | (program.pmt_pid >> 8));
        data.push_back(program.pmt_pid);
    }
    return seal(data);
}
static Bytes pmt(uint16_t program, std::initializer_list<Parser::Stream> streams, uint8_t version = 0)
{
    Bytes data{2, 0, 0, uint8_t(program >> 8), uint8_t(program), uint8_t(0xc1 | (version << 1)),
               0, 0, 0xe2, 0x34, 0xf0, 0};
    for (auto stream : streams)
    {
        data.push_back(stream.type);
        data.push_back(0xe0 | (stream.pid >> 8));
        data.push_back(stream.pid);
        data.push_back(0xf0);
        data.push_back(0);
    }
    return seal(data);
}
static Packet packet(uint16_t pid, uint8_t cc, const Bytes& payload, bool start = true, size_t capacity = 184)
{
    assert(capacity && capacity <= 184 && payload.size() + (start ? 1 : 0) <= capacity);
    Packet result;
    result.fill(0xff);
    result[0] = 0x47;
    result[1] = uint8_t(pid >> 8) | (start ? 0x40 : 0);
    result[2] = pid;
    result[3] = 0x10 | (cc & 15);
    size_t offset = 4;
    if (capacity < 184)
    {
        result[3] |= 0x20;
        result[4] = 183 - capacity;
        if (result[4]) result[5] = 0;
        offset = 188 - capacity;
    }
    if (start) result[offset++] = 0;
    std::copy(payload.begin(), payload.end(), result.begin() + offset);
    return result;
}
static void feed(Parser& parser, const Packet& data) { assert(parser.accept(data.data(), data.size())); }

static void discovery()
{
    Parser parser;
    assert(!parser.snapshot().pat_valid && !parser.snapshot().pmt_valid);
    assert(!parser.selectProgram(8));
    feed(parser, packet(0, 0, pat({{0, 0x10}, {123, 0x901}, {456, 0x902}})));
    auto& state = parser.snapshot();
    assert(state.pat_valid && !state.pmt_valid && state.program_count == 2);
    assert(state.selected_program == 123 && state.pmt_pid == 0x901 && state.transport_id == 42);
    feed(parser, packet(0x901, 0, pmt(123, {{0x345, 0x1b}, {0x678, 0x0f}, {0x789, 6}})));
    assert(state.pmt_valid && state.stream_count == 3 && state.pcr_pid == 0x234);
    feed(parser, packet(0x345, 0, {1, 2}, false));
    feed(parser, packet(0x678, 0, {1, 2}, false));
    feed(parser, packet(0x789, 0, {1, 2}, false));
    assert(state.selected_video_packets == 1 && state.selected_audio_packets == 1);
    assert(parser.selectProgram(456));
    assert(!state.pmt_valid && state.stream_count == 0 && state.selected_video_packets == 0);
    feed(parser, packet(0x901, 1, pmt(123, {{0x345, 0x1b}})));
    assert(!state.pmt_valid);
    feed(parser, packet(0x902, 0, pmt(456, {{0x500, 0x11}})));
    assert(state.pmt_valid && state.streams[0].pid == 0x500);
    parser.reset();
    assert(!state.pat_valid && !state.pmt_valid && !state.packets);
}
static void assembly_and_versions()
{
    Parser parser;
    const auto first = pat({{21, 0x101}}, 3, 0, 1), second = pat({{22, 0x102}}, 3, 1, 1);
    feed(parser, packet(0, 15, second));
    assert(!parser.snapshot().pat_valid);
    feed(parser, packet(0, 0, first));
    assert(parser.snapshot().program_count == 2 && parser.snapshot().pat_version == 3);
    feed(parser, packet(0x102, 0, pmt(22, {{0x400, 0x1b}})));
    assert(parser.snapshot().pmt_valid);
    // Partial PAT update is not published; commit only after every section.
    feed(parser, packet(0, 1, pat({{77, 0x103}}, 4, 0, 1)));
    assert(parser.snapshot().program_count == 2 && parser.snapshot().pat_version == 3);
    feed(parser, packet(0, 2, pat({{78, 0x104}}, 4, 1, 1)));
    assert(parser.snapshot().pat_version == 4 && parser.snapshot().selected_program == 77);
    assert(!parser.snapshot().pmt_valid);
    // A new transport with identical service/PID must invalidate old media metadata.
    feed(parser, packet(0x103, 0, pmt(77, {{0x400, 0x1b}})));
    assert(parser.snapshot().pmt_valid);
    feed(parser, packet(0, 3, pat({{77, 0x103}}, 4, 0, 0, 99)));
    assert(!parser.snapshot().pmt_valid && parser.snapshot().transport_id == 99);

    Parser split;
    auto section = pat({{300, 0x777}});
    feed(split, packet(0, 0, Bytes(section.begin(), section.begin() + 5), true, 6));
    assert(!split.snapshot().pat_valid);
    feed(split, packet(0, 1, Bytes(section.begin() + 5, section.end()), false));
    assert(split.snapshot().pat_valid && split.snapshot().selected_program == 300);
    // PUSI pointer bytes finish the preceding section before a new section starts.
    Parser pointer;
    feed(pointer, packet(0, 0, Bytes(first.begin(), first.begin() + 7), true, 8));
    Bytes tail{uint8_t(first.size() - 7)};
    tail.insert(tail.end(), first.begin() + 7, first.end());
    tail.insert(tail.end(), second.begin(), second.end());
    auto joined = packet(0, 1, tail, false);
    joined[1] |= 0x40;
    feed(pointer, joined);
    assert(pointer.snapshot().pat_valid && pointer.snapshot().program_count == 2);
}
static void rejection_and_recovery()
{
    Parser parser;
    auto section = pat({{1, 0x100}});
    auto corrupt = section;
    corrupt.back() ^= 1;
    feed(parser, packet(0, 0, corrupt));
    assert(!parser.snapshot().pat_valid && parser.snapshot().crc_errors == 1);
    auto valid = packet(0, 1, section);
    feed(parser, valid);
    feed(parser, valid);
    assert(parser.snapshot().duplicates == 1);
    // Same CC with different data is a continuity error, not a duplicate.
    feed(parser, packet(0, 1, pat({{2, 0x200}}, 1)));
    assert(parser.snapshot().continuity_errors == 1 && parser.snapshot().selected_program == 2);
    auto tei = packet(0x200, 0, pmt(2, {{0x400, 0x1b}}));
    tei[1] |= 0x80;
    assert(!parser.accept(tei.data(), tei.size()));
    assert(!parser.snapshot().pmt_valid && parser.snapshot().transport_errors == 1);
    feed(parser, packet(0x200, 1, pmt(2, {{0x400, 0x1b}})));
    assert(parser.snapshot().pmt_valid);
    // Incorrect programme identity cannot replace the selected service.
    feed(parser, packet(0x200, 2, pmt(3, {{0x401, 0x0f}})));
    assert(parser.snapshot().streams[0].pid == 0x400);
    auto bad = packet(0x200, 3, {});
    bad[4] = 184; // Pointer exceeds payload.
    assert(!parser.accept(bad.data(), bad.size()));
    bad = packet(0, 2, {});
    bad[3] = 0;
    assert(!parser.accept(bad.data(), bad.size()));
    assert(!parser.accept(nullptr, 188) && !parser.accept(bad.data(), 187));

    Parser lost;
    feed(lost, packet(0, 0, Bytes(section.begin(), section.begin() + 5), true, 6));
    feed(lost, packet(0, 2, Bytes(section.begin() + 5, section.end()), false));
    assert(!lost.snapshot().pat_valid && lost.snapshot().continuity_errors == 1);
    feed(lost, packet(0, 3, section));
    assert(lost.snapshot().pat_valid);
    // Discontinuity flags allow counter reset without reporting packet loss.
    auto discont = packet(0, 9, section, true, 182);
    discont[5] = 0x80;
    feed(lost, discont);
    assert(lost.snapshot().continuity_errors == 1);
}
static void malformed_tables()
{
    Parser parser;
    feed(parser, packet(0, 0, pat({{1, 0x100}})));
    feed(parser, packet(0x100, 0, pmt(1, {{0x400, 0x1b}})));
    const auto original = parser.snapshot();
    // CRC-correct but truncated ES descriptor loop must preserve the old PMT.
    auto invalid = pmt(1, {{0x500, 0x0f}}, 1);
    invalid.resize(invalid.size() - 4);
    invalid[16] = 1;
    feed(parser, packet(0x100, 1, seal(invalid)));
    assert(parser.snapshot().malformed == original.malformed + 1);
    assert(parser.snapshot().streams[0].pid == 0x400);
    // An impossible section length must not write outside the fixed assembler.
    feed(parser, packet(0, 1, {0, 0xbf, 0xff}));
    assert(parser.snapshot().malformed == original.malformed + 2);
    // PAT with duplicate programme numbers is not a selectable programme table.
    feed(parser, packet(0, 2, pat({{7, 0x200}, {7, 0x201}}, 2)));
    assert(parser.snapshot().selected_program == 1);
    // Next tables are not current reception metadata.
    auto future = pat({{9, 0x900}}, 3);
    future.resize(future.size() - 4);
    future[5] &= ~1;
    feed(parser, packet(0, 3, seal(future)));
    assert(parser.snapshot().selected_program == 1);
    // Multiple sections in one PUSI payload.
    Parser multiple;
    auto a = pat({{1, 0x100}}, 1, 0, 1), b = pat({{2, 0x200}}, 1, 1, 1);
    a.insert(a.end(), b.begin(), b.end());
    feed(multiple, packet(0, 0, a));
    assert(multiple.snapshot().program_count == 2);
}
static void capacity_and_updates()
{
    Parser parser;
    for (unsigned section = 0; section < 33; ++section)
        feed(parser, packet(0, section, pat({{uint16_t(section + 1), uint16_t(0x100 + section)}}, 0, section, 32)));
    assert(parser.snapshot().capacity_errors == 1 && !parser.snapshot().pat_valid);
    feed(parser, packet(0, 1, pat({{7, 0x200}}, 1)));
    assert(parser.snapshot().pat_valid && parser.snapshot().program_count == 1);
    auto table = pmt(7, {});
    table.resize(table.size() - 4);
    for (unsigned i = 0; i < 17; ++i)
        table.insert(table.end(), {0x1b, 0xe4, uint8_t(i), 0xf0, 0});
    feed(parser, packet(0x200, 0, seal(table)));
    assert(parser.snapshot().capacity_errors == 2 && !parser.snapshot().pmt_valid);
    feed(parser, packet(0x200, 1, pmt(7, {{0x400, 0x1b}})));
    feed(parser, packet(0x400, 0, {}, false));
    assert(parser.snapshot().selected_video_packets == 1);
    feed(parser, packet(0x200, 2, pmt(7, {{0x400, 0x1b}})));
    assert(parser.snapshot().selected_video_packets == 1);
    feed(parser, packet(0x200, 3, pmt(7, {{0x500, 0x0f}}, 1)));
    assert(parser.snapshot().selected_video_packets == 0 && parser.snapshot().pmt_version == 1);
    // Empty current PAT removes the selected service rather than inventing one.
    feed(parser, packet(0, 2, pat({}, 2)));
    assert(parser.snapshot().pat_valid && !parser.snapshot().pmt_valid);
    assert(parser.snapshot().program_count == 0 && parser.snapshot().selected_program == 0);
}
static void bounded_untrusted_sections()
{
    Parser parser;
    uint32_t random = 0x5a17c001;
    auto next = [&random]()
    { random ^= random << 13; random ^= random >> 17; random ^= random << 5; return random; };
    uint8_t pat_cc = 0, pmt_cc = 0;
    for (unsigned iteration = 0; iteration < 2000; ++iteration)
    {
        parser.reset();
        feed(parser, packet(0, pat_cc++, pat({{7, 0x200}})));
        Bytes section(8 + next() % 1013);
        for (auto& byte : section) byte = next();
        const bool is_pat = iteration & 1;
        section[0] = is_pat ? 0 : 2;
        section[3] = 0;
        section[4] = 7;
        section[5] = 0xc3;
        section[6] = section[7] = 0;
        section = seal(section);
        size_t offset = 0;
        while (offset < section.size())
        {
            const bool start = offset == 0;
            const size_t count = std::min(section.size() - offset, size_t(start ? 183 : 184));
            feed(parser, packet(is_pat ? 0 : 0x200, is_pat ? pat_cc++ : pmt_cc++,
                                Bytes(section.begin() + offset, section.begin() + offset + count), start));
            offset += count;
        }
        assert(parser.snapshot().program_count <= Parser::max_programs);
        assert(parser.snapshot().stream_count <= Parser::max_streams);
    }
}
static void arib_partial_reception()
{
    Parser parser(Parser::Discovery::AribPartial);
    assert(parser.snapshot().source == Parser::Source::None);
    // No PAT: service identity and all ES PIDs come from a valid received PMT.
    feed(parser, packet(0x1fc9, 0, pmt(0x1239, {{0x500, 0x1b}, {0x600, 0x0f}})));
    assert(!parser.snapshot().pat_valid && parser.snapshot().pmt_valid);
    assert(parser.snapshot().source == Parser::Source::AribPartialPmt);
    assert(parser.snapshot().selected_program == 0x1239 && parser.snapshot().pmt_pid == 0x1fc9);
    assert(parser.snapshot().transport_id == 0 && parser.snapshot().stream_count == 2);
    // Primary partial service is preferred until the user makes a choice.
    feed(parser, packet(0x1fc8, 0, pmt(0x1238, {{0x700, 0x1b}})));
    assert(parser.snapshot().selected_program == 0x1238 && parser.snapshot().program_count == 2);
    assert(parser.selectProgram(0x1239) && !parser.snapshot().pmt_valid);
    feed(parser, packet(0x1fc8, 1, pmt(0x1238, {{0x700, 0x1b}})));
    assert(parser.snapshot().selected_program == 0x1239 && !parser.snapshot().pmt_valid);
    feed(parser, packet(0x1fc9, 1, pmt(0x1239, {{0x500, 0x1b}, {0x600, 0x0f}})));
    assert(parser.snapshot().pmt_valid);
    auto scrambled = packet(0x500, 0, {}, false);
    scrambled[3] |= 0x80;
    feed(parser, scrambled);
    assert(parser.snapshot().scrambled_packets == 1 && parser.snapshot().selected_video_packets == 0);
    // PMT identity must match the low three service bits assigned to its PID.
    feed(parser, packet(0x1fca, 0, pmt(0x123b, {{0x800, 0x1b}})));
    assert(parser.snapshot().program_count == 2 && parser.snapshot().malformed == 1);
    auto invalid = pmt(0x123a, {{0x800, 0x1b}});
    invalid.back() ^= 1;
    feed(parser, packet(0x1fca, 1, invalid));
    assert(parser.snapshot().program_count == 2 && parser.snapshot().crc_errors == 1);
    feed(parser, packet(0x1fca, 2, pmt(0x123a, {{0x800, 0x1b}})));
    assert(parser.snapshot().program_count == 3);
    feed(parser, packet(0x1fcb, 0, pmt(0x123b, {{0x900, 0x1b}})));
    assert(parser.snapshot().program_count == 3 && parser.snapshot().capacity_errors == 1);
    // Complete PAT takes precedence when present, without inventing a TS ID.
    feed(parser, packet(0, 0, pat({{0x1239, 0x1fc9}})));
    assert(parser.snapshot().pat_valid && parser.snapshot().source == Parser::Source::Pat);
    assert(parser.snapshot().transport_id == 42 && parser.snapshot().program_count == 1);
    parser.reset();
    auto section = pmt(8, {{0x321, 0x1b}, {0x654, 0x0f}});
    feed(parser, packet(0x1fc8, 0, Bytes(section.begin(), section.begin() + 7), true, 8));
    assert(!parser.snapshot().pmt_valid && parser.snapshot().program_count == 0);
    feed(parser, packet(0x1fc8, 1, Bytes(section.begin() + 7, section.end()), false));
    assert(parser.snapshot().pmt_valid && parser.snapshot().selected_program == 8);
    // The strict PAT mode cannot accidentally enable the ARIB-specific fallback.
    Parser strict;
    feed(strict, packet(0x1fc8, 0, section));
    assert(!strict.snapshot().pmt_valid && strict.snapshot().program_count == 0);
}
int main()
{
    discovery();
    assembly_and_versions();
    rejection_and_recovery();
    malformed_tables();
    capacity_and_updates();
    bounded_untrusted_sections();
    arib_partial_reception();
}
