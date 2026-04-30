#include "platform/ui/team_ui_store_runtime.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
#include <utility>

namespace team::ui
{
namespace
{

constexpr const char* kSdRootEnv = "TRAIL_MATE_SD_ROOT";
constexpr const char* kSettingsRootEnv = "TRAIL_MATE_SETTINGS_ROOT";
constexpr const char* kTeamBaseDirName = "team";
constexpr const char* kTracksDirName = "tracks";
constexpr const char* kGpxHeader =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<gpx version=\"1.1\" creator=\"Trail-Mate\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
    "<trk>\n"
    "<trkseg>\n";
constexpr uint32_t kMinValidEpoch = 1577836800U; // 2020-01-01

uint64_t team_id_to_u64(const TeamId& id)
{
    uint64_t value = 0;
    for (size_t i = 0; i < id.size(); ++i)
    {
        value |= (static_cast<uint64_t>(id[i]) << (8U * i));
    }
    return value;
}

std::filesystem::path default_storage_root()
{
    if (const char* configured = std::getenv(kSdRootEnv))
    {
        if (configured[0] != '\0')
        {
            return std::filesystem::path(configured);
        }
    }

    if (const char* configured = std::getenv(kSettingsRootEnv))
    {
        if (configured[0] != '\0')
        {
            return std::filesystem::path(configured) / "sdcard";
        }
    }

#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"))
    {
        if (appdata[0] != '\0')
        {
            return std::filesystem::path(appdata) / "TrailMateCardputerZero" / "sdcard";
        }
    }
#endif

    if (const char* home = std::getenv("HOME"))
    {
        if (home[0] != '\0')
        {
            return std::filesystem::path(home) / ".trailmate_cardputer_zero" / "sdcard";
        }
    }

    return std::filesystem::current_path() / ".trailmate_cardputer_zero" / "sdcard";
}

std::string base32_from_u64(uint64_t value)
{
    static const char* kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    char buf[13] = {0};
    for (int i = 12; i >= 0; --i)
    {
        buf[i] = kAlphabet[value & 0x1F];
        value >>= 5;
    }
    return std::string(buf, 13);
}

std::string team_dir_from_id(const TeamId& id)
{
    std::string full = base32_from_u64(team_id_to_u64(id));
    const std::size_t first = full.find_first_not_of('A');
    if (first == std::string::npos)
    {
        full = "AAAA";
    }
    else
    {
        full = full.substr(first);
    }
    if (full.size() > 10)
    {
        full = full.substr(full.size() - 10);
    }
    if (full.size() < 4)
    {
        full = std::string(4 - full.size(), 'A') + full;
    }
    return std::string("T_") + full;
}

std::filesystem::path team_base_dir()
{
    return default_storage_root() / kTeamBaseDirName;
}

bool ensure_dir(const std::filesystem::path& dir)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return !ec;
}

bool ensure_team_dir_for_id(const TeamId& id, std::filesystem::path& out_dir)
{
    out_dir = team_base_dir() / team_dir_from_id(id);
    return ensure_dir(out_dir);
}

std::filesystem::path member_track_path_for(const TeamId& team_id, uint32_t member_id)
{
    std::filesystem::path team_dir;
    if (!ensure_team_dir_for_id(team_id, team_dir))
    {
        return {};
    }
    const std::filesystem::path tracks_dir = team_dir / kTracksDirName;
    if (!ensure_dir(tracks_dir))
    {
        return {};
    }

    char name[24] = {0};
    std::snprintf(name, sizeof(name), "%08lX.gpx", static_cast<unsigned long>(member_id));
    return tracks_dir / name;
}

std::string iso_time(std::time_t t)
{
    if (t <= 0)
    {
        t = std::time(nullptr);
    }

    std::tm tm_utc{};
    char buf[32] = {0};
#if defined(_WIN32)
    if (gmtime_s(&tm_utc, &t) == 0)
#else
    if (gmtime_r(&t, &tm_utc) != nullptr)
#endif
    {
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
        return std::string(buf);
    }
    return std::string("1970-01-01T00:00:00Z");
}

struct TeamUiRuntimeMemory
{
    std::map<uint64_t, std::deque<TeamChatLogEntry>> chat_logs{};
    std::map<uint64_t, std::deque<TeamPosSample>> latest_positions{};
};

TeamUiRuntimeMemory& runtime_memory()
{
    static TeamUiRuntimeMemory memory{};
    return memory;
}

constexpr size_t kMaxChatEntriesPerTeam = 128;
constexpr size_t kMaxPosSamplesPerTeam = 96;

TeamUiMemoryStore s_memory_store{};
ITeamUiStore* s_store = &s_memory_store;

} // namespace

