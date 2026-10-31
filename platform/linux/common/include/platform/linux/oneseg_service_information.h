#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace platform::linux_runtime
{
// Worker-owned ARIB STD-B10 actual-transport SDT and present/following EIT.
// Text remains encoded STD-B24 bytes; it is never passed to a UTF-8 widget.
class OneSegServiceInformation
{
  public:
    struct Text
    {
        std::array<uint8_t, 255> bytes{};
        uint16_t size = 0;
    };
    struct Event
    {
        bool valid = false;
        uint16_t id = 0;
        std::array<uint8_t, 5> start{};
        std::array<uint8_t, 3> duration{}, language{};
        uint8_t running = 0;
        uint64_t received_ms = 0;
        Text title, description;
    };
    struct Snapshot
    {
        uint16_t service = 0, transport = 0, network = 0;
        bool identity_valid = false, service_valid = false;
        Text name, provider;
        Event present, following;
        uint64_t service_received_ms = 0;
        uint64_t crc_errors = 0, malformed = 0, continuity_errors = 0;
    };
    void reset()
    {
        state_ = {};
        for (auto& item : assembly_) item = {};
    }
    void select(uint16_t service, uint16_t transport = 0)
    {
        if (state_.service == service && (!transport || state_.transport == transport)) return;
        reset();
        state_.service = service;
        state_.transport = transport;
    }
    const Snapshot& snapshot() const { return state_; }
    // Receiver policy, not a claim about the broadcast event's end time.
    static constexpr uint64_t freshness_ms = 30000;
    static void expire(Snapshot& value, uint64_t now)
    {
        const auto stale = [now](uint64_t received)
        { return now < received || now - received >= freshness_ms; };
        if (value.service_valid && stale(value.service_received_ms))
        {
            value.service_valid = false;
            value.name = value.provider = {};
        }
        for (auto* event : {&value.present, &value.following})
            if (event->valid && stale(event->received_ms)) *event = {};
    }
    bool accept(const uint8_t* packet, size_t size, uint64_t now_ms = 0)
    {
        now_ms_ = now_ms;
        if (!packet || size != 188 || packet[0] != 0x47) return bad();
        const uint16_t pid = ((packet[1] & 31) << 8) | packet[2];
        const int index = pid == 0x11 ? 0 : pid == 0x12 ? 1
                                        : pid == 0x26   ? 2
                                        : pid == 0x27   ? 3
                                                        : -1;
        if (index < 0 || !state_.service) return true;
        auto& a = assembly_[index];
        if ((packet[1] & 0x80) || (packet[3] & 0xc0))
        {
            a = {};
            return false;
        }
        const unsigned control = (packet[3] >> 4) & 3;
        if (!control)
        {
            a = {};
            return bad();
        }
        size_t offset = 4;
        if (control & 2)
        {
            const size_t length = packet[offset++];
            if (length > 188 - offset || (control == 2 && length != 183) || (control == 3 && length > 182))
            {
                a = {};
                return bad();
            }
            if (length && (packet[offset] & 0x80)) a = {};
            offset += length;
        }
        if (!(control & 1)) return true;
        const uint8_t cc = packet[3] & 15;
        if (a.have_cc)
        {
            if (cc == a.cc && !std::memcmp(packet, a.previous.data(), 188)) return true;
            if (cc != ((a.cc + 1) & 15))
            {
                ++state_.continuity_errors;
                a = {};
            }
        }
        a.have_cc = true;
        a.cc = cc;
        std::memcpy(a.previous.data(), packet, 188);
        const uint8_t* payload = packet + offset;
        size_t remaining = 188 - offset;
        if (packet[1] & 0x40)
        {
            const size_t pointer = *payload++;
            --remaining;
            if (pointer > remaining)
            {
                a = {};
                return bad();
            }
            if (a.used)
            {
                append(a, payload, pointer, false, pid);
                if (a.used) ++state_.malformed;
            }
            a.used = a.expected = 0;
            append(a, payload + pointer, remaining - pointer, true, pid);
        }
        else if (a.used) append(a, payload, remaining, false, pid);
        return true;
    }

  private:
    struct Assembly
    {
        std::array<uint8_t, 4096> bytes{};
        std::array<uint8_t, 188> previous{};
        size_t used = 0, expected = 0;
        uint8_t cc = 0;
        bool have_cc = false;
    };
    std::array<Assembly, 4> assembly_{};
    Snapshot state_;
    uint64_t now_ms_ = 0;
    bool bad()
    {
        ++state_.malformed;
        return false;
    }
    static uint16_t word(const uint8_t* p) { return (uint16_t(p[0]) << 8) | p[1]; }
    static uint32_t crc(const uint8_t* p, size_t n)
    {
        uint32_t value = 0xffffffff;
        while (n--)
        {
            value ^= uint32_t(*p++) << 24;
            for (unsigned i = 0; i < 8; ++i) value = (value << 1) ^ ((value & 0x80000000) ? 0x04c11db7 : 0);
        }
        return value;
    }
    static void text(Text& out, const uint8_t* p, size_t n)
    {
        out = {};
        out.size = static_cast<uint16_t>(n);
        std::memcpy(out.bytes.data(), p, n);
    }
    bool identity(Snapshot& next, uint16_t transport, uint16_t network)
    {
        if ((next.transport && next.transport != transport) ||
            (next.identity_valid && next.network != network)) return false;
        next.transport = transport;
        next.network = network;
        next.identity_valid = true;
        return true;
    }
    void append(Assembly& a, const uint8_t* p, size_t n, bool new_sections, uint16_t pid)
    {
        while (n)
        {
            if (!a.used && (!new_sections || *p == 0xff)) return;
            const size_t need = a.used < 3 ? 3 : a.expected;
            const size_t count = n < need - a.used ? n : need - a.used;
            std::memcpy(a.bytes.data() + a.used, p, count);
            a.used += count;
            p += count;
            n -= count;
            if (a.used == 3 && !a.expected)
            {
                a.expected = 3 + (word(a.bytes.data() + 1) & 0xfff);
                if (a.expected < 12 || a.expected > (pid == 0x11 ? 1024 : 4096) || (a.bytes[1] & 0xb0) != 0xb0)
                {
                    a.used = a.expected = 0;
                    bad();
                    return;
                }
            }
            if (a.expected && a.used == a.expected)
            {
                if (crc(a.bytes.data(), a.used)) ++state_.crc_errors;
                else parse(a.bytes.data(), a.used, pid);
                a.used = a.expected = 0;
                if (!new_sections) return;
            }
        }
    }
    void parse(const uint8_t* p, size_t n, uint16_t pid)
    {
        if (!(p[5] & 1)) return;
        if (p[6] > p[7])
        {
            bad();
            return;
        }
        if (pid == 0x11 && p[0] == 0x42) sdt(p, n);
        else if (pid != 0x11 && p[0] == 0x4e && word(p + 3) == state_.service && p[6] <= 1) eit(p, n);
    }
    void sdt(const uint8_t* p, size_t n)
    {
        if (n < 15)
        {
            bad();
            return;
        }
        auto next = state_;
        if (!identity(next, word(p + 3), word(p + 8))) return;
        bool found = false;
        for (size_t pos = 11; pos < n - 4;)
        {
            if (n - 4 - pos < 5)
            {
                bad();
                return;
            }
            const bool selected = word(p + pos) == state_.service;
            const size_t length = word(p + pos + 3) & 0xfff;
            pos += 5;
            if (length > n - 4 - pos)
            {
                bad();
                return;
            }
            const size_t end = pos + length;
            bool have_name = false;
            if (selected)
            {
                if (found)
                {
                    bad();
                    return;
                }
                found = true;
                next.name = {};
                next.provider = {};
                next.service_valid = false;
            }
            while (pos < end)
            {
                if (end - pos < 2 || p[pos + 1] > end - pos - 2)
                {
                    bad();
                    return;
                }
                const size_t size = p[pos + 1];
                const uint8_t tag = p[pos];
                const auto* d = p + pos + 2;
                if (tag == 0x48)
                {
                    if (have_name || size < 3 || d[1] > size - 3 || d[2 + d[1]] != size - 3 - d[1])
                    {
                        bad();
                        return;
                    }
                    have_name = true;
                    if (selected)
                    {
                        text(next.provider, d + 2, d[1]);
                        text(next.name, d + 3 + d[1], d[2 + d[1]]);
                        next.service_valid = true;
                        next.service_received_ms = now_ms_;
                    }
                }
                pos += size + 2;
            }
        }
        if (found) state_ = next;
    }
    void eit(const uint8_t* p, size_t n)
    {
        if (n < 18)
        {
            bad();
            return;
        }
        auto next = state_;
        if (!identity(next, word(p + 8), word(p + 10))) return;
        Event event;
        if (n > 18)
        {
            if (n < 30 || (word(p + 24) & 0xfff) != n - 30)
            {
                bad();
                return;
            }
            event.valid = true;
            event.received_ms = now_ms_;
            event.id = word(p + 14);
            event.running = p[24] >> 5;
            std::memcpy(event.start.data(), p + 16, 5);
            std::memcpy(event.duration.data(), p + 21, 3);
            bool described = false;
            std::array<uint32_t, 586> languages{}; // More than fit in a 4096-byte section.
            size_t language_count = 0;
            for (size_t pos = 26; pos < n - 4;)
            {
                if (n - 4 - pos < 2 || p[pos + 1] > n - 4 - pos - 2)
                {
                    bad();
                    return;
                }
                const size_t length = p[pos + 1];
                const auto* d = p + pos + 2;
                if (p[pos] == 0x4d)
                {
                    if (length < 5 || d[3] > length - 5 || d[4 + d[3]] != length - 5 - d[3])
                    {
                        bad();
                        return;
                    }
                    const uint32_t key = (uint32_t(d[0]) << 16) | (uint32_t(d[1]) << 8) | d[2];
                    for (size_t i = 0; i < language_count; ++i)
                        if (languages[i] == key)
                        {
                            bad();
                            return;
                        }
                    if (language_count == languages.size())
                    {
                        bad();
                        return;
                    }
                    languages[language_count++] = key;
                    if (!described || (d[0] == 'j' && d[1] == 'p' && d[2] == 'n'))
                    {
                        described = true;
                        std::memcpy(event.language.data(), d, 3);
                        text(event.title, d + 4, d[3]);
                        text(event.description, d + 5 + d[3], d[4 + d[3]]);
                    }
                }
                pos += length + 2;
            }
        }
        (p[6] ? next.following : next.present) = event;
        state_ = next;
    }
};
} // namespace platform::linux_runtime
