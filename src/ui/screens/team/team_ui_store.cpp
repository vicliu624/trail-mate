/**
 * @file team_ui_store.cpp
 * @brief Team UI snapshot store (SD-only, persist.md format)
 */

#include "team_ui_store.h"

#include <Arduino.h>
#include <SD.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace team
{
namespace ui
{

bool TeamUiStoreStub::has_snapshot_ = false;
TeamUiSnapshot TeamUiStoreStub::snapshot_{};

namespace
{
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

constexpr uint16_t kSnapshotVersion = 1;
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

uint32_t now_secs()
{
    return static_cast<uint32_t>(millis() / 1000);
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
    if (SD.exists(path))
    {
        return true;
    }
    return SD.mkdir(path);
}

bool read_current_dir(std::string& out_dir)
{
    if (SD.cardType() == CARD_NONE || !SD.exists(kCurrentPath))
    {
        return false;
    }
    File f = SD.open(kCurrentPath, FILE_READ);
    if (!f)
    {
        return false;
    }
    String line = f.readStringUntil('\n');
    f.close();
    line.trim();
    if (line.length() == 0)
    {
        return false;
    }
    out_dir = std::string(line.c_str());
    return true;
}

bool write_current_dir(const std::string& dir)
{
    if (SD.cardType() == CARD_NONE)
    {
        return false;
    }
    if (!ensure_dir(kBaseDir))
    {
        return false;
    }
    File f = SD.open(kCurrentTmpPath, FILE_WRITE);
    if (!f)
    {
        return false;
    }
    f.print(dir.c_str());
    f.print("\n");
    f.flush();
    f.close();
    if (SD.exists(kCurrentPath))
    {
        SD.remove(kCurrentPath);
    }
    return SD.rename(kCurrentTmpPath, kCurrentPath);
}

bool clear_current_dir()
{
    if (SD.cardType() == CARD_NONE)
    {
        return false;
    }
    if (SD.exists(kCurrentPath))
    {
        SD.remove(kCurrentPath);
    }
    if (SD.exists(kCurrentTmpPath))
    {
        SD.remove(kCurrentTmpPath);
    }
    return true;
}

bool ensure_team_dir_for_id(const TeamId& id, std::string& out_dir_path)
{
    if (SD.cardType() == CARD_NONE)
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
    write_current_dir(dir);
    return true;
}

void write_u8(File& f, uint8_t v)
{
    f.write(&v, 1);
}

void write_u16(File& f, uint16_t v)
{
    uint8_t b[2] = {
        static_cast<uint8_t>(v & 0xFF),
        static_cast<uint8_t>((v >> 8) & 0xFF)};
    f.write(b, 2);
}

void write_u32(File& f, uint32_t v)
{
    uint8_t b[4];
    for (int i = 0; i < 4; ++i)
    {
        b[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
    }
    f.write(b, 4);
}

void write_u64(File& f, uint64_t v)
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
        uint8_t role = 0;
        if (!read_u32(payload, off, member_id) ||
            !read_u8(payload, off, role))
        {
            return;
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
    if (!SD.exists(snapshot_path.c_str()))
    {
        return false;
    }
    File f = SD.open(snapshot_path.c_str(), FILE_READ);
    if (!f)
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

    size_t off = 0;
    if (off + 4 > buf.size())
    {
        return false;
    }
    if (std::memcmp(buf.data(), "TMS1", 4) != 0)
    {
        return false;
    }
    off += 4;

    uint8_t version = 0;
    uint8_t flags = 0;
    uint16_t reserved = 0;
    uint32_t updated_ts = 0;
    uint64_t team_id = 0;
    uint32_t epoch = 0;
    uint32_t last_event_seq = 0;
    uint32_t self_node_id = 0;
    uint32_t leader_node_id = 0;
    uint8_t self_role = 0;
    uint16_t member_count = 0;
    uint16_t reserved3 = 0;

    if (!read_u8(buf, off, version) ||
        !read_u8(buf, off, flags) ||
        !read_u16(buf, off, reserved) ||
        !read_u32(buf, off, updated_ts) ||
        !read_u64(buf, off, team_id) ||
        !read_u32(buf, off, epoch) ||
        !read_u32(buf, off, last_event_seq) ||
        !read_u32(buf, off, self_node_id) ||
        !read_u32(buf, off, leader_node_id) ||
        !read_u8(buf, off, self_role))
    {
        return false;
    }
    if (off + 3 > buf.size())
    {
        return false;
    }
    off += 3;
    if (!read_u16(buf, off, member_count) ||
        !read_u16(buf, off, reserved3))
    {
        return false;
    }

    if (version != kSnapshotVersion)
    {
        return false;
    }

    out.in_team = (flags & 0x01) != 0;
    out.has_team_id = (team_id != 0);
    out.team_id = team_id_from_u64(team_id);
    out.security_round = epoch;
    out.last_event_seq = last_event_seq;
    out.self_is_leader = (self_role == kRoleLeader);
    out.members.clear();

    for (uint16_t i = 0; i < member_count; ++i)
    {
        uint32_t node_id = 0;
        uint8_t role = 0;
        uint8_t mflags = 0;
        uint16_t name_len = 0;
        if (!read_u32(buf, off, node_id) ||
            !read_u8(buf, off, role) ||
            !read_u8(buf, off, mflags) ||
            !read_u16(buf, off, name_len))
        {
            return false;
        }
        if (off + name_len > buf.size())
        {
            return false;
        }
        std::string name;
        if (name_len > 0)
        {
            name.assign(reinterpret_cast<const char*>(buf.data() + off), name_len);
        }
        off += name_len;

        TeamMemberUi m;
        m.node_id = node_id;
        m.name = name;
        m.leader = (role == kRoleLeader);
        out.members.push_back(m);
    }

    (void)updated_ts;
    (void)self_node_id;
    (void)leader_node_id;
    return true;
}

bool save_snapshot_to_path(const std::string& dir_path, const TeamUiSnapshot& in)
{
    if (SD.cardType() == CARD_NONE)
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

    File f = SD.open(tmp_path.c_str(), FILE_WRITE);
    if (!f)
    {
        return false;
    }

    f.write(reinterpret_cast<const uint8_t*>("TMS1"), 4);
    write_u8(f, kSnapshotVersion);
    uint8_t flags = in.in_team ? 0x01 : 0x00;
    write_u8(f, flags);
    write_u16(f, 0);
    write_u32(f, now_secs());
    write_u64(f, in.has_team_id ? team_id_to_u64(in.team_id) : 0);
    write_u32(f, in.security_round);
    write_u32(f, in.last_event_seq);
    write_u32(f, 0);
    uint32_t leader_node_id = 0;
    for (const auto& m : in.members)
    {
        if (m.leader)
        {
            leader_node_id = m.node_id;
            break;
        }
    }
    write_u32(f, leader_node_id);
    write_u8(f, in.self_is_leader ? kRoleLeader : (in.in_team ? kRoleMember : kRoleNone));
    write_u8(f, 0);
    write_u8(f, 0);
    write_u8(f, 0);
    uint16_t member_count = static_cast<uint16_t>(in.members.size());
    write_u16(f, member_count);
    write_u16(f, 0);

    for (const auto& m : in.members)
    {
        write_u32(f, m.node_id);
        write_u8(f, m.leader ? kRoleLeader : kRoleMember);
        uint16_t name_len = 0;
        uint8_t name_flag = 0;
        std::string name = m.name;
        if (!name.empty())
        {
            if (name.size() > 24)
            {
                name.resize(24);
            }
            name_len = static_cast<uint16_t>(name.size());
            name_flag = 0x01;
        }
        write_u8(f, name_flag);
        write_u16(f, name_len);
        if (name_len > 0)
        {
            f.write(reinterpret_cast<const uint8_t*>(name.data()), name_len);
        }
    }

    f.flush();
    f.close();

    std::string out_path_c = out_path;
    if (SD.exists(out_path_c.c_str()))
    {
        SD.remove(out_path_c.c_str());
    }
    return SD.rename(tmp_path.c_str(), out_path_c.c_str());
}

bool load_keys_from_path(const std::string& keys_path, TeamUiSnapshot& out)
{
    if (!SD.exists(keys_path.c_str()))
    {
        return false;
    }
    File f = SD.open(keys_path.c_str(), FILE_READ);
    if (!f)
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
    File f = SD.open(tmp_path.c_str(), FILE_WRITE);
    if (!f)
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
    if (SD.exists(out_path.c_str()))
    {
        SD.remove(out_path.c_str());
    }
    return SD.rename(tmp_path.c_str(), out_path.c_str());
}

bool load_events_apply(const std::string& events_path, TeamUiSnapshot& out)
{
    if (!SD.exists(events_path.c_str()))
    {
        return false;
    }
    File f = SD.open(events_path.c_str(), FILE_READ);
    if (!f)
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
    if (SD.cardType() == CARD_NONE)
    {
        return false;
    }
    std::string dir_path;
    if (!ensure_team_dir_for_id(team_id, dir_path))
    {
        return false;
    }

    std::string events_path = dir_path + "/" + kEventsName;
    File f = SD.open(events_path.c_str(), FILE_APPEND);
    if (!f)
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

bool init_posring(File& f)
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

bool read_posring_header(File& f, uint32_t& write_offset)
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

bool write_posring_header(File& f, uint32_t write_offset)
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

class TeamUiStorePersisted : public ITeamUiStore
{
  public:
    bool load(TeamUiSnapshot& out) override
    {
        if (SD.cardType() == CARD_NONE)
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
        snap.has_join_target = false;
        if (!snap.has_team_psk)
        {
            std::fill(snap.team_psk.begin(), snap.team_psk.end(), 0);
        }

        out = snap;
        return true;
    }

    void save(const TeamUiSnapshot& in) override
    {
        if (SD.cardType() == CARD_NONE)
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
                           (in.has_team_psk != last_snapshot_has_psk_);
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
        }
    }

    void clear() override
    {
        clear_current_dir();
    }

  private:
    uint32_t last_snapshot_ts_ = 0;
    uint32_t last_snapshot_seq_ = 0;
    bool last_snapshot_in_team_ = false;
    bool last_snapshot_self_is_leader_ = false;
    uint32_t last_snapshot_epoch_ = 0;
    bool last_snapshot_has_psk_ = false;
};

TeamUiStorePersisted s_persisted_store;
} // namespace

bool TeamUiStoreStub::load(TeamUiSnapshot& out)
{
    if (!has_snapshot_)
    {
        return false;
    }
    out = snapshot_;
    return true;
}

void TeamUiStoreStub::save(const TeamUiSnapshot& in)
{
    snapshot_ = in;
    has_snapshot_ = true;
}

void TeamUiStoreStub::clear()
{
    has_snapshot_ = false;
}

namespace
{
TeamUiStoreStub s_stub_store;
ITeamUiStore* s_store = &s_persisted_store;
} // namespace

ITeamUiStore& team_ui_get_store()
{
    return *s_store;
}

void team_ui_set_store(ITeamUiStore* store)
{
    if (store)
    {
        s_store = store;
    }
    else
    {
        s_store = &s_stub_store;
    }
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
    File f = SD.open(path.c_str(), FILE_READ);
    bool exists = f;
    if (f)
    {
        f.close();
    }
    File rw = SD.open(path.c_str(), FILE_WRITE);
    if (!rw)
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
    if (SD.cardType() == CARD_NONE)
    {
        return false;
    }
    std::string dir = team_dir_from_id(team_id);
    std::string dir_path = std::string(kBaseDir) + "/" + dir;
    std::string path = dir_path + "/" + kPosringName;
    if (!SD.exists(path.c_str()))
    {
        return false;
    }
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f)
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
    std::vector<uint8_t> payload;
    payload.assign(text.begin(), text.end());
    return team_ui_chatlog_append_structured(team_id,
                                             peer_id,
                                             incoming,
                                             ts,
                                             team::proto::TeamChatType::Text,
                                             payload);
}

bool team_ui_chatlog_append_structured(const TeamId& team_id,
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
    if (SD.exists(path.c_str()))
    {
        File f = SD.open(path.c_str(), FILE_READ);
        if (f)
        {
            size_t size = f.size();
            f.close();
            if (size + record_len > kChatlogMaxBytes)
            {
                std::string old_path = dir_path + "/" + kChatlogOldName;
                if (SD.exists(old_path.c_str()))
                {
                    SD.remove(old_path.c_str());
                }
                SD.rename(path.c_str(), old_path.c_str());
            }
        }
    }

    File out = SD.open(path.c_str(), FILE_APPEND);
    if (!out)
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

bool team_ui_chatlog_load_recent(const TeamId& team_id,
                                 size_t max_count,
                                 std::vector<TeamChatLogEntry>& out)
{
    out.clear();
    if (SD.cardType() == CARD_NONE)
    {
        return false;
    }
    std::string dir_path;
    if (!ensure_team_dir_for_id(team_id, dir_path))
    {
        return false;
    }
    std::string path = dir_path + "/" + kChatlogName;
    if (!SD.exists(path.c_str()))
    {
        return false;
    }
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f)
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

bool team_ui_save_keys_now(const TeamId& team_id,
                           uint32_t key_id,
                           const std::array<uint8_t, team::proto::kTeamChannelPskSize>& psk)
{
    if (SD.cardType() == CARD_NONE)
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

} // namespace ui
} // namespace team
