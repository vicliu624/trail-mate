#pragma once

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace platform::linux_runtime
{
// Only validated received services belong here. RF energy alone is not a station.
struct OneSegChannel
{
    unsigned channel = 0;
    unsigned service = 0;
    std::string name;
    bool sameService(const OneSegChannel& other) const
    {
        return channel == other.channel && service == other.service;
    }
};

// The caller owns threading. Load and replacement are transactional; a failed
// scan or failed write never replaces the last successfully saved channel list.
class OneSegChannelStore
{
  public:
    static constexpr size_t capacity = 1600; // 50 physical channels x 32 received services.
    explicit OneSegChannelStore(std::filesystem::path path) : path_(std::move(path)) {}
    const std::vector<OneSegChannel>& channels() const { return channels_; }
    const OneSegChannel* lastChannel() const
    {
        const auto found = std::find_if(channels_.begin(), channels_.end(), [this](const auto& entry)
                                        { return entry.sameService(last_); });
        return found == channels_.end() ? nullptr : &*found;
    }
    bool load(std::string& error)
    {
        error.clear();
        std::error_code ec;
        const bool exists = std::filesystem::exists(path_, ec);
        if (ec) return fail(error, "Cannot inspect saved TV channels");
        if (!exists) return true;
        const auto bytes = std::filesystem::file_size(path_, ec);
        if (ec || bytes > 512 * 1024) return fail(error, "Invalid TV channel file size");
        std::ifstream input(path_);
        std::string header;
        OneSegChannel last;
        size_t count = 0;
        if (!(input >> header >> last.channel >> last.service >> count) || header != "TRAILMATE_ONESEG_V1" || count > capacity)
            return fail(error, "Invalid saved TV channel header");
        std::vector<OneSegChannel> entries;
        for (size_t i = 0; i < count; ++i)
        {
            OneSegChannel entry;
            if (!(input >> entry.channel >> entry.service >> std::quoted(entry.name)))
                return fail(error, "Incomplete saved TV channel list");
            entries.push_back(std::move(entry));
        }
        input >> std::ws;
        if (!input.eof() || !valid(entries, last)) return fail(error, "Invalid saved TV channels");
        channels_ = std::move(entries);
        last_ = std::move(last);
        return true;
    }
    bool replaceScan(const std::vector<OneSegChannel>& received, std::string& error)
    {
        error.clear();
        // Empty searches do not erase a list from a different reception area.
        if (received.empty()) return fail(error, "No television services found; previous channels retained");
        OneSegChannel last = received.front();
        for (const auto& entry : received)
            if (entry.sameService(last_)) last = entry;
        if (!valid(received, last)) return fail(error, "Invalid or duplicate television services");
        if (!write(received, last, error)) return false;
        channels_ = received;
        last_ = std::move(last);
        return true;
    }
    bool select(unsigned channel, unsigned service, std::string& error)
    {
        error.clear();
        const OneSegChannel key{channel, service, {}};
        const auto found = std::find_if(channels_.begin(), channels_.end(), [&key](const auto& entry)
                                        { return entry.sameService(key); });
        if (found == channels_.end()) return fail(error, "Television service is not saved");
        if (!write(channels_, *found, error)) return false;
        last_ = *found;
        return true;
    }
    // Call after verifying received programme mapping and current decoded media.
    // This also supports saving a manually tuned station before the first scan.
    bool rememberReceived(const OneSegChannel& received, std::string& error)
    {
        error.clear();
        auto entries = channels_;
        auto found = std::find_if(entries.begin(), entries.end(), [&received](const auto& entry)
                                  { return entry.sameService(received); });
        if (found == entries.end()) entries.push_back(received);
        else if (!received.name.empty()) *found = received;
        if (!valid(entries, received)) return fail(error, "Invalid received television service");
        if (!write(entries, received, error)) return false;
        channels_ = std::move(entries);
        last_ = received;
        return true;
    }

  private:
    std::filesystem::path path_;
    std::vector<OneSegChannel> channels_;
    OneSegChannel last_;
    static bool fail(std::string& error, const char* message)
    {
        error = message;
        return false;
    }
    static bool valid(const std::vector<OneSegChannel>& entries, const OneSegChannel& last)
    {
        if (entries.empty() || entries.size() > capacity) return false;
        bool contains_last = false;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const auto& entry = entries[i];
            if (entry.channel < 13 || entry.channel > 62 || !entry.service || entry.service > 65535 || entry.name.size() > 192)
                return false;
            for (unsigned char c : entry.name)
                if (c < 32 || c == 127) return false;
            for (size_t j = 0; j < i; ++j)
                if (entry.sameService(entries[j])) return false;
            contains_last |= entry.sameService(last);
        }
        return contains_last;
    }
    bool write(const std::vector<OneSegChannel>& entries, const OneSegChannel& last, std::string& error)
    {
        std::error_code ec;
        const auto parent = path_.parent_path().empty() ? std::filesystem::path(".") : path_.parent_path();
        std::filesystem::create_directories(parent, ec);
        if (ec) return fail(error, "Cannot create TV settings directory");
        std::ostringstream content;
        content << "TRAILMATE_ONESEG_V1 " << last.channel << ' ' << last.service << ' ' << entries.size() << '\n';
        for (const auto& entry : entries)
            content << entry.channel << ' ' << entry.service << ' ' << std::quoted(entry.name) << '\n';
        const auto bytes = content.str();
        auto temporary = path_.string() + ".tmp.XXXXXX";
        const int fd = ::mkstemp(temporary.data());
        if (fd < 0) return fail(error, "Cannot create temporary TV channel file");
        size_t offset = 0;
        bool ok = true;
        while (offset < bytes.size())
        {
            const auto n = ::write(fd, bytes.data() + offset, bytes.size() - offset);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0)
            {
                ok = false;
                break;
            }
            offset += static_cast<size_t>(n);
        }
        if (ok && ::fsync(fd)) ok = false;
        if (::close(fd)) ok = false;
        if (ok && ::rename(temporary.c_str(), path_.c_str())) ok = false;
        if (!ok)
        {
            ::unlink(temporary.c_str());
            return fail(error, "Cannot save TV channels; previous channels retained");
        }
        // Rename is atomic. Sync the parent where supported for power-loss safety.
        const int directory = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
        if (directory >= 0)
        {
            ::fsync(directory);
            ::close(directory);
        }
        return true;
    }
};
} // namespace platform::linux_runtime
