#include "ui/screens/team/team_ui_store.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <sys/stat.h>
#include <sys/types.h>

#include "platform/esp/idf_common/bsp_runtime.h"
#include "platform/ui/settings_store.h"

namespace team::ui
{

bool TeamUiMemoryStore::has_snapshot_ = false;
TeamUiSnapshot TeamUiMemoryStore::snapshot_{};

namespace
{

constexpr const char* kStoreNs = "team_ui";
constexpr const char* kSnapshotKey = "snapshot";
constexpr size_t kMaxChatEntries = 128;
constexpr size_t kMaxPosSamples = 256;
constexpr uint16_t kSnapshotVersion = 1;
constexpr uint32_t kMinValidEpoch = 1577836800U;

class Writer
{
  public:
    void u8(uint8_t value) { data_.push_back(value); }
    void u32(uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            data_.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
        }
    }
    void boolean(bool value) { u8(value ? 1 : 0); }
    void bytes(const uint8_t* ptr, std::size_t len)
    {
        u32(static_cast<uint32_t>(len));
        if (ptr && len > 0)
        {
            data_.insert(data_.end(), ptr, ptr + len);
        }
    }
    void text(const std::string& value)
    {
        bytes(reinterpret_cast<const uint8_t*>(value.data()), value.size());
    }
    const std::vector<uint8_t>& data() const { return data_; }

  private:
    std::vector<uint8_t> data_;
};

class Reader
{
  public:
    explicit Reader(const std::vector<uint8_t>& data) : data_(data) {}

    bool u8(uint8_t& value)
    {
        if (offset_ + 1 > data_.size())
        {
            return false;
        }
        value = data_[offset_++];
        return true;
    }
    bool u32(uint32_t& value)
    {
        uint8_t b0 = 0, b1 = 0, b2 = 0, b3 = 0;
        if (u8(b0) == false || u8(b1) == false || u8(b2) == false || u8(b3) == false)
        {
            return false;
        }
        value = static_cast<uint32_t>(b0) |
                (static_cast<uint32_t>(b1) << 8) |
                (static_cast<uint32_t>(b2) << 16) |
                (static_cast<uint32_t>(b3) << 24);
        return true;
    }
    bool boolean(bool& value)
    {
        uint8_t raw = 0;
        if (u8(raw) == false)
        {
            return false;
        }
        value = raw != 0;
        return true;
    }
    bool bytes(std::vector<uint8_t>& out)
    {
        uint32_t len = 0;
        if (u32(len) == false || offset_ + len > data_.size())
        {
            return false;
        }
        out.assign(data_.begin() + static_cast<std::ptrdiff_t>(offset_),
                   data_.begin() + static_cast<std::ptrdiff_t>(offset_ + len));
        offset_ += len;
        return true;
    }
    bool text(std::string& out)
    {
        std::vector<uint8_t> raw;
        if (bytes(raw) == false)
        {
            return false;
        }
        out.assign(raw.begin(), raw.end());
        return true;
    }

