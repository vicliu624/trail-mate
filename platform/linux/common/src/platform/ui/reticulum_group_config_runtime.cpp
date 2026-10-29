#include "platform/ui/reticulum_group_config_runtime.h"

#include "platform/linux/runtime_paths.h"
#include "platform/linux/storage_monitor.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace platform::ui::reticulum_groups
{
namespace
{

constexpr const char* kRelativePath = "trailmate/reticulum/groups.tsv";
constexpr const char* kLogicalPath = "/trailmate/reticulum/groups.tsv";
constexpr std::size_t kMaxConfigBytes = 2048;
chat::ReticulumGroupDestinationConfig
    s_pending_groups[chat::kReticulumGroupDestinationMaxCount] = {};
bool s_pending = false;

void copy_text(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

void set_status(Status& out, const char* message, const char* detail = nullptr)
{
    copy_text(out.message, sizeof(out.message), message);
    copy_text(out.detail, sizeof(out.detail), detail);
}

std::filesystem::path config_file_path()
{
    return ::platform::linux_runtime::resolve_paths().sd_root / kRelativePath;
}

bool enabled_text(const std::string& value)
{
    return value == "1" || value == "true" || value == "yes" || value == "enabled";
}

std::string trim_line(std::string line)
{
    while (!line.empty() &&
           (line.back() == '\r' || line.back() == '\n' ||
            line.back() == ' ' || line.back() == '\t'))
    {
        line.pop_back();
    }
    std::size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
    {
        ++start;
    }
    return start == 0 ? line : line.substr(start);
}

bool parse_group_line(const std::string& line,
                      chat::ReticulumGroupDestinationConfig& out,
                      char* error,
                      std::size_t error_len)
{
    const std::size_t first_tab = line.find('\t');
    const std::size_t second_tab =
        first_tab == std::string::npos ? std::string::npos : line.find('\t', first_tab + 1);
    if (first_tab == std::string::npos || second_tab == std::string::npos)
    {
        copy_text(error, error_len, "Invalid group config line");
        return false;
    }

    const std::string destination = line.substr(second_tab + 1);

    out = chat::ReticulumGroupDestinationConfig{};
    if (!chat::parseReticulumDestinationHashText(destination.c_str(),
                                                 &out.identity,
                                                 error,
                                                 error_len))
    {
        return false;
    }
    out.enabled = enabled_text(line.substr(0, first_tab));
    std::snprintf(out.name,
                  sizeof(out.name),
                  "%s",
                  line.substr(first_tab + 1, second_tab - first_tab - 1).c_str());
    return true;
}

bool read_config_text(const std::filesystem::path& path, std::string& out)
{
    out.clear();
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0 || size > kMaxConfigBytes)
    {
        return false;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
    {
        return false;
    }
    out.resize(static_cast<std::size_t>(size));
    stream.read(&out[0], static_cast<std::streamsize>(out.size()));
    return stream.good() || stream.gcount() == static_cast<std::streamsize>(out.size());
}

} // namespace

const char* config_path()
{
    return kLogicalPath;
}

void clear(chat::ReticulumGroupDestinationConfig* groups, std::size_t group_count)
{
    if (!groups)
    {
        return;
    }
    for (std::size_t index = 0; index < group_count; ++index)
    {
        groups[index] = chat::ReticulumGroupDestinationConfig{};
    }
}

Status load(chat::ReticulumGroupDestinationConfig* groups, std::size_t group_count)
{
    Status out{};
    out.supported = true;
    // This is a file-I/O operation, including startup migration. Its readiness
    // must not depend on whether the UI monitor has published its first sample.
    out.sd_present = ::platform::linux_runtime::probe_storage_root(
                         ::platform::linux_runtime::resolve_paths().sd_root)
                         .available;
    if (!groups || group_count == 0)
    {
        set_status(out, "Group storage unavailable", kLogicalPath);
        return out;
    }

    clear(groups, group_count);
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kLogicalPath);
        return out;
    }

    const auto path = config_file_path();
    std::error_code ec;
    out.file_present = std::filesystem::exists(path, ec) && !ec;
    if (!out.file_present)
    {
        out.loaded = true;
        set_status(out, "No Reticulum groups", kLogicalPath);
        return out;
    }

    std::string text;
    if (!read_config_text(path, text))
    {
        set_status(out, "Cannot read Reticulum groups", path.string().c_str());
        return out;
    }

    std::size_t slot = 0;
    std::size_t line_start = 0;
    while (line_start <= text.size() && slot < group_count)
    {
        const std::size_t line_end = text.find('\n', line_start);
        std::string line = trim_line(text.substr(
            line_start,
            line_end == std::string::npos ? std::string::npos : line_end - line_start));
        if (!line.empty() && line[0] != '#' && line.rfind("version\t", 0) != 0)
        {
            char error[96] = {};
            chat::ReticulumGroupDestinationConfig parsed{};
            if (parse_group_line(line, parsed, error, sizeof(error)))
            {
                groups[slot++] = parsed;
            }
            else
            {
                std::printf("[RTGroupConfig] skip invalid line: %s\n", error);
            }
        }
        if (line_end == std::string::npos)
        {
            break;
        }
        line_start = line_end + 1;
    }

    out.loaded = true;
    set_status(out, slot == 0 ? "No Reticulum groups" : "Reticulum groups loaded", kLogicalPath);
    return out;
}

