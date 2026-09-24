#include "gps/gpx/track_writer.h"
#include "platform/esp/arduino_common/gps/sd_gpx_output.h"
/**
 * @file team_ui_store.cpp
 * @brief ESP/SD-backed Team UI snapshot store (kept outside ui_shared)
 */

#include "platform/ui/team_ui_store_runtime.h"
#include "ui/team_persistence/team_ui_snapshot_codec.h"

#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "sys/clock.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace team
{
namespace ui
{

bool TeamUiSnapshotMemoryStore::has_snapshot_ = false;
TeamUiSnapshot TeamUiSnapshotMemoryStore::snapshot_{};

namespace
{
using ::platform::esp::arduino_common::storage::sd_card_ready;
using ::platform::esp::arduino_common::storage::sd_exists;
using ::platform::esp::arduino_common::storage::sd_mkdir;
using ::platform::esp::arduino_common::storage::sd_remove;
using ::platform::esp::arduino_common::storage::sd_rename;
using ::platform::esp::arduino_common::storage::SdRuntimeFile;

constexpr const char* kBaseDir = "/team";
constexpr const char* kCurrentPath = "/team/current.txt";
constexpr const char* kCurrentTmpPath = "/team/current.tmp";
constexpr const char* kSnapshotName = "snapshot.bin";
constexpr const char* kSnapshotTmpName = "snapshot.tmp";
constexpr const char* kEventsName = "events.log";
constexpr const char* kKeysName = "keys.bin";
constexpr const char* kKeysTmpName = "keys.tmp";
constexpr const char* kPosringName = "posring.log";
constexpr const char* kChatlogName = "chatlog.log";
constexpr const char* kChatlogOldName = "chatlog.old";
constexpr const char* kTracksDirName = "tracks";
constexpr const char* kGpxHeader =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<gpx version=\"1.1\" creator=\"Trail-Mate\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
    "<trk>\n"
    "<trkseg>\n";

constexpr uint16_t kEventVersion = 1;
constexpr uint8_t kKeysVersion = 1;
constexpr uint8_t kPosringVersion = 1;
constexpr uint8_t kChatlogVersionV1 = 1;
constexpr uint8_t kChatlogVersionV2 = 2;

constexpr uint8_t kRoleNone = 0;
constexpr uint8_t kRoleMember = 1;
constexpr uint8_t kRoleLeader = 2;

constexpr uint32_t kPosRecSize = 28;
constexpr uint32_t kPosRingCapacity = kPosRecSize * 512;
constexpr uint32_t kPosHeaderSize = 24;
constexpr uint32_t kPosMinIntervalSec = 15;
constexpr uint32_t kPosMaxIntervalSec = 30;
constexpr float kPosMinDistanceM = 20.0f;
constexpr size_t kChatlogMaxBytes = 256 * 1024;
constexpr uint32_t kMinValidEpoch = 1577836800U; // 2020-01-01

uint32_t now_secs()
{
    return sys::uptime_seconds_now();
}

uint32_t member_presence_fingerprint(const TeamUiSnapshot& in)
{
    uint32_t h = 2166136261u;
    auto mix = [&](uint32_t value)
    {
        h ^= value;
        h *= 16777619u;
    };
    mix(static_cast<uint32_t>(in.members.size()));
    for (const auto& member : in.members)
    {
        mix(member.node_id);
        mix(member.leader ? 1U : 0U);
        mix(member.last_seen_s);
    }
    return h;
}

uint64_t team_id_to_u64(const TeamId& id)
{
    uint64_t value = 0;
    for (size_t i = 0; i < id.size(); ++i)
    {
        value |= (static_cast<uint64_t>(id[i]) << (8 * i));
    }
    return value;
}

TeamId team_id_from_u64(uint64_t value)
{
    TeamId id{};
    for (size_t i = 0; i < id.size(); ++i)
    {
        id[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
    }
    return id;
}

std::string base32_from_u64(uint64_t value)
{
    static const char* kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    char buf[13];
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
    size_t first = full.find_first_not_of('A');
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

bool ensure_dir(const char* path)
{
    if (sd_exists(path))
    {
        return true;
    }
    return sd_mkdir(path);
}

std::string iso_time(time_t t)
{
    if (t <= 0)
    {
        t = time(nullptr);
    }
    struct tm tm_utc
    {
    };
    char buf[32] = {0};
    if (t > 0 && gmtime_r(&t, &tm_utc))
    {
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
        return std::string(buf);
    }
    return std::string("1970-01-01T00:00:00Z");
}

std::string trim_copy(const std::string& value)
{
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0)
    {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
    {
        --end;
    }

    return value.substr(start, end - start);
}

std::string read_line(SdRuntimeFile& file)
{
    std::string line;
    while (file.available())
    {
        const int raw = file.read_byte();
        if (raw < 0 || raw == '\n')
        {
            break;
        }
        if (raw != '\r')
        {
            line.push_back(static_cast<char>(raw));
        }
    }
    return trim_copy(line);
}

bool read_current_dir(std::string& out_dir)
{
    if (!sd_card_ready() || !sd_exists(kCurrentPath))
    {
        return false;
    }
    SdRuntimeFile f;
    if (!f.open(kCurrentPath, "r"))
    {
        return false;
    }
    const std::string line = read_line(f);
    f.close();
    if (line.empty())
    {
        return false;
    }
    out_dir = line;
    return true;
}

bool write_current_dir(const std::string& dir)
{
    if (!sd_card_ready())
    {
        return false;
    }
    if (!ensure_dir(kBaseDir))
    {
        return false;
    }
    SdRuntimeFile f;
    if (!f.open(kCurrentTmpPath, "w"))
    {
        return false;
    }
    f.print(dir.c_str());
    f.print("\n");
    f.flush();
    f.close();
    if (sd_exists(kCurrentPath))
    {
        sd_remove(kCurrentPath);
    }
    return sd_rename(kCurrentTmpPath, kCurrentPath);
}

bool clear_current_dir()
{
    if (!sd_card_ready())
    {
        return false;
    }
    if (sd_exists(kCurrentPath))
    {
        sd_remove(kCurrentPath);
    }
    if (sd_exists(kCurrentTmpPath))
    {
        sd_remove(kCurrentTmpPath);
    }
    return true;
}

bool ensure_team_dir_for_id_internal(const TeamId& id, std::string& out_dir_path, bool update_current)
{
    if (!sd_card_ready())
    {
        return false;
    }
    if (!ensure_dir(kBaseDir))
    {
        return false;
    }
    std::string dir = team_dir_from_id(id);
    out_dir_path = std::string(kBaseDir) + "/" + dir;
    if (!ensure_dir(out_dir_path.c_str()))
    {
        return false;
    }
    if (update_current)
    {
        write_current_dir(dir);
    }
    return true;
}

bool ensure_team_dir_for_id(const TeamId& id, std::string& out_dir_path)
{
    return ensure_team_dir_for_id_internal(id, out_dir_path, true);
}

void write_u8(SdRuntimeFile& f, uint8_t v)
{
    f.write(&v, 1);
}

void write_u16(SdRuntimeFile& f, uint16_t v)
{
    uint8_t b[2] = {
        static_cast<uint8_t>(v & 0xFF),
        static_cast<uint8_t>((v >> 8) & 0xFF)};
    f.write(b, 2);
}

void write_u32(SdRuntimeFile& f, uint32_t v)
{
    uint8_t b[4];
    for (int i = 0; i < 4; ++i)
    {
        b[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
    }
    f.write(b, 4);
}

void write_u64(SdRuntimeFile& f, uint64_t v)
{
    uint8_t b[8];
    for (int i = 0; i < 8; ++i)
    {
        b[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
    }
    f.write(b, 8);
}

bool read_u8(const std::vector<uint8_t>& buf, size_t& off, uint8_t& out)
{
    if (off + 1 > buf.size())
    {
        return false;
    }
    out = buf[off];
    off += 1;
    return true;
}

bool read_u16(const std::vector<uint8_t>& buf, size_t& off, uint16_t& out)
{
    if (off + 2 > buf.size())
    {
        return false;
    }
    out = static_cast<uint16_t>(buf[off]) |
          (static_cast<uint16_t>(buf[off + 1]) << 8);
    off += 2;
    return true;
}

bool read_u32(const std::vector<uint8_t>& buf, size_t& off, uint32_t& out)
{
    if (off + 4 > buf.size())
    {
        return false;
    }
    out = static_cast<uint32_t>(buf[off]) |
          (static_cast<uint32_t>(buf[off + 1]) << 8) |
          (static_cast<uint32_t>(buf[off + 2]) << 16) |
          (static_cast<uint32_t>(buf[off + 3]) << 24);
    off += 4;
    return true;
}

bool read_u64(const std::vector<uint8_t>& buf, size_t& off, uint64_t& out)
{
    if (off + 8 > buf.size())
    {
        return false;
    }
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
    {
        v |= (static_cast<uint64_t>(buf[off + i]) << (8 * i));
    }
    out = v;
    off += 8;
    return true;
}

int find_member_index(const TeamUiSnapshot& snap, uint32_t node_id)
{
    for (size_t i = 0; i < snap.members.size(); ++i)
    {
        if (snap.members[i].node_id == node_id)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void apply_key_event(TeamUiSnapshot& snap, TeamKeyEventType type, const std::vector<uint8_t>& payload)
{
    size_t off = 0;
    if (type == TeamKeyEventType::TeamCreated)
    {
        uint64_t team_id = 0;
        uint32_t leader_id = 0;
        uint32_t epoch = 0;
        if (!read_u64(payload, off, team_id) ||
            !read_u32(payload, off, leader_id) ||
            !read_u32(payload, off, epoch))
        {
            return;
        }
        snap.team_id = team_id_from_u64(team_id);
        snap.has_team_id = true;
        snap.in_team = true;
        snap.security_round = epoch;
        snap.self_is_leader = (leader_id == 0);
        if (find_member_index(snap, leader_id) < 0)
        {
            TeamMemberUi leader;
            leader.node_id = leader_id;
            leader.leader = true;
            snap.members.push_back(leader);
        }
    }
    else if (type == TeamKeyEventType::MemberAccepted)
    {
        uint32_t member_id = 0;
        uint8_t role = kRoleMember;
        if (!read_u32(payload, off, member_id))
        {
            return;
        }
        if (off < payload.size())
        {
            uint8_t role_in = 0;
            if (read_u8(payload, off, role_in))
            {
                role = role_in;
            }
        }
        int idx = find_member_index(snap, member_id);
        if (idx < 0)
        {
            TeamMemberUi m;
            m.node_id = member_id;
            snap.members.push_back(m);
            idx = static_cast<int>(snap.members.size() - 1);
        }
        snap.members[idx].leader = (role == kRoleLeader);
        if (member_id == 0)
        {
            snap.self_is_leader = (role == kRoleLeader);
            snap.in_team = true;
        }
    }
    else if (type == TeamKeyEventType::MemberKicked)
    {
        uint32_t member_id = 0;
        if (!read_u32(payload, off, member_id))
        {
            return;
        }
        int idx = find_member_index(snap, member_id);
        if (idx >= 0)
        {
            snap.members.erase(snap.members.begin() + idx);
        }
        if (member_id == 0)
        {
            snap.in_team = false;
            snap.self_is_leader = false;
        }
    }
    else if (type == TeamKeyEventType::LeaderTransferred)
    {
        uint32_t leader_id = 0;
        if (!read_u32(payload, off, leader_id))
        {
            return;
        }
        for (auto& m : snap.members)
        {
            m.leader = false;
        }
        int idx = find_member_index(snap, leader_id);
        if (idx < 0)
        {
            TeamMemberUi leader;
            leader.node_id = leader_id;
            leader.leader = true;
            snap.members.push_back(leader);
        }
        else
        {
            snap.members[idx].leader = true;
        }
        snap.self_is_leader = (leader_id == 0);
    }
    else if (type == TeamKeyEventType::EpochRotated)
    {
        uint32_t epoch = 0;
        if (!read_u32(payload, off, epoch))
        {
            return;
        }
        snap.security_round = epoch;
    }
}

bool load_snapshot_from_path(const std::string& snapshot_path, TeamUiSnapshot& out)
{
    if (!sd_exists(snapshot_path.c_str()))
    {
        return false;
    }
    SdRuntimeFile f;
    if (!f.open(snapshot_path.c_str(), "r"))
    {
        return false;
    }
    size_t file_size = f.size();
    std::vector<uint8_t> buf(file_size);
    bool ok = (f.read(buf.data(), buf.size()) == buf.size());
    f.close();
    if (!ok || buf.size() < 32)
    {
        return false;
    }

    return ::ui::team_persistence::decodeTeamUiSnapshot(buf.data(),
                                                        buf.size(),
                                                        out);
}

bool save_snapshot_to_path(const std::string& dir_path, const TeamUiSnapshot& in)
{
    if (!sd_card_ready())
    {
        return false;
    }
    if (!ensure_dir(kBaseDir))
    {
        return false;
    }
    if (!ensure_dir(dir_path.c_str()))
    {
        return false;
    }

    std::string tmp_path = dir_path + "/" + kSnapshotTmpName;
    std::string out_path = dir_path + "/" + kSnapshotName;

    std::vector<uint8_t> encoded;
    if (!::ui::team_persistence::encodeTeamUiSnapshot(in, now_secs(), encoded))
    {
        return false;
    }

    SdRuntimeFile f;
    if (!f.open(tmp_path.c_str(), "w"))
    {
        return false;
    }

    f.write(encoded.data(), encoded.size());

    f.flush();
    f.close();

    std::string out_path_c = out_path;
    if (sd_exists(out_path_c.c_str()))
    {
        sd_remove(out_path_c.c_str());
    }
    return sd_rename(tmp_path.c_str(), out_path_c.c_str());
}

bool load_keys_from_path(const std::string& keys_path, TeamUiSnapshot& out)
{
    if (!sd_exists(keys_path.c_str()))
    {
        return false;
    }
    SdRuntimeFile f;
    if (!f.open(keys_path.c_str(), "r"))
    {
        return false;
    }
    size_t file_size = f.size();
    std::vector<uint8_t> buf(file_size);
    bool ok = (f.read(buf.data(), buf.size()) == buf.size());
    f.close();
    if (!ok || buf.size() < 22)
    {
        return false;
    }
    if (std::memcmp(buf.data(), "TMK1", 4) != 0)
    {
        return false;
    }
    size_t off = 4;
    uint8_t version = 0;
    uint8_t psk_len = 0;
    uint16_t reserved = 0;
    uint64_t team_id = 0;
    uint32_t key_id = 0;
    if (!read_u8(buf, off, version) ||
        !read_u8(buf, off, psk_len) ||
        !read_u16(buf, off, reserved) ||
        !read_u64(buf, off, team_id) ||
        !read_u32(buf, off, key_id))
    {
        return false;
    }
    if (version != kKeysVersion || psk_len == 0 ||
        off + psk_len > buf.size() ||
        psk_len > out.team_psk.size())
    {
        return false;
    }
    out.team_id = team_id_from_u64(team_id);
    out.has_team_id = (team_id != 0);
    out.security_round = key_id;
    std::fill(out.team_psk.begin(), out.team_psk.end(), 0);
    std::memcpy(out.team_psk.data(), buf.data() + off, psk_len);
    out.has_team_psk = true;
    return true;
}

bool save_keys_to_path(const std::string& dir_path, const TeamUiSnapshot& in)
{
    if (!in.has_team_id || !in.has_team_psk)
    {
        return false;
    }
    std::string tmp_path = dir_path + "/" + kKeysTmpName;
    std::string out_path = dir_path + "/" + kKeysName;
    SdRuntimeFile f;
    if (!f.open(tmp_path.c_str(), "w"))
    {
        return false;
    }
    f.write(reinterpret_cast<const uint8_t*>("TMK1"), 4);
    write_u8(f, kKeysVersion);
    write_u8(f, static_cast<uint8_t>(in.team_psk.size()));
    write_u16(f, 0);
    write_u64(f, team_id_to_u64(in.team_id));
    write_u32(f, in.security_round);
    f.write(in.team_psk.data(), in.team_psk.size());
    f.flush();
    f.close();
    if (sd_exists(out_path.c_str()))
    {
        sd_remove(out_path.c_str());
    }
    return sd_rename(tmp_path.c_str(), out_path.c_str());
}

bool load_events_apply(const std::string& events_path, TeamUiSnapshot& out)
{
    if (!sd_exists(events_path.c_str()))
    {
        return false;
    }
    SdRuntimeFile f;
    if (!f.open(events_path.c_str(), "r"))
    {
        return false;
    }
    size_t file_size = f.size();
    std::vector<uint8_t> buf(file_size);
    bool ok = (f.read(buf.data(), buf.size()) == buf.size());
    f.close();
    if (!ok)
    {
        return false;
    }

    size_t off = 0;
    uint32_t last_seq = out.last_event_seq;
    bool applied = false;

    while (off + 12 <= buf.size())
    {
        if (buf[off] != 'E' || buf[off + 1] != 'V')
        {
            break;
        }
        off += 2;
        uint8_t version = 0;
        uint8_t type = 0;
        uint32_t seq = 0;
        uint32_t ts = 0;
        uint16_t payload_len = 0;
        uint16_t reserved = 0;
        if (!read_u8(buf, off, version) ||
            !read_u8(buf, off, type) ||
            !read_u32(buf, off, seq) ||
            !read_u32(buf, off, ts) ||
            !read_u16(buf, off, payload_len) ||
            !read_u16(buf, off, reserved))
        {
            break;
        }
        if (version != kEventVersion)
        {
            break;
        }
        if (off + payload_len > buf.size())
        {
            break;
        }
        if (seq > last_seq)
        {
            std::vector<uint8_t> payload;
            if (payload_len > 0)
            {
                payload.assign(buf.begin() + off, buf.begin() + off + payload_len);
            }
            apply_key_event(out, static_cast<TeamKeyEventType>(type), payload);
            out.last_event_seq = seq;
            applied = true;
        }
        off += payload_len;
        (void)ts;
        (void)reserved;
    }
    return applied;
}

bool append_event(const TeamId& team_id, TeamKeyEventType type,
                  uint32_t event_seq, uint32_t ts,
                  const uint8_t* payload, size_t len)
{
    if (!sd_card_ready())
    {
        return false;
    }
    std::string dir_path;
    if (!ensure_team_dir_for_id(team_id, dir_path))
    {
        return false;
    }

    std::string events_path = dir_path + "/" + kEventsName;
    SdRuntimeFile f;
    if (!f.open(events_path.c_str(), "a"))
    {
        return false;
    }
    f.write(reinterpret_cast<const uint8_t*>("EV"), 2);
    write_u8(f, kEventVersion);
    write_u8(f, static_cast<uint8_t>(type));
    write_u32(f, event_seq);
    write_u32(f, ts);
    write_u16(f, static_cast<uint16_t>(len));
    write_u16(f, 0);
    if (payload && len > 0)
    {
        f.write(payload, len);
    }
    f.flush();
    f.close();
    return true;
}

struct PosThrottleState
{
    uint32_t member_id = 0;
    uint32_t ts = 0;
    int32_t lat_e7 = 0;
    int32_t lon_e7 = 0;
};

std::vector<PosThrottleState> s_pos_throttle;

bool should_write_pos(uint32_t member_id, int32_t lat_e7, int32_t lon_e7, uint32_t ts)
{
    for (auto& item : s_pos_throttle)
    {
        if (item.member_id != member_id)
        {
            continue;
        }
        uint32_t dt = (ts > item.ts) ? (ts - item.ts) : 0;
        if (dt >= kPosMaxIntervalSec)
        {
            item.ts = ts;
            item.lat_e7 = lat_e7;
            item.lon_e7 = lon_e7;
            return true;
        }
        if (dt < kPosMinIntervalSec)
        {
            return false;
        }
        float dlat = (lat_e7 - item.lat_e7) / 1e7f;
        float dlon = (lon_e7 - item.lon_e7) / 1e7f;
        float meters_per_deg = 111320.0f;
        float lat_m = dlat * meters_per_deg;
        float lon_m = dlon * meters_per_deg;
        float dist = std::sqrt(lat_m * lat_m + lon_m * lon_m);
        if (dist < kPosMinDistanceM)
        {
            return false;
        }
        item.ts = ts;
        item.lat_e7 = lat_e7;
        item.lon_e7 = lon_e7;
        return true;
    }
    PosThrottleState state;
    state.member_id = member_id;
    state.ts = ts;
    state.lat_e7 = lat_e7;
    state.lon_e7 = lon_e7;
    s_pos_throttle.push_back(state);
    return true;
}

bool init_posring(SdRuntimeFile& f)
{
    f.seek(0);
    f.write(reinterpret_cast<const uint8_t*>("PSR1"), 4);
    write_u8(f, kPosringVersion);
    write_u8(f, 0);
    write_u8(f, 0);
    write_u8(f, 0);
    write_u32(f, kPosRingCapacity);
    write_u32(f, 0);
    write_u32(f, kPosRecSize);
    write_u32(f, 0);
    f.flush();
    return true;
}

bool read_posring_header(SdRuntimeFile& f, uint32_t& write_offset)
{
    f.seek(0);
    uint8_t header[kPosHeaderSize];
    if (f.read(header, sizeof(header)) != sizeof(header))
    {
        return false;
    }
    if (std::memcmp(header, "PSR1", 4) != 0 || header[4] != kPosringVersion)
    {
        return false;
    }
    uint32_t data_capacity = static_cast<uint32_t>(header[8]) |
                             (static_cast<uint32_t>(header[9]) << 8) |
                             (static_cast<uint32_t>(header[10]) << 16) |
                             (static_cast<uint32_t>(header[11]) << 24);
    uint32_t offset = static_cast<uint32_t>(header[12]) |
                      (static_cast<uint32_t>(header[13]) << 8) |
                      (static_cast<uint32_t>(header[14]) << 16) |
                      (static_cast<uint32_t>(header[15]) << 24);
    uint32_t rec_size = static_cast<uint32_t>(header[16]) |
                        (static_cast<uint32_t>(header[17]) << 8) |
                        (static_cast<uint32_t>(header[18]) << 16) |
                        (static_cast<uint32_t>(header[19]) << 24);
    if (data_capacity != kPosRingCapacity || rec_size != kPosRecSize)
    {
        return false;
    }
    if (offset >= data_capacity)
    {
        offset = 0;
    }
    write_offset = offset;
    return true;
}

bool write_posring_header(SdRuntimeFile& f, uint32_t write_offset)
{
    f.seek(0);
    f.write(reinterpret_cast<const uint8_t*>("PSR1"), 4);
    write_u8(f, kPosringVersion);
    write_u8(f, 0);
    write_u8(f, 0);
    write_u8(f, 0);
    write_u32(f, kPosRingCapacity);
    write_u32(f, write_offset);
    write_u32(f, kPosRecSize);
    write_u32(f, 0);
    f.flush();
    return true;
}

TeamUiSnapshot s_cached_snapshot{};
bool s_has_cached_snapshot = false;

class TeamUiSnapshotStorePersisted : public ITeamUiSnapshotStore
{
  public:
    bool load(TeamUiSnapshot& out) override
    {
        if (s_has_cached_snapshot)
        {
            out = s_cached_snapshot;
            return true;
        }
        if (!sd_card_ready())
        {
            return false;
        }

        TeamUiSnapshot snap;
        std::string dir;
        bool has_current = read_current_dir(dir);
        bool loaded = false;
        if (has_current)
        {
            std::string dir_path = std::string(kBaseDir) + "/" + dir;
            std::string snapshot_path = dir_path + "/" + kSnapshotName;
            if (load_snapshot_from_path(snapshot_path, snap))
            {
                loaded = true;
            }
            std::string keys_path = dir_path + "/" + kKeysName;
            if (load_keys_from_path(keys_path, snap))
            {
                loaded = true;
            }
            std::string events_path = dir_path + "/" + kEventsName;
            if (load_events_apply(events_path, snap))
            {
                loaded = true;
            }
        }

        if (!loaded)
        {
            return false;
        }

        snap.pending_join = false;
        snap.pending_join_started_s = 0;
        snap.kicked_out = false;
        if (!snap.has_team_psk)
        {
            std::fill(snap.team_psk.begin(), snap.team_psk.end(), 0);
        }

        s_cached_snapshot = snap;
        s_has_cached_snapshot = true;
        out = snap;
        return true;
    }

    void save(const TeamUiSnapshot& in) override
    {
        s_cached_snapshot = in;
        s_has_cached_snapshot = true;
        if (!sd_card_ready())
        {
            return;
        }
        if (!in.has_team_id || !in.in_team)
        {
            clear_current_dir();
            return;
        }

        uint32_t now = now_secs();
        bool force_write = (in.in_team != last_snapshot_in_team_) ||
                           (in.self_is_leader != last_snapshot_self_is_leader_) ||
                           (in.security_round != last_snapshot_epoch_) ||
                           (in.has_team_psk != last_snapshot_has_psk_) ||
                           (in.team_chat_unread != last_snapshot_unread_) ||
                           (member_presence_fingerprint(in) != last_member_presence_fingerprint_);
        bool seq_trigger = (in.last_event_seq >= last_snapshot_seq_ + 10);
        bool time_trigger = (now - last_snapshot_ts_ >= 60);

        if (!force_write && !seq_trigger && !time_trigger)
        {
            return;
        }

        std::string dir = team_dir_from_id(in.team_id);
        std::string dir_path = std::string(kBaseDir) + "/" + dir;
        write_current_dir(dir);
        if (save_snapshot_to_path(dir_path, in))
        {
            if (in.has_team_psk)
            {
                save_keys_to_path(dir_path, in);
            }
            last_snapshot_ts_ = now;
            last_snapshot_seq_ = in.last_event_seq;
            last_snapshot_in_team_ = in.in_team;
            last_snapshot_self_is_leader_ = in.self_is_leader;
            last_snapshot_epoch_ = in.security_round;
            last_snapshot_has_psk_ = in.has_team_psk;
            last_snapshot_unread_ = in.team_chat_unread;
            last_member_presence_fingerprint_ = member_presence_fingerprint(in);
        }
    }

    void clear() override
    {
        s_has_cached_snapshot = false;
        clear_current_dir();
    }

  private:
    uint32_t last_snapshot_ts_ = 0;
    uint32_t last_snapshot_seq_ = 0;
    bool last_snapshot_in_team_ = false;
    bool last_snapshot_self_is_leader_ = false;
    uint32_t last_snapshot_epoch_ = 0;
    bool last_snapshot_has_psk_ = false;
    uint32_t last_snapshot_unread_ = 0;
    uint32_t last_member_presence_fingerprint_ = 0;
};

TeamUiSnapshotStorePersisted s_persisted_snapshot_store;
} // namespace

bool TeamUiSnapshotMemoryStore::load(TeamUiSnapshot& out)
{
    if (!has_snapshot_)
    {
        return false;
    }
    out = snapshot_;
    return true;
}

void TeamUiSnapshotMemoryStore::save(const TeamUiSnapshot& in)
{
    snapshot_ = in;
    has_snapshot_ = true;
}

void TeamUiSnapshotMemoryStore::clear()
{
    has_snapshot_ = false;
}

namespace
{
TeamUiSnapshotMemoryStore s_snapshot_memory_store;
ITeamUiSnapshotStore* s_snapshot_store = &s_persisted_snapshot_store;

class TeamUiSdChatLogStore final : public ITeamUiChatLogStore
{
  public:
    bool appendText(const TeamId& team_id,
                    uint32_t peer_id,
                    bool incoming,
                    uint32_t ts,
                    const std::string& text) override
    {
        std::vector<uint8_t> payload;
        payload.assign(text.begin(), text.end());
        return appendStructured(team_id,
                                peer_id,
                                incoming,
                                ts,
                                team::proto::TeamChatType::Text,
                                payload);
    }

    bool appendStructured(const TeamId& team_id,
                          uint32_t peer_id,
                          bool incoming,
                          uint32_t ts,
                          team::proto::TeamChatType type,
                          const std::vector<uint8_t>& payload) override;

    bool loadRecent(const TeamId& team_id,
                    std::size_t max_count,
                    std::vector<TeamChatLogEntry>& out) override;
};

TeamUiSdChatLogStore s_chat_log_store;
} // namespace

ITeamUiSnapshotStore& team_ui_snapshot_store()
{
    return *s_snapshot_store;
}

void team_ui_set_snapshot_store(ITeamUiSnapshotStore* store)
{
    if (store)
    {
        s_snapshot_store = store;
    }
    else
    {
        s_snapshot_store = &s_snapshot_memory_store;
    }
}

ITeamUiStore& team_ui_get_store()
{
    return team_ui_snapshot_store();
}

void team_ui_set_store(ITeamUiStore* store)
{
    team_ui_set_snapshot_store(store);
}

ITeamUiChatLogStore& team_ui_chat_log_store()
{
    return s_chat_log_store;
}

bool team_ui_append_key_event(const TeamId& team_id,
                              TeamKeyEventType type,
                              uint32_t event_seq,
                              uint32_t ts,
                              const uint8_t* payload,
                              size_t len)
{
    return append_event(team_id, type, event_seq, ts, payload, len);
}

bool team_ui_posring_append(const TeamId& team_id,
                            uint32_t member_id,
                            int32_t lat_e7,
                            int32_t lon_e7,
                            int16_t alt_m,
                            uint16_t speed_dmps,
                            uint32_t ts)
{
    if (!should_write_pos(member_id, lat_e7, lon_e7, ts))
    {
        return false;
    }

    std::string dir_path;
    if (!ensure_team_dir_for_id(team_id, dir_path))
    {
        return false;
    }

    std::string path = dir_path + "/" + kPosringName;
    SdRuntimeFile f;
    bool exists = f.open(path.c_str(), "r");
    if (exists)
    {
        f.close();
    }
    SdRuntimeFile rw;
    if (!rw.open(path.c_str(), exists ? "r+" : "w+"))
    {
        return false;
    }
    if (!exists)
    {
        init_posring(rw);
    }

    uint32_t write_offset = 0;
    if (!read_posring_header(rw, write_offset))
    {
        init_posring(rw);
        write_offset = 0;
    }

    uint32_t data_offset = kPosHeaderSize + write_offset;
    rw.seek(data_offset);
    write_u16(rw, 0x5053);
    write_u8(rw, kPosringVersion);
    write_u8(rw, 0);
    write_u32(rw, ts);
    write_u32(rw, member_id);
    write_u32(rw, static_cast<uint32_t>(lat_e7));
    write_u32(rw, static_cast<uint32_t>(lon_e7));
    write_u16(rw, static_cast<uint16_t>(alt_m));
    write_u16(rw, speed_dmps);

    write_offset += kPosRecSize;
    if (write_offset >= kPosRingCapacity)
    {
        write_offset = 0;
    }
    write_posring_header(rw, write_offset);
    rw.close();
    return true;
}

bool team_ui_posring_load_latest(const TeamId& team_id,
                                 std::vector<TeamPosSample>& out)
{
    out.clear();
    if (!sd_card_ready())
    {
        return false;
    }
    std::string dir = team_dir_from_id(team_id);
    std::string dir_path = std::string(kBaseDir) + "/" + dir;
    std::string path = dir_path + "/" + kPosringName;
    if (!sd_exists(path.c_str()))
    {
        return false;
    }
    SdRuntimeFile f;
    if (!f.open(path.c_str(), "r"))
    {
        return false;
    }
    uint32_t write_offset = 0;
    if (!read_posring_header(f, write_offset))
    {
        f.close();
        return false;
    }
    if (f.size() < kPosHeaderSize + kPosRecSize)
    {
        f.close();
        return false;
    }
    size_t data_size = kPosRingCapacity;
    if (kPosHeaderSize + data_size > f.size())
    {
        data_size = f.size() - kPosHeaderSize;
    }
    std::vector<uint8_t> buf(data_size);
    f.seek(kPosHeaderSize);
    bool ok = (f.read(buf.data(), buf.size()) == buf.size());
    f.close();
    if (!ok)
    {
        return false;
    }

    for (size_t base = 0; base + kPosRecSize <= buf.size(); base += kPosRecSize)
    {
        size_t off = base;
        uint16_t magic = 0;
        uint8_t ver = 0;
        uint8_t flags = 0;
        uint32_t ts = 0;
        uint32_t member_id = 0;
        uint32_t lat_u = 0;
        uint32_t lon_u = 0;
        uint16_t alt_u = 0;
        uint16_t speed_u = 0;
        if (!read_u16(buf, off, magic) ||
            !read_u8(buf, off, ver) ||
            !read_u8(buf, off, flags) ||
            !read_u32(buf, off, ts) ||
            !read_u32(buf, off, member_id) ||
            !read_u32(buf, off, lat_u) ||
            !read_u32(buf, off, lon_u) ||
            !read_u16(buf, off, alt_u) ||
            !read_u16(buf, off, speed_u))
        {
            break;
        }
        if (magic != 0x5053 || ver != kPosringVersion || ts == 0)
        {
            continue;
        }
        (void)flags;
        TeamPosSample sample;
        sample.member_id = member_id;
        sample.lat_e7 = static_cast<int32_t>(lat_u);
        sample.lon_e7 = static_cast<int32_t>(lon_u);
        sample.alt_m = static_cast<int16_t>(alt_u);
        sample.speed_dmps = speed_u;
        sample.ts = ts;

        auto it = std::find_if(out.begin(), out.end(),
                               [&](const TeamPosSample& s)
                               { return s.member_id == member_id; });
        if (it == out.end())
        {
            out.push_back(sample);
        }
        else if (sample.ts > it->ts)
        {
            *it = sample;
        }
    }
    return !out.empty();
}

bool team_ui_chatlog_append(const TeamId& team_id,
                            uint32_t peer_id,
                            bool incoming,
                            uint32_t ts,
                            const std::string& text)
{
    return team_ui_chat_log_store().appendText(team_id,
                                               peer_id,
                                               incoming,
                                               ts,
                                               text);
}

bool TeamUiSdChatLogStore::appendStructured(const TeamId& team_id,
                                            uint32_t peer_id,
                                            bool incoming,
                                            uint32_t ts,
                                            team::proto::TeamChatType type,
                                            const std::vector<uint8_t>& payload)
{
    std::string dir_path;
    if (!ensure_team_dir_for_id(team_id, dir_path))
    {
        return false;
    }

    std::string path = dir_path + "/" + kChatlogName;
    size_t record_len = 2 + 1 + 1 + 4 + 4 + 1 + 3 + 2 + 2 + payload.size();
    if (sd_exists(path.c_str()))
    {
        SdRuntimeFile f;
        if (f.open(path.c_str(), "r"))
        {
            size_t size = f.size();
            f.close();
            if (size + record_len > kChatlogMaxBytes)
            {
                std::string old_path = dir_path + "/" + kChatlogOldName;
                if (sd_exists(old_path.c_str()))
                {
                    sd_remove(old_path.c_str());
                }
                sd_rename(path.c_str(), old_path.c_str());
            }
        }
    }

    SdRuntimeFile out;
    if (!out.open(path.c_str(), "a"))
    {
        return false;
    }
    out.write(reinterpret_cast<const uint8_t*>("CH"), 2);
    write_u8(out, kChatlogVersionV2);
    write_u8(out, incoming ? 1 : 0);
    write_u32(out, ts);
    write_u32(out, peer_id);
    write_u8(out, static_cast<uint8_t>(type));
    write_u8(out, 0);
    write_u8(out, 0);
    write_u8(out, 0);
    uint16_t payload_len = static_cast<uint16_t>(payload.size());
    write_u16(out, payload_len);
    write_u16(out, 0);
    if (payload_len > 0)
    {
        out.write(payload.data(), payload_len);
    }
    out.flush();
    out.close();
    return true;
}

bool team_ui_chatlog_append_structured(const TeamId& team_id,
                                       uint32_t peer_id,
                                       bool incoming,
                                       uint32_t ts,
                                       team::proto::TeamChatType type,
                                       const std::vector<uint8_t>& payload)
{
    return team_ui_chat_log_store().appendStructured(team_id,
                                                     peer_id,
                                                     incoming,
                                                     ts,
                                                     type,
                                                     payload);
}

bool TeamUiSdChatLogStore::loadRecent(const TeamId& team_id,
                                      std::size_t max_count,
                                      std::vector<TeamChatLogEntry>& out)
{
    out.clear();
    if (!sd_card_ready())
    {
        return false;
    }
    std::string dir_path;
    if (!ensure_team_dir_for_id(team_id, dir_path))
    {
        return false;
    }
    std::string path = dir_path + "/" + kChatlogName;
    if (!sd_exists(path.c_str()))
    {
        return false;
    }
    SdRuntimeFile f;
    if (!f.open(path.c_str(), "r"))
    {
        return false;
    }
    size_t file_size = f.size();
    if (file_size == 0)
    {
        f.close();
        return false;
    }
    std::vector<uint8_t> buf(file_size);
    bool ok = (f.read(buf.data(), buf.size()) == buf.size());
    f.close();
    if (!ok)
    {
        return false;
    }

    size_t off = 0;
    while (off + 4 <= buf.size())
    {
        if (buf[off] != 'C' || buf[off + 1] != 'H')
        {
            break;
        }
        off += 2;
        uint8_t version = buf[off++];
        uint8_t flags = buf[off++];
        TeamChatLogEntry entry;
        entry.incoming = (flags & 0x01) != 0;

        uint32_t ts = 0;
        uint32_t peer_id = 0;
        uint16_t payload_len = 0;

        if (version == kChatlogVersionV1)
        {
            if (!read_u32(buf, off, ts) ||
                !read_u32(buf, off, peer_id))
            {
                break;
            }
            uint16_t text_len = 0;
            uint16_t reserved = 0;
            if (!read_u16(buf, off, text_len) ||
                !read_u16(buf, off, reserved))
            {
                break;
            }
            if (off + text_len > buf.size())
            {
                break;
            }
            entry.type = team::proto::TeamChatType::Text;
            entry.ts = ts;
            entry.peer_id = peer_id;
            if (text_len > 0)
            {
                entry.payload.assign(buf.begin() + off, buf.begin() + off + text_len);
            }
            off += text_len;
        }
        else if (version == kChatlogVersionV2)
        {
            if (!read_u32(buf, off, ts) ||
                !read_u32(buf, off, peer_id))
            {
                break;
            }
            if (off + 4 > buf.size())
            {
                break;
            }
            entry.type = static_cast<team::proto::TeamChatType>(buf[off]);
            off += 4;
            uint16_t reserved = 0;
            if (!read_u16(buf, off, payload_len) ||
                !read_u16(buf, off, reserved))
            {
                break;
            }
            if (off + payload_len > buf.size())
            {
                break;
            }
            entry.ts = ts;
            entry.peer_id = peer_id;
            if (payload_len > 0)
            {
                entry.payload.assign(buf.begin() + off, buf.begin() + off + payload_len);
            }
            off += payload_len;
        }
        else
        {
            break;
        }

        if (max_count > 0 && out.size() >= max_count)
        {
            out.erase(out.begin());
        }
        out.push_back(std::move(entry));
    }
    return !out.empty();
}

bool team_ui_chatlog_load_recent(const TeamId& team_id,
                                 size_t max_count,
                                 std::vector<TeamChatLogEntry>& out)
{
    return team_ui_chat_log_store().loadRecent(team_id, max_count, out);
}

bool team_ui_save_keys_now(const TeamId& team_id,
                           uint32_t key_id,
                           const std::array<uint8_t, team::proto::kTeamChannelPskSize>& psk)
{
    if (!sd_card_ready())
    {
        return false;
    }
    if (key_id == 0)
    {
        return false;
    }
    std::string dir_path;
    if (!ensure_team_dir_for_id(team_id, dir_path))
    {
        return false;
    }
    TeamUiSnapshot snap;
    snap.team_id = team_id;
    snap.has_team_id = true;
    snap.security_round = key_id;
    snap.team_psk = psk;
    snap.has_team_psk = true;
    return save_keys_to_path(dir_path, snap);
}

bool team_ui_get_member_track_path(const TeamId& team_id,
                                   uint32_t member_id,
                                   std::string& out_path)
{
    if (!sd_card_ready())
    {
        return false;
    }
    std::string dir_path;
    if (!ensure_team_dir_for_id_internal(team_id, dir_path, false))
    {
        return false;
    }
    std::string tracks_dir = dir_path + "/" + kTracksDirName;
    if (!ensure_dir(tracks_dir.c_str()))
    {
        return false;
    }
    char name[24] = {0};
    snprintf(name, sizeof(name), "%08lX.gpx", static_cast<unsigned long>(member_id));
    out_path = tracks_dir + "/" + name;
    return true;
}

bool team_ui_append_member_track(const TeamId& team_id,
                                 uint32_t member_id,
                                 const team::proto::TeamTrackMessage& track)
{
    if (!sd_card_ready())
    {
        return false;
    }
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

    std::string dir_path;
    if (!ensure_team_dir_for_id_internal(team_id, dir_path, true))
    {
        return false;
    }
    std::string tracks_dir = dir_path + "/" + kTracksDirName;
    if (!ensure_dir(tracks_dir.c_str()))
    {
        return false;
    }
    char name[24] = {0};
    snprintf(name, sizeof(name), "%08lX.gpx", static_cast<unsigned long>(member_id));
    std::string path = tracks_dir + "/" + name;

    SdRuntimeFile f;
    if (!f.open(path.c_str(), "a+"))
    {
        return false;
    }
    if (f.size() == 0)
    {
        f.print(kGpxHeader);
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
        const std::string time_str = ts >= kMinValidEpoch ? iso_time(static_cast<time_t>(ts)) : "";
        ::platform::esp::arduino_common::gps::SdGpxOutput output(f);
        if (!::gps::gpx::writeTrackPoint(output, lat, lon, time_str, 0, 7)) return false;
    }
    f.flush();
    f.close();
    return true;
}

} // namespace ui
} // namespace team