  private:
    const std::vector<uint8_t>& data_;
    std::size_t offset_ = 0;
};

class TeamUiStorePersisted : public ITeamUiStore
{
  public:
    bool load(TeamUiSnapshot& out) override;
    void save(const TeamUiSnapshot& in) override;
    void clear() override;
};

TeamUiStorePersisted s_persisted_store;
TeamUiMemoryStore s_memory_store;
ITeamUiStore* s_store = &s_persisted_store;
std::mutex s_mutex;
bool s_snapshot_loaded = false;
bool s_has_snapshot = false;
TeamUiSnapshot s_snapshot{};
std::vector<TeamChatLogEntry> s_chatlog;
std::vector<TeamPosSample> s_posring;
std::map<uint32_t, std::string> s_track_paths;

void trim_chatlog()
{
    if (s_chatlog.size() > kMaxChatEntries)
    {
        s_chatlog.erase(s_chatlog.begin(), s_chatlog.begin() + static_cast<std::ptrdiff_t>(s_chatlog.size() - kMaxChatEntries));
    }
}

void trim_posring()
{
    if (s_posring.size() > kMaxPosSamples)
    {
        s_posring.erase(s_posring.begin(), s_posring.begin() + static_cast<std::ptrdiff_t>(s_posring.size() - kMaxPosSamples));
    }
}

void serialize_snapshot(const TeamUiSnapshot& snapshot, Writer& writer)
{
    writer.u8(static_cast<uint8_t>(kSnapshotVersion & 0xFF));
    writer.u8(static_cast<uint8_t>((kSnapshotVersion >> 8) & 0xFF));
    writer.boolean(snapshot.in_team);
    writer.boolean(snapshot.pending_join);
    writer.u32(snapshot.pending_join_started_s);
    writer.boolean(snapshot.kicked_out);
    writer.boolean(snapshot.self_is_leader);
    writer.u32(snapshot.last_event_seq);
    writer.u32(snapshot.team_chat_unread);
    writer.boolean(snapshot.has_team_id);
    writer.bytes(snapshot.team_id.data(), snapshot.team_id.size());
    writer.text(snapshot.team_name);
    writer.u32(snapshot.security_round);
    writer.u32(snapshot.last_update_s);
    writer.boolean(snapshot.has_team_psk);
    writer.bytes(snapshot.team_psk.data(), snapshot.team_psk.size());
    writer.u32(static_cast<uint32_t>(snapshot.members.size()));
    for (const TeamMemberUi& member : snapshot.members)
    {
        writer.u32(member.node_id);
        writer.text(member.name);
        writer.boolean(member.online);
        writer.boolean(member.leader);
        writer.u32(member.last_seen_s);
        writer.u8(member.color_index);
    }
}

bool deserialize_snapshot(const std::vector<uint8_t>& blob, TeamUiSnapshot& out)
{
    Reader reader(blob);
    uint8_t ver_lo = 0;
    uint8_t ver_hi = 0;
    if (reader.u8(ver_lo) == false || reader.u8(ver_hi) == false)
    {
        return false;
    }
    const uint16_t version = static_cast<uint16_t>(ver_lo | (static_cast<uint16_t>(ver_hi) << 8));
    if (version != kSnapshotVersion)
    {
        return false;
    }

    TeamUiSnapshot snapshot{};
    std::vector<uint8_t> team_id_bytes;
    std::vector<uint8_t> psk_bytes;
    uint32_t member_count = 0;
    if (reader.boolean(snapshot.in_team) == false ||
        reader.boolean(snapshot.pending_join) == false ||
        reader.u32(snapshot.pending_join_started_s) == false ||
        reader.boolean(snapshot.kicked_out) == false ||
        reader.boolean(snapshot.self_is_leader) == false ||
        reader.u32(snapshot.last_event_seq) == false ||
        reader.u32(snapshot.team_chat_unread) == false ||
        reader.boolean(snapshot.has_team_id) == false ||
        reader.bytes(team_id_bytes) == false ||
        reader.text(snapshot.team_name) == false ||
        reader.u32(snapshot.security_round) == false ||
        reader.u32(snapshot.last_update_s) == false ||
        reader.boolean(snapshot.has_team_psk) == false ||
        reader.bytes(psk_bytes) == false ||
        reader.u32(member_count) == false)
    {
        return false;
    }
    if (team_id_bytes.size() != snapshot.team_id.size() || psk_bytes.size() != snapshot.team_psk.size())
    {
        return false;
    }
    std::copy(team_id_bytes.begin(), team_id_bytes.end(), snapshot.team_id.begin());
    std::copy(psk_bytes.begin(), psk_bytes.end(), snapshot.team_psk.begin());
    snapshot.members.reserve(member_count);
    for (uint32_t i = 0; i < member_count; ++i)
    {
        TeamMemberUi member{};
        if (reader.u32(member.node_id) == false ||
            reader.text(member.name) == false ||
            reader.boolean(member.online) == false ||
            reader.boolean(member.leader) == false ||
            reader.u32(member.last_seen_s) == false ||
            reader.u8(member.color_index) == false)
        {
            return false;
        }
        snapshot.members.push_back(std::move(member));
    }

    out = std::move(snapshot);
    return true;
}

void ensure_snapshot_loaded_locked()
{
    if (s_snapshot_loaded)
    {
        return;
    }
    platform::esp::idf_common::bsp_runtime::ensure_nvs_ready();
    std::vector<uint8_t> blob;
    if (platform::ui::settings_store::get_blob(kStoreNs, kSnapshotKey, blob))
    {
        TeamUiSnapshot snapshot;
        if (deserialize_snapshot(blob, snapshot))
        {
            s_snapshot = std::move(snapshot);
            s_has_snapshot = true;
        }
    }
    s_snapshot_loaded = true;
}

void persist_snapshot_locked()
{
    Writer writer;
    serialize_snapshot(s_snapshot, writer);
    const std::vector<uint8_t>& blob = writer.data();
    if (blob.empty() == false)
    {
        (void)platform::ui::settings_store::put_blob(kStoreNs, kSnapshotKey, blob.data(), blob.size());
    }
}

bool ensure_dir(const std::string& path)
{
    struct stat st
    {
    };
    if (stat(path.c_str(), &st) == 0)
    {
        return (st.st_mode & S_IFDIR) != 0;
    }
    return mkdir(path.c_str(), 0777) == 0;
}

std::string team_hex(const TeamId& team_id)
{
    char buf[team_id.size() * 2 + 1] = {0};
    for (std::size_t i = 0; i < team_id.size(); ++i)
    {
        std::snprintf(buf + i * 2, 3, "%02X", team_id[i]);
    }
    return std::string(buf);
}

std::string iso_time(uint32_t ts)
{
    if (ts < kMinValidEpoch)
    {
        return std::string();
    }
    char buf[32] = {0};
    std::time_t value = static_cast<std::time_t>(ts);
    std::tm info{};
    if (gmtime_r(&value, &info) == nullptr)
    {
        return std::string();
    }
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &info);
    return std::string(buf);
}

} // namespace

