#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace platform::shared::radio
{
// Worker-owned MPEG-2 PSI reader. Input is complete, aligned 188-byte TS packets.
// Keep this fixed-storage object off ESP task stacks, for example in a service.
// No inferred PAT, fixed service/PID, RF lock or playback state is generated.
class TransportStreamPrograms
{
  public:
    static constexpr size_t max_programs = 32, max_streams = 16;
    enum class Discovery : uint8_t
    {
        Pat,
        AribPartial
    };
    enum class Source : uint8_t
    {
        None,
        Pat,
        AribPartialPmt
    };
    struct Program
    {
        uint16_t number = 0, pmt_pid = 0;
    };
    struct Stream
    {
        uint16_t pid = 0;
        uint8_t type = 0;
    };
    struct Snapshot
    {
        std::array<Program, max_programs> programs{};
        std::array<Stream, max_streams> streams{};
        size_t program_count = 0, stream_count = 0;
        uint16_t transport_id = 0, selected_program = 0, pmt_pid = 0, pcr_pid = 0x1fff;
        uint8_t pat_version = 0, pmt_version = 0;
        bool pat_valid = false, pmt_valid = false;
        Source source = Source::None;
        uint64_t packets = 0, transport_errors = 0, continuity_errors = 0;
        uint64_t scrambled_packets = 0;
        uint64_t duplicates = 0, malformed = 0, crc_errors = 0, capacity_errors = 0;
        uint64_t selected_video_packets = 0, selected_audio_packets = 0;
    };

    explicit TransportStreamPrograms(Discovery discovery = Discovery::Pat) : discovery_(discovery) {}
    const Snapshot& snapshot() const { return state_; }
    void reset()
    {
        state_ = {};
        pat_.clear();
        pmt_.clear();
        for (auto& assembly : partial_) assembly.clear();
        sections_.fill(false);
        pending_count_ = 0;
        pending_active_ = false;
        user_selected_ = false;
    }
    bool selectProgram(uint16_t number)
    {
        for (size_t i = 0; i < state_.program_count; ++i)
            if (state_.programs[i].number == number)
            {
                user_selected_ = true;
                select(state_.programs[i]);
                return true;
            }
        return false;
    }

    bool accept(const uint8_t* packet, size_t size)
    {
        if (!packet || size != 188 || packet[0] != 0x47)
        {
            ++state_.malformed;
            return false;
        }
        ++state_.packets;
        const uint16_t pid = ((packet[1] & 31) << 8) | packet[2];
        Assembly* assembly = pid == 0 ? &pat_ : state_.pat_valid && pid == state_.pmt_pid ? &pmt_
                                                                                          : nullptr;
        if (!assembly && !state_.pat_valid && discovery_ == Discovery::AribPartial && pid >= 0x1fc8 && pid <= 0x1fcf)
            assembly = &partial_[pid - 0x1fc8];
        if (packet[1] & 0x80)
        {
            ++state_.transport_errors;
            if (assembly) assembly->clear();
            return false;
        }
        const unsigned control = (packet[3] >> 4) & 3;
        size_t offset = 4;
        bool discontinuity = false;
        if (control == 0) return malformed(assembly);
        if (packet[3] & 0xc0)
        {
            ++state_.scrambled_packets;
            if (assembly) assembly->clear();
            return true;
        }
        if (control & 2)
        {
            const size_t length = packet[offset++];
            if (length > 188 - offset || (control == 2 && length != 183) ||
                (control == 3 && length > 182)) return malformed(assembly);
            discontinuity = length && (packet[offset] & 0x80);
            offset += length;
        }
        if (assembly && discontinuity) assembly->clear();
        if (!(control & 1)) return true;
        if (!assembly)
        {
            if (state_.pmt_valid)
                for (size_t i = 0; i < state_.stream_count; ++i)
                    if (state_.streams[i].pid == pid)
                    {
                        if (video(state_.streams[i].type)) ++state_.selected_video_packets;
                        if (audio(state_.streams[i].type)) ++state_.selected_audio_packets;
                    }
            return true;
        }
        const uint8_t cc = packet[3] & 15;
        if (assembly->have_cc)
        {
            if (cc == assembly->cc && std::memcmp(packet, assembly->previous.data(), 188) == 0)
            {
                ++state_.duplicates;
                return true;
            }
            if (cc != ((assembly->cc + 1) & 15))
            {
                ++state_.continuity_errors;
                assembly->clear();
            }
        }
        assembly->cc = cc;
        assembly->have_cc = true;
        std::memcpy(assembly->previous.data(), packet, 188);
        const uint8_t* payload = packet + offset;
        size_t remaining = 188 - offset;
        if (packet[1] & 0x40)
        {
            const size_t pointer = *payload++;
            --remaining;
            if (pointer > remaining) return malformed(assembly);
            if (assembly->used)
            {
                append(*assembly, payload, pointer, false, pid);
                if (assembly->used) ++state_.malformed;
            }
            assembly->used = assembly->expected = 0;
            payload += pointer;
            remaining -= pointer;
            append(*assembly, payload, remaining, true, pid);
        }
        else if (assembly->used)
            append(*assembly, payload, remaining, false, pid);
        return true;
    }

    static bool video(uint8_t type)
    {
        return type == 1 || type == 2 || type == 0x10 || type == 0x1b || type == 0x24;
    }
    static bool audio(uint8_t type)
    {
        return type == 3 || type == 4 || type == 0x0f || type == 0x11;
    }

  private:
    struct Assembly
    {
        std::array<uint8_t, 1024> data{};
        std::array<uint8_t, 188> previous{};
        size_t used = 0, expected = 0;
        uint8_t cc = 0;
        bool have_cc = false;
        void clear()
        {
            used = expected = 0;
            have_cc = false;
        }
    };
    Snapshot state_;
    Assembly pat_, pmt_;
    std::array<Assembly, 8> partial_;
    Discovery discovery_;
    bool user_selected_ = false;
    std::array<Program, max_programs> pending_{};
    std::array<bool, 256> sections_{};
    size_t pending_count_ = 0;
    uint16_t pending_id_ = 0;
    uint8_t pending_version_ = 0, pending_last_ = 0;
    bool pending_active_ = false;

    bool malformed(Assembly* assembly)
    {
        ++state_.malformed;
        if (assembly) assembly->clear();
        return false;
    }
    static uint16_t word(const uint8_t* p) { return (uint16_t(p[0]) << 8) | p[1]; }
    static uint16_t pidValue(const uint8_t* p) { return word(p) & 0x1fff; }
    static uint32_t crc(const uint8_t* data, size_t size)
    {
        uint32_t value = 0xffffffff;
        for (size_t i = 0; i < size; ++i)
        {
            value ^= uint32_t(data[i]) << 24;
            for (unsigned bit = 0; bit < 8; ++bit)
                value = (value << 1) ^ ((value & 0x80000000) ? 0x04c11db7 : 0);
        }
        return value;
    }
    void select(Program program)
    {
        if (state_.selected_program == program.number && state_.pmt_pid == program.pmt_pid) return;
        state_.selected_program = program.number;
        state_.pmt_pid = program.pmt_pid;
        invalidatePmt();
    }
    void invalidatePmt()
    {
        state_.pmt_valid = false;
        state_.stream_count = 0;
        state_.pcr_pid = 0x1fff;
        state_.selected_video_packets = state_.selected_audio_packets = 0;
        pmt_.clear();
    }
    void append(Assembly& assembly, const uint8_t* data, size_t size, bool allow_new, uint16_t pid)
    {
        while (size)
        {
            if (!assembly.used && (!allow_new || *data == 0xff)) return;
            const size_t needed = assembly.used < 3 ? 3 : assembly.expected;
            const size_t count = size < needed - assembly.used ? size : needed - assembly.used;
            std::memcpy(assembly.data.data() + assembly.used, data, count);
            assembly.used += count;
            data += count;
            size -= count;
            if (assembly.used == 3 && !assembly.expected)
            {
                assembly.expected = 3 + (((assembly.data[1] & 15) << 8) | assembly.data[2]);
                if (assembly.expected < 12 || assembly.expected > assembly.data.size() ||
                    (assembly.data[1] & 0xf0) != 0xb0)
                {
                    malformed(&assembly);
                    return;
                }
            }
            if (assembly.expected && assembly.used == assembly.expected)
            {
                if (crc(assembly.data.data(), assembly.used)) ++state_.crc_errors;
                else if (pid == 0) parsePat(assembly.data.data(), assembly.used);
                else parsePmt(assembly.data.data(), assembly.used, pid);
                assembly.used = assembly.expected = 0;
                // Without PUSI, bytes after the completed section are stuffing.
                if (!allow_new) return;
            }
        }
    }
    void parsePat(const uint8_t* data, size_t size)
    {
        if (data[0] != 0 || (size - 12) % 4 || data[6] > data[7])
        {
            ++state_.malformed;
            return;
        }
        if (!(data[5] & 1)) return;
        const uint16_t id = word(data + 3);
        const uint8_t version = (data[5] >> 1) & 31, section = data[6], last = data[7];
        if (!pending_active_ || id != pending_id_ || version != pending_version_ || last != pending_last_)
        {
            pending_active_ = true;
            pending_id_ = id;
            pending_version_ = version;
            pending_last_ = last;
            pending_count_ = 0;
            sections_.fill(false);
        }
        if (sections_[section]) return;
        // Validate the entire section before extending the pending table.
        size_t additions = 0;
        for (size_t pos = 8; pos < size - 4; pos += 4)
        {
            const uint16_t number = word(data + pos), pid = pidValue(data + pos + 2);
            if (!number) continue; // NIT, not a television service.
            if (pid < 0x10 || pid == 0x1fff)
            {
                ++state_.malformed;
                return;
            }
            for (size_t i = 0; i < pending_count_; ++i)
                if (pending_[i].number == number)
                {
                    ++state_.malformed;
                    return;
                }
            for (size_t earlier = 8; earlier < pos; earlier += 4)
                if (word(data + earlier) == number)
                {
                    ++state_.malformed;
                    return;
                }
            ++additions;
        }
        if (pending_count_ + additions > max_programs)
        {
            ++state_.capacity_errors;
            return;
        }
        for (size_t pos = 8; pos < size - 4; pos += 4)
            if (word(data + pos)) pending_[pending_count_++] = {word(data + pos), pidValue(data + pos + 2)};
        sections_[section] = true;
        for (unsigned i = 0; i <= last; ++i)
            if (!sections_[i]) return;
        const bool new_transport = state_.pat_valid && state_.transport_id != id;
        state_.pat_valid = true;
        state_.source = Source::Pat;
        state_.transport_id = id;
        state_.pat_version = version;
        state_.program_count = pending_count_;
        state_.programs = pending_;
        if (new_transport) invalidatePmt();
        for (size_t i = 0; i < state_.program_count; ++i)
            if (state_.programs[i].number == state_.selected_program)
            {
                select(state_.programs[i]);
                return;
            }
        if (state_.program_count) select(state_.programs[0]);
        else
        {
            state_.selected_program = state_.pmt_pid = 0;
            invalidatePmt();
        }
    }
    void parsePmt(const uint8_t* data, size_t size, uint16_t pmt_pid)
    {
        const bool partial = !state_.pat_valid && discovery_ == Discovery::AribPartial &&
                             pmt_pid >= 0x1fc8 && pmt_pid <= 0x1fcf;
        const uint16_t number = word(data + 3);
        if (data[0] != 2 || size < 16 || data[6] != 0 || data[7] != 0 ||
            !number || (partial ? (number & 7) != pmt_pid - 0x1fc8 : number != state_.selected_program))
        {
            ++state_.malformed;
            return;
        }
        if (!(data[5] & 1)) return;
        const size_t info = word(data + 10) & 0x0fff, end = size - 4;
        size_t pos = 12 + info, count = 0;
        if (pos > end || !descriptors(data + 12, info))
        {
            ++state_.malformed;
            return;
        }
        // Two passes: a malformed update must not partially replace a valid PMT.
        while (pos < end)
        {
            if (end - pos < 5)
            {
                ++state_.malformed;
                return;
            }
            const size_t extra = word(data + pos + 3) & 0x0fff;
            const uint16_t pid = pidValue(data + pos + 1);
            if (extra > end - pos - 5 || pid < 0x10 || pid == 0x1fff ||
                !descriptors(data + pos + 5, extra))
            {
                ++state_.malformed;
                return;
            }
            for (size_t earlier = 12 + info; earlier < pos;)
            {
                if (pidValue(data + earlier + 1) == pid)
                {
                    ++state_.malformed;
                    return;
                }
                earlier += 5 + (word(data + earlier + 3) & 0x0fff);
            }
            ++count;
            pos += 5 + extra;
        }
        if (count > max_streams)
        {
            ++state_.capacity_errors;
            return;
        }
        if (partial)
        {
            size_t service = 0;
            while (service < state_.program_count && state_.programs[service].pmt_pid != pmt_pid) ++service;
            if (service == state_.program_count)
            {
                // ARIB TR-B14 partial reception carries at most three services.
                if (state_.program_count >= 3)
                {
                    ++state_.capacity_errors;
                    return;
                }
                ++state_.program_count;
            }
            state_.programs[service] = {number, pmt_pid};
            state_.source = Source::AribPartialPmt;
            if (!state_.selected_program || state_.pmt_pid == pmt_pid ||
                (!user_selected_ && pmt_pid == 0x1fc8)) select(state_.programs[service]);
            if (state_.selected_program != number || state_.pmt_pid != pmt_pid) return;
        }
        bool changed = !state_.pmt_valid || state_.stream_count != count ||
                       state_.pcr_pid != pidValue(data + 8);
        size_t index = 0;
        for (pos = 12 + info; pos < end; pos += 5 + (word(data + pos + 3) & 0x0fff), ++index)
            if (index >= state_.stream_count || state_.streams[index].pid != pidValue(data + pos + 1) ||
                state_.streams[index].type != data[pos]) changed = true;
        if (changed) state_.selected_video_packets = state_.selected_audio_packets = 0;
        state_.stream_count = 0;
        for (pos = 12 + info; pos < end; pos += 5 + (word(data + pos + 3) & 0x0fff))
            state_.streams[state_.stream_count++] = {pidValue(data + pos + 1), data[pos]};
        state_.pcr_pid = pidValue(data + 8);
        state_.pmt_version = (data[5] >> 1) & 31;
        state_.pmt_valid = true;
    }
    static bool descriptors(const uint8_t* data, size_t size)
    {
        while (size)
        {
            if (size < 2 || data[1] > size - 2) return false;
            const size_t count = 2 + data[1];
            data += count;
            size -= count;
        }
        return true;
    }
};
} // namespace platform::shared::radio
