#include "platform/linux/team_snapshot_store.h"
#include "ui/team_persistence/team_ui_snapshot_codec.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <unistd.h>

namespace platform::linux_runtime
{
TeamSnapshotStore::TeamSnapshotStore(std::filesystem::path path) : path_(std::move(path)) {}

bool TeamSnapshotStore::load(team::ui::TeamUiSnapshot& out)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path_, error);
    if (error || size < 23 || size > 65536) return false;
    std::ifstream file(path_, std::ios::binary);
    std::vector<uint8_t> data{std::istreambuf_iterator<char>(file), {}};
    if (data.size() != size) return false;
    const bool extended = !std::memcmp(data.data(), "LTS2", 4);
    if ((!extended && std::memcmp(data.data(), "LTS1", 4)) || (extended && size < 27)) return false;
    const size_t name_offset = extended ? 26 : 22;
    const size_t name_size = data[name_offset - 1];
    const size_t header_size = name_offset + name_size;
    if (header_size >= data.size()) return false;
    team::ui::TeamUiSnapshot value;
    if (!::ui::team_persistence::decodeTeamUiSnapshot(data.data() + header_size, data.size() - header_size, value)) return false;
    value.has_team_psk = (data[4] & 1) != 0;
    value.pending_join = extended && (data[4] & 2) != 0;
    value.kicked_out = extended && (data[4] & 4) != 0;
    if (extended)
    {
        for (unsigned i = 0; i < 4; ++i)
            value.pending_join_started_s |= static_cast<uint32_t>(data[21 + i]) << (8 * i);
    }
    std::copy_n(data.begin() + 5, 16, value.team_psk.begin());
    value.team_name.assign(reinterpret_cast<const char*>(data.data() + name_offset), name_size);
    out = std::move(value);
    return true;
}

void TeamSnapshotStore::save(const team::ui::TeamUiSnapshot& value)
{
    std::vector<uint8_t> snapshot;
    if (!::ui::team_persistence::encodeTeamUiSnapshot(value, value.last_update_s, snapshot)) return;
    const size_t name_size = std::min<size_t>(value.team_name.size(), 255);
    const uint8_t flags = (value.has_team_psk ? 1 : 0) | (value.pending_join ? 2 : 0) | (value.kicked_out ? 4 : 0);
    std::vector<uint8_t> data{'L', 'T', 'S', '2', flags};
    data.insert(data.end(), value.team_psk.begin(), value.team_psk.end());
    for (unsigned i = 0; i < 4; ++i)
        data.push_back(static_cast<uint8_t>(value.pending_join_started_s >> (8 * i)));
    data.push_back(static_cast<uint8_t>(name_size));
    data.insert(data.end(), value.team_name.begin(), value.team_name.begin() + name_size);
    data.insert(data.end(), snapshot.begin(), snapshot.end());
    if (data.size() > 65536) return;
    std::error_code error;
    std::filesystem::create_directories(path_.parent_path(), error);
    if (error) return;
    auto temp = path_;
    temp += ".new";
    const int file = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (file < 0) return;
    bool ok = ::fchmod(file, 0600) == 0;
    size_t written = 0;
    while (ok && written < data.size())
    {
        const auto size = ::write(file, data.data() + written, data.size() - written);
        if (size < 0 && errno == EINTR) continue;
        if (size <= 0)
        {
            ok = false;
            break;
        }
        written += static_cast<size_t>(size);
    }
    ok = ok && ::fsync(file) == 0;
    ::close(file);
    if (ok) ok = ::rename(temp.c_str(), path_.c_str()) == 0;
    if (!ok)
    {
        ::unlink(temp.c_str());
        std::fputs("[Team] Snapshot persistence failed\n", stderr);
        return;
    }
    const int directory = ::open(path_.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory >= 0)
    {
        ::fsync(directory);
        ::close(directory);
    }
}

void TeamSnapshotStore::clear()
{
    ::unlink(path_.c_str());
}
} // namespace platform::linux_runtime