bool TeamUiMemoryStore::load(TeamUiSnapshot& out)
{
    if (has_snapshot_ == false)
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
}

bool TeamUiStorePersisted::load(TeamUiSnapshot& out)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    ensure_snapshot_loaded_locked();
    if (s_has_snapshot == false)
    {
        return false;
    }
    out = s_snapshot;
    return true;
}

void TeamUiStorePersisted::save(const TeamUiSnapshot& in)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    ensure_snapshot_loaded_locked();
    s_snapshot = in;
    s_has_snapshot = true;
    persist_snapshot_locked();
}

void TeamUiStorePersisted::clear()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_snapshot = TeamUiSnapshot{};
    s_has_snapshot = false;
    s_chatlog.clear();
    s_posring.clear();
    s_track_paths.clear();
    const char* keys[] = {kSnapshotKey};
    platform::ui::settings_store::remove_keys(kStoreNs, keys, 1);
}

ITeamUiStore& team_ui_get_store()
{
    return s_store ? *s_store : s_memory_store;
}

void team_ui_set_store(ITeamUiStore* store)
{
    s_store = store ? store : &s_persisted_store;
}
bool team_ui_append_key_event(const TeamId& team_id,
                              TeamKeyEventType type,
                              uint32_t event_seq,
                              uint32_t ts,
                              const uint8_t* payload,
                              size_t len)
{
    (void)team_id;
    (void)type;
    (void)event_seq;
    (void)ts;
    (void)payload;
    (void)len;
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
    (void)team_id;
    std::lock_guard<std::mutex> lock(s_mutex);
    TeamPosSample sample{};
    sample.member_id = member_id;
    sample.lat_e7 = lat_e7;
    sample.lon_e7 = lon_e7;
    sample.alt_m = alt_m;
    sample.speed_dmps = speed_dmps;
    sample.ts = ts;
    s_posring.push_back(sample);
    trim_posring();
    return true;
}

bool team_ui_posring_load_latest(const TeamId& team_id,
                                 std::vector<TeamPosSample>& out)
{
    (void)team_id;
    std::lock_guard<std::mutex> lock(s_mutex);
    out = s_posring;
    return true;
}

