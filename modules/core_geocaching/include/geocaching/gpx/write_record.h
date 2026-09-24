#pragma once
#include "gps/gpx/text_writer.h"
#include "geocaching/protocol/verify_record.h"
#include <ctime>

namespace geocaching::gpx
{
using gps::gpx::OutputSink;
using gps::gpx::GpxTextWriter;
inline bool writeGeocacheGpx(const protocol::VerifiedRecordView& verified,
                             protocol::RecordCrypto& crypto, OutputSink& sink)
{
    const auto& r = verified.record;
    if (!r.encoded.data || !verified.signature.data || verified.signature.size != 64 ||
        r.author_public_key.size != 64 || static_cast<unsigned>(r.state) > 2 ||
        static_cast<unsigned>(r.container_size) > 5) return false;
    std::array<std::uint8_t, 32> author_hash{};
    if (!crypto.sha256(r.author_public_key, author_hash.data())) return false;
    char timestamp[32]{};
    const auto seconds = r.updated_at ? r.updated_at : r.created_at;
    if (seconds)
    {
        const std::time_t time = static_cast<std::time_t>(seconds);
        if (static_cast<std::uint64_t>(time) != seconds) return false;
        const auto* utc = std::gmtime(&time);
        if (!utc || !std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", utc)) return false;
    }
    GpxTextWriter w(sink);
    auto element = [&w](const char* tag, std::string_view value)
    {
        return w.raw("<") && w.raw(tag) && w.raw(">") && w.escaped(value) &&
               w.raw("</") && w.raw(tag) && w.raw(">\n");
    };
    auto hex = [&w](const std::uint8_t* bytes, std::size_t size)
    {
        constexpr char alphabet[] = "0123456789abcdef";
        for (std::size_t i = 0; i < size; ++i)
        {
            char pair[] = {alphabet[bytes[i] >> 4], alphabet[bytes[i] & 15]};
            if (!w.raw({pair, 2})) return false;
        }
        return true;
    };
    char revision[16], difficulty[8], terrain[8];
    std::snprintf(revision, sizeof(revision), "%lu", static_cast<unsigned long>(r.revision));
    std::snprintf(difficulty, sizeof(difficulty), "%u.%u", r.difficulty_x2 / 2, (r.difficulty_x2 % 2) * 5);
    std::snprintf(terrain, sizeof(terrain), "%u.%u", r.terrain_x2 / 2, (r.terrain_x2 % 2) * 5);
    const char* states[] = {"active", "disabled", "archived"};
    const char* containers[] = {"not-specified", "micro", "small", "regular", "large", "other"};
    const char* gs_containers[] = {"Not chosen", "Micro", "Small", "Regular", "Large", "Other"};
    const auto state = static_cast<unsigned>(r.state);
    const auto container = static_cast<unsigned>(r.container_size);
    if (!w.raw("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<gpx xmlns=\"http://www.topografix.com/GPX/1/1\" xmlns:tm=\"urn:trailmate:geocaching:gpx:1\" xmlns:groundspeak=\"http://www.groundspeak.com/cache/1/0/1\" version=\"1.1\" creator=\"Trail Mate\">\n<wpt lat=\"") ||
        !w.coordinate(r.latitude_e7) || !w.raw("\" lon=\"") || !w.coordinate(r.longitude_e7) || !w.raw("\">\n")) return false;
    if (seconds && !element("time", timestamp)) return false;
    if (!element("name", r.name) || !element("cmt", r.hint) || !w.raw("<desc>") || !w.escaped(r.description) ||
        !w.raw("\n\n--- Reticulum Geocaching ---\nCache-ID: ") || !hex(verified.id.bytes.data(), 32) ||
        !w.raw("\nRevision: ") || !w.raw(revision) || !w.raw("\nState: ") || !w.raw(states[state]) ||
        !w.raw("\nDifficulty: ") || !w.raw(difficulty) || !w.raw("\nTerrain: ") || !w.raw(terrain) ||
        !w.raw("\nContainer: ") || !w.raw(containers[container]) || !w.raw("\nHint: ") || !w.escaped(r.hint) || !w.raw("</desc>\n") ||
        !element("src", "Reticulum Geocaching") || !element("sym", "Geocache") || !element("type", "Geocache|Traditional Cache") ||
        !w.raw("<extensions><groundspeak:cache available=\"") || !w.raw(state == 0 ? "True" : "False") ||
        !w.raw("\" archived=\"") || !w.raw(state == 2 ? "True" : "False") || !w.raw("\">\n") ||
        !element("groundspeak:name", r.name) || !w.raw("<groundspeak:placed_by>Reticulum author ") ||
        !hex(author_hash.data(), 32) || !w.raw("</groundspeak:placed_by>\n") ||
        !element("groundspeak:type", "Traditional Cache") || !element("groundspeak:container", gs_containers[container]) ||
        !element("groundspeak:difficulty", difficulty) || !element("groundspeak:terrain", terrain) ||
        !w.raw("<groundspeak:short_description html=\"False\">Public cache shared over Reticulum.</groundspeak:short_description>\n<groundspeak:long_description html=\"False\">") ||
        !w.escaped(r.description) || !w.raw("</groundspeak:long_description>\n") || !element("groundspeak:encoded_hints", r.hint) ||
        !w.raw("</groundspeak:cache><tm:record version=\"1\"><tm:payload encoding=\"base64\">") || !w.base64({r.encoded.data, r.encoded.size}) ||
        !w.raw("</tm:payload><tm:signature encoding=\"base64\">") || !w.base64({verified.signature.data, verified.signature.size}) ||
        !w.raw("</tm:signature></tm:record></extensions></wpt></gpx>\n")) return false;
    return w.good();
}
} // namespace geocaching::gpx