bool TeamUiMemoryStore::has_snapshot_ = false;
TeamUiSnapshot TeamUiMemoryStore::snapshot_{};

bool TeamUiMemoryStore::load(TeamUiSnapshot& out)
{
    if (!has_snapshot_)
    {
        return false;
    }

    out = snapshot_;
    return true;
}

void TeamUiMemoryStore::save(const TeamUiSnapshot& in)
{
    snapshot_ = in;
    has_snapshot_ = true;
}

void TeamUiMemoryStore::clear()
{
    snapshot_ = TeamUiSnapshot{};
    has_snapshot_ = false;
    runtime_memory() = TeamUiRuntimeMemory{};
}

ITeamUiStore& team_ui_get_store()
{
    return *s_store;
}

void team_ui_set_store(ITeamUiStore* store)
{
    s_store = store ? store : &s_memory_store;
}

bool team_ui_append_key_event(const TeamId& team_id,
                              TeamKeyEventType type,
                              uint32_t event_seq,
                              uint32_t ts,
                              const uint8_t* payload,
                              size_t len)
{
    (void)ts;
    (void)payload;
    (void)len;

    TeamUiSnapshot snapshot{};
    if (!team_ui_get_store().load(snapshot))
    {
        snapshot = TeamUiSnapshot{};
    }

    snapshot.team_id = team_id;
    snapshot.has_team_id = true;
    snapshot.last_event_seq = event_seq;

    if (type == TeamKeyEventType::TeamCreated)
    {
        snapshot.in_team = true;
        snapshot.self_is_leader = true;
        snapshot.kicked_out = false;
    }

    team_ui_get_store().save(snapshot);
    return true;
}

bool team_ui_posring_append(const TeamId& team_id,
                            uint32_t member_id,
                            int32_t lat_e7,
                            int32_t lon_e7,
                            int16_t alt_m,
                            uint16_t speed_dmps,
                            uint32_t ts)
{
    auto& positions = runtime_memory().latest_positions[team_id_to_u64(team_id)];
    positions.push_back(TeamPosSample{
        .member_id = member_id,
        .lat_e7 = lat_e7,
        .lon_e7 = lon_e7,
        .alt_m = alt_m,
        .speed_dmps = speed_dmps,
        .ts = ts,
    });
    while (positions.size() > kMaxPosSamplesPerTeam)
    {
        positions.pop_front();
    }
    return true;
}

bool team_ui_posring_load_latest(const TeamId& team_id, std::vector<TeamPosSample>& out)
{
    out.clear();
    const auto it = runtime_memory().latest_positions.find(team_id_to_u64(team_id));
    if (it == runtime_memory().latest_positions.end())
    {
        return false;
    }

    out.assign(it->second.begin(), it->second.end());
    return !out.empty();
}

bool team_ui_chatlog_append(const TeamId& team_id,
                            uint32_t peer_id,
                            bool incoming,
                            uint32_t ts,
                            const std::string& text)
{
    return team_ui_chatlog_append_structured(team_id,
                                             peer_id,
                                             incoming,
                                             ts,
                                             team::proto::TeamChatType::Text,
                                             std::vector<uint8_t>(text.begin(), text.end()));
}

bool team_ui_chatlog_append_structured(const TeamId& team_id,
                                       uint32_t peer_id,
                                       bool incoming,
                                       uint32_t ts,
                                       team::proto::TeamChatType type,
                                       const std::vector<uint8_t>& payload)
{
    auto& chat_log = runtime_memory().chat_logs[team_id_to_u64(team_id)];
    chat_log.push_back(TeamChatLogEntry{
        .incoming = incoming,
        .ts = ts,
        .peer_id = peer_id,
        .type = type,
        .payload = payload,
    });
    while (chat_log.size() > kMaxChatEntriesPerTeam)
    {
        chat_log.pop_front();
    }
    return true;
}