bool team_ui_chatlog_append(const TeamId& team_id,
                            uint32_t peer_id,
                            bool incoming,
                            uint32_t ts,
                            const std::string& text)
{
    std::vector<uint8_t> payload(text.begin(), text.end());
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
    (void)team_id;
    std::lock_guard<std::mutex> lock(s_mutex);
    TeamChatLogEntry entry{};
    entry.incoming = incoming;
    entry.ts = ts;
    entry.peer_id = peer_id;
    entry.type = type;
    entry.payload = payload;
    s_chatlog.push_back(std::move(entry));
    trim_chatlog();
    return true;
}

bool team_ui_chatlog_load_recent(const TeamId& team_id,
                                 size_t max_count,
                                 std::vector<TeamChatLogEntry>& out)
{
    (void)team_id;
    std::lock_guard<std::mutex> lock(s_mutex);
    out.clear();
    if (s_chatlog.empty())
    {
        return true;
    }
    const size_t count = std::min(max_count, s_chatlog.size());
    out.insert(out.end(), s_chatlog.end() - static_cast<std::ptrdiff_t>(count), s_chatlog.end());
    return true;
}

bool team_ui_save_keys_now(const TeamId& team_id,
                           uint32_t key_id,
                           const std::array<uint8_t, team::proto::kTeamChannelPskSize>& psk)
{
    (void)key_id;
    TeamUiSnapshot snapshot{};
    if (team_ui_get_store().load(snapshot) == false)
    {
        snapshot = TeamUiSnapshot{};
    }
    snapshot.team_id = team_id;
    snapshot.has_team_id = true;
    snapshot.team_psk = psk;
    snapshot.has_team_psk = true;
    team_ui_get_store().save(snapshot);
    return true;
}
bool team_ui_append_member_track(const TeamId& team_id,
                                 uint32_t member_id,
                                 const team::proto::TeamTrackMessage& track)
{
    if (platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready() == false)
    {
        return false;
    }
    if (track.points.empty() || track.valid_mask == 0)
    {
        return false;
    }

    const std::string base_dir = std::string(platform::esp::idf_common::bsp_runtime::sdcard_mount_point()) + "/team";
    const std::string team_dir = base_dir + "/" + team_hex(team_id);
    if (ensure_dir(base_dir) == false || ensure_dir(team_dir) == false)
    {
        return false;
    }

    char name[32] = {0};
    std::snprintf(name, sizeof(name), "%08lX.gpx", static_cast<unsigned long>(member_id));
    const std::string path = team_dir + "/" + name;

    FILE* file = std::fopen(path.c_str(), "w");
    if (file == nullptr)
    {
        return false;
    }

    std::fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", file);
    std::fputs("<gpx version=\"1.1\" creator=\"Trail-Mate\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n", file);
    std::fputs("<trk><trkseg>\n", file);
    for (size_t i = 0; i < track.points.size(); ++i)
    {
        if ((track.valid_mask & (1u << static_cast<uint32_t>(i))) == 0)
        {
            continue;
        }
        const auto& point = track.points[i];
        const double lat = static_cast<double>(point.lat_e7) / 1e7;
        const double lon = static_cast<double>(point.lon_e7) / 1e7;
        const uint32_t ts = track.start_ts + static_cast<uint32_t>(track.interval_s) * static_cast<uint32_t>(i);
        std::fprintf(file, "<trkpt lat=\"%.7f\" lon=\"%.7f\">\n", lat, lon);
        const std::string time = iso_time(ts);
        if (time.empty() == false)
        {
            std::fprintf(file, "  <time>%s</time>\n", time.c_str());
        }
        std::fputs("</trkpt>\n", file);
    }
    std::fputs("</trkseg></trk></gpx>\n", file);
    std::fflush(file);
    std::fclose(file);

    std::lock_guard<std::mutex> lock(s_mutex);
    s_track_paths[member_id] = path;
    return true;
}

bool team_ui_get_member_track_path(const TeamId& team_id,
                                   uint32_t member_id,
                                   std::string& out_path)
{
    (void)team_id;
    std::lock_guard<std::mutex> lock(s_mutex);
    const auto it = s_track_paths.find(member_id);
    if (it == s_track_paths.end())
    {
        out_path.clear();
        return false;
    }
    out_path = it->second;
    return true;
}

} // namespace team::ui