Status submit(const chat::ReticulumGroupDestinationConfig* groups,
              std::size_t group_count)
{
    Status out{};
    out.supported = true;
    if (!groups || group_count == 0 ||
        group_count > chat::kReticulumGroupDestinationMaxCount)
    {
        set_status(out, "Group storage unavailable", kLogicalPath);
        return out;
    }
    std::memcpy(s_pending_groups,
                groups,
                group_count * sizeof(chat::ReticulumGroupDestinationConfig));
    for (std::size_t index = group_count;
         index < chat::kReticulumGroupDestinationMaxCount;
         ++index)
    {
        s_pending_groups[index] = chat::ReticulumGroupDestinationConfig{};
    }
    s_pending = true;
    out.queued = true;
    set_status(out, "Reticulum groups save queued", kLogicalPath);
    return out;
}

Status flushPending()
{
    if (!s_pending)
    {
        Status out{};
        out.supported = true;
        out.saved = true;
        set_status(out, "Reticulum groups idle", kLogicalPath);
        return out;
    }

    const Status out = save(s_pending_groups,
                            chat::kReticulumGroupDestinationMaxCount);
    if (out.saved)
    {
        s_pending = false;
    }
    return out;
}

bool hasPending()
{
    return s_pending;
}

Status save(const chat::ReticulumGroupDestinationConfig* groups, std::size_t group_count)
{
    Status out{};
    out.supported = true;
    out.sd_present = ::platform::linux_runtime::probe_storage_root(
                         ::platform::linux_runtime::resolve_paths().sd_root)
                         .available;
    if (!groups || group_count == 0)
    {
        set_status(out, "Group storage unavailable", kLogicalPath);
        return out;
    }
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kLogicalPath);
        return out;
    }

    std::string text;
    text.reserve(384);
    text += "# Trail Mate Reticulum groups\n";
    text += "version\t1\n";
    for (std::size_t index = 0; index < group_count; ++index)
    {
        const auto& group = groups[index];
        if (!chat::hasReticulumDestinationIdentity(group.identity))
        {
            continue;
        }
        char hash[chat::kReticulumPeerHashSize * 2 + 1] = {};
        chat::formatReticulumDestinationHashText(group.identity, hash, sizeof(hash));
        text += group.enabled ? "1\t" : "0\t";
        text += group.name[0] != '\0' ? group.name : "Group";
        text += "\t";
        text += hash;
        text += "\n";
    }

    const auto paths = ::platform::linux_runtime::resolve_paths();
    if (!::platform::linux_runtime::ensure_directory(paths.sd_root / "trailmate" / "reticulum") ||
        !::platform::linux_runtime::safe_write_under_root(paths.sd_root, kRelativePath, text))
    {
        set_status(out, "Cannot write Reticulum groups", kLogicalPath);
        return out;
    }

    out.file_present = true;
    out.saved = true;
    set_status(out, "Reticulum groups saved", kLogicalPath);
    return out;
}

} // namespace platform::ui::reticulum_groups