bool team_ui_chatlog_load_recent(const TeamId& team_id,
                                 size_t max_count,
                                 std::vector<TeamChatLogEntry>& out)
{
    out.clear();
    const auto it = runtime_memory().chat_logs.find(team_id_to_u64(team_id));
    if (it == runtime_memory().chat_logs.end())
    {
        return false;
    }

    const auto& log = it->second;
    const size_t available = log.size();
    const size_t count = std::min(max_count, available);
    out.reserve(count);
    auto start = log.end();
    std::advance(start, -static_cast<long long>(count));
    out.assign(start, log.end());
    return !out.empty();
}

bool team_ui_save_keys_now(const TeamId& team_id,
                           uint32_t key_id,
                           const std::array<uint8_t, team::proto::kTeamChannelPskSize>& psk)
{
    TeamUiSnapshot snapshot{};
    if (!team_ui_get_store().load(snapshot))
    {
        snapshot = TeamUiSnapshot{};
    }
    snapshot.team_id = team_id;
    snapshot.has_team_id = true;
    snapshot.security_round = key_id;
    snapshot.team_psk = psk;
    snapshot.has_team_psk = true;
    team_ui_get_store().save(snapshot);
    return true;
}

bool team_ui_append_member_track(const TeamId& team_id,
                                 uint32_t member_id,
                                 const team::proto::TeamTrackMessage& track)
{
    if (track.points.empty() || track.valid_mask == 0)
    {
        return false;
    }

    bool has_valid = false;
    for (size_t i = 0; i < track.points.size(); ++i)
    {
        if ((track.valid_mask & (1u << static_cast<uint32_t>(i))) != 0)
        {
            has_valid = true;
            break;
        }
    }
    if (!has_valid)
    {
        return false;
    }

    const std::filesystem::path path = member_track_path_for(team_id, member_id);
    if (path.empty())
    {
        return false;
    }

    std::error_code ec;
    const bool file_exists = std::filesystem::exists(path, ec) && !ec;
    std::ofstream stream(path, std::ios::binary | std::ios::app);
    if (!stream.is_open())
    {
        return false;
    }

    if (!file_exists || std::filesystem::file_size(path, ec) == 0 || ec)
    {
        stream << kGpxHeader;
    }

    for (size_t i = 0; i < track.points.size(); ++i)
    {
        if ((track.valid_mask & (1u << static_cast<uint32_t>(i))) == 0)
        {
            continue;
        }

        const auto& pt = track.points[i];
        const double lat = static_cast<double>(pt.lat_e7) / 1e7;
        const double lon = static_cast<double>(pt.lon_e7) / 1e7;
        const uint32_t ts = track.start_ts + static_cast<uint32_t>(track.interval_s) * static_cast<uint32_t>(i);

        char line[128] = {0};
        std::snprintf(line, sizeof(line), "<trkpt lat=\"%.7f\" lon=\"%.7f\">\n", lat, lon);
        stream << line;
        stream << "  <ele>0.0</ele>\n";
        if (ts >= kMinValidEpoch)
        {
            stream << "  <time>" << iso_time(static_cast<std::time_t>(ts)) << "</time>\n";
        }
        stream << "  <extensions>\n";
        stream << "    <speed>0.00</speed>\n";
        stream << "    <course>0.0</course>\n";
        stream << "    <hdop>0.0</hdop>\n";
        stream << "    <sat>0</sat>\n";
        stream << "  </extensions>\n";
        stream << "</trkpt>\n";
    }
    stream.flush();
    return stream.good();
}

bool team_ui_get_member_track_path(const TeamId& team_id, uint32_t member_id, std::string& out_path)
{
    const std::filesystem::path path = member_track_path_for(team_id, member_id);
    if (path.empty())
    {
        out_path.clear();
        return false;
    }
    out_path = path.string();
    return true;
}

} // namespace team::ui
