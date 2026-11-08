#include "platform/linux/tmap_area_search.h"
#include "platform/linux/http_download.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <sqlite3.h>
#include <stdexcept>
#include <thread>

namespace platform::linux_runtime
{
namespace
{
namespace tc = ::platform::ui::tmap_creation;
std::string escape_query(const std::string& input)
{
    std::string output;
    for (const unsigned char c : input)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') output += char(c);
        else
        {
            output += '%';
            output += "0123456789ABCDEF"[c >> 4];
            output += "0123456789ABCDEF"[c & 15];
        }
    }
    return output;
}
std::vector<tc::Area> parse_areas(const std::string& text)
{
    sqlite3* database = nullptr;
    if (sqlite3_open(":memory:", &database) != SQLITE_OK)
    {
        sqlite3_close(database);
        throw std::runtime_error("Area response parser unavailable.");
    }
    struct Close
    {
        sqlite3* value;
        ~Close() { sqlite3_close(value); }
    } close{database};
    sqlite3_limit(database, SQLITE_LIMIT_LENGTH, 512 * 1024);
    constexpr const char* sql = R"SQL(
SELECT json_extract(value,'$.properties.osm_type'), json_extract(value,'$.properties.osm_id'),
 json_extract(value,'$.properties.name'), json_extract(value,'$.properties.countrycode'),
 json_extract(value,'$.properties.extent[0]'),json_extract(value,'$.properties.extent[3]'),
 json_extract(value,'$.properties.extent[2]'),json_extract(value,'$.properties.extent[1]'),
 json_extract(value,'$.properties.city'),json_extract(value,'$.properties.county'),
 json_extract(value,'$.properties.state'),json_extract(value,'$.properties.country')
FROM json_each(?1,'$.features')
WHERE json_array_length(value,'$.properties.extent')=4
 AND json_extract(value,'$.properties.type') IN ('country','state','county','city','district') LIMIT 12
)SQL";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) throw std::runtime_error("Area response parser unavailable.");
    struct Finalize
    {
        sqlite3_stmt* value;
        ~Finalize() { sqlite3_finalize(value); }
    } finalize{statement};
    sqlite3_bind_text(statement, 1, text.data(), int(text.size()), SQLITE_TRANSIENT);
    const auto string = [&](int index)
    {
        const auto* value = sqlite3_column_text(statement, index);
        return value ? std::string(reinterpret_cast<const char*>(value)) : std::string{};
    };
    std::vector<tc::Area> result;
    int status;
    while ((status = sqlite3_step(statement)) == SQLITE_ROW)
    {
        tc::Area area;
        const auto type = string(0);
        const auto id = sqlite3_column_int64(statement, 1);
        area.name = string(2);
        area.country = string(3);
        area.west = sqlite3_column_double(statement, 4);
        area.south = sqlite3_column_double(statement, 5);
        area.east = sqlite3_column_double(statement, 6);
        area.north = sqlite3_column_double(statement, 7);
        bool numeric = true;
        for (int i = 4; i < 8; ++i)
            numeric &= sqlite3_column_type(statement, i) == SQLITE_FLOAT || sqlite3_column_type(statement, i) == SQLITE_INTEGER;
        if (!numeric || id <= 0 || (type != "R" && type != "W") || area.name.empty() || area.name.size() > 192 ||
            !std::isfinite(area.west) || !std::isfinite(area.south) || !std::isfinite(area.east) || !std::isfinite(area.north) ||
            area.west < -180 || area.east > 180 || area.south < -90 || area.north > 90 || area.west >= area.east || area.south >= area.north) continue;
        area.south = std::max(area.south, -85.051128);
        area.north = std::min(area.north, 85.051128);
        if (area.south >= area.north) continue;
        area.id = type + std::to_string(id);
        area.description = area.name;
        for (int i = 8; i <= 11; ++i)
        {
            const auto part = string(i);
            if (!part.empty() && part != area.name && area.description.find(part) == std::string::npos)
                area.description += ", " + part;
        }
        if (area.description.size() > 512) area.description.resize(512);
        if (std::none_of(result.begin(), result.end(), [&](const auto& existing)
                         { return existing.id == area.id; })) result.push_back(std::move(area));
    }
    if (status != SQLITE_DONE) throw std::runtime_error("Invalid area search response.");
    return result;
}
} // namespace
struct TmapAreaSearch::Impl
{
    explicit Impl(std::string endpoint) : endpoint(std::move(endpoint)), worker([this]
                                                                                { run(); }) {}
    ~Impl()
    {
        {
            std::lock_guard lock(mutex);
            stopping = true;
            interrupted = true;
        }
        changed.notify_all();
        worker.join();
    }
    void run()
    {
        auto next_request = std::chrono::steady_clock::now();
        for (;;)
        {
            std::string query;
            uint32_t request;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, [&]
                             { return stopping || pending.has_value(); });
                if (stopping) return;
                changed.wait_until(lock, next_request, [&]
                                   { return stopping; });
                if (stopping) return;
                query = std::move(*pending);
                pending.reset();
                request = generation;
                interrupted = false;
            }
            tc::AreaSearchSnapshot response;
            response.query = query;
            const auto cached = cache.find(query);
            if (cached != cache.end())
            {
                response = cached->second;
                response.from_cache = true;
            }
            else
            {
                std::string body, error;
                const auto url = endpoint + (endpoint.find('?') == std::string::npos ? "?" : "&") +
                                 "q=" + escape_query(query) + "&limit=12&lang=en&layer=country&layer=state&layer=county&layer=city&layer=district";
                ::platform::ui::http_client::Request http;
                http.url = url.c_str();
                http.timeout_ms = 20000;
                http.max_bytes = 256 * 1024;
                const bool ok = http_download(
                    http, [](const uint8_t* data, size_t size, void* context)
                    { static_cast<std::string*>(context)->append(reinterpret_cast<const char*>(data), size); return true; },
                    &body, error, nullptr, &interrupted, "TrailMate/1.0 (+https://github.com/vicliu624/trail-mate)");
                next_request = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                if (!ok) response.error = "Area search failed: " + error + ". Check Wi-Fi and retry, or use coordinates.";
                else
                {
                    try
                    {
                        response.results = parse_areas(body);
                    }
                    catch (const std::exception& exception)
                    {
                        response.error = exception.what();
                    }
                    if (response.error.empty() && response.results.empty()) response.error = "No administrative boundary found. Add a country or state to the search.";
                    if (response.error.empty())
                    {
                        if (cache.size() >= 16) cache.erase(cache.begin());
                        cache[query] = response;
                    }
                }
            }
            std::lock_guard lock(mutex);
            if (request != generation || stopping) continue;
            response.revision = state.revision + 1;
            state = std::move(response);
        }
    }
    std::string endpoint;
    std::mutex mutex;
    std::condition_variable changed;
    bool stopping = false;
    std::atomic_bool interrupted{false};
    uint32_t generation = 0;
    std::optional<std::string> pending;
    tc::AreaSearchSnapshot state;
    std::map<std::string, tc::AreaSearchSnapshot> cache;
    std::thread worker;
};
TmapAreaSearch::TmapAreaSearch(std::string endpoint) : impl_(std::make_unique<Impl>(std::move(endpoint))) {}
TmapAreaSearch::~TmapAreaSearch() = default;
bool TmapAreaSearch::search(const std::string& query)
{
    if (query.empty() || query.size() > 192) return false;
    std::lock_guard lock(impl_->mutex);
    impl_->interrupted = true;
    ++impl_->generation;
    impl_->pending = query;
    const auto revision = impl_->state.revision + 1;
    impl_->state = {};
    impl_->state.revision = revision;
    impl_->state.busy = true;
    impl_->state.query = query;
    impl_->changed.notify_all();
    return true;
}
tc::AreaSearchSnapshot TmapAreaSearch::snapshot()
{
    std::lock_guard lock(impl_->mutex);
    return impl_->state;
}
} // namespace platform::linux_runtime
