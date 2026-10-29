#include "platform/linux/geocaching_source.h"
#include "geocaching_offline_cache.h"
#include "platform/linux/geocaching_draft_store.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <random>
#include <thread>
#if defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/rand.h>
#endif

namespace platform::linux_runtime
{
namespace gc = ::ui::geocaching;
using Store = LinuxGeocachingDraftStore;
using Result = Store::Result;
namespace
{
bool empty(const Store::Id& id)
{
    return std::all_of(id.begin(), id.end(), [](uint8_t byte)
                       { return !byte; });
}
const char* resultText(Result result)
{
    switch (result)
    {
    case Result::Ready:
        return "Local drafts";
    case Result::Missing:
        return "Draft no longer exists";
    case Result::Conflict:
        return "Draft changed; reopen before editing";
    case Result::Invalid:
        return "Invalid draft fields";
    case Result::Corrupt:
        return "Draft storage is corrupt; data retained";
    case Result::PublicationRequired:
        return "Published draft requires publication history support";
    default:
        return "Draft storage operation failed";
    }
}
bool newId(Store::Id& id)
{
#if defined(TRAIL_MATE_HAS_OPENSSL)
    if (RAND_bytes(id.data(), id.size()) != 1) return false;
#else
    std::random_device random;
    for (auto& byte : id) byte = static_cast<uint8_t>(random());
#endif
    return !empty(id);
}
bool sameItem(const gc::Item& a, const gc::Item& b)
{
    return a.id == b.id && a.revision_hash == b.revision_hash && a.name == b.name && a.detail == b.detail &&
           a.latitude_e7 == b.latitude_e7 && a.longitude_e7 == b.longitude_e7 && a.has_coordinates == b.has_coordinates &&
           a.downloaded == b.downloaded && a.state == b.state && a.can_download == b.can_download;
}
bool sameDraft(const Store::Summary& a, const Store::Summary& b)
{
    return a.id == b.id && a.generation == b.generation && a.state == b.state && a.name == b.name &&
           a.latitude_e7 == b.latitude_e7 && a.longitude_e7 == b.longitude_e7 && a.has_coordinates == b.has_coordinates;
}
} // namespace
struct LinuxGeocachingSource::Impl
{
    Impl(std::filesystem::path path, std::filesystem::path storage) : database(std::move(path)), storage_root(std::move(storage)), thread([this]
                                                                                                                                          { run(); }) {}
    ~Impl()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
            wake.notify_all();
        }
        if (thread.joinable()) thread.join();
    }
    struct Mutation
    {
        Store::Id id{};
        uint64_t expected = 0;
        bool remove = false;
        std::vector<uint8_t> bytes;
    };
    void changed()
    {
        if (!++generation) ++generation;
    }
    void mapChanged()
    {
        if (!++map_revision) ++map_revision;
    }
    void publishCache(GeocachingOfflineCache::Window value)
    {
        bool map_changed = cache_window.volume != value.volume || cache_window.ready != value.ready || cache_window.total != value.total;
        if (cache_window.offset == value.offset)
        {
            map_changed = map_changed || cache_window.count != value.count;
            for (size_t i = 0; i < value.count && !map_changed; ++i)
                map_changed = !sameItem(cache_window.rows[i], value.rows[i]);
        }
        bool different = cache_loading || cache_window.volume != value.volume || cache_window.ready != value.ready || cache_window.offset != value.offset ||
                         cache_window.count != value.count || cache_window.total != value.total || cache_window.status != value.status;
        for (size_t i = 0; i < value.count && !different; ++i)
            different = !sameItem(cache_window.rows[i], value.rows[i]) || cache_window.references[i].volume != value.references[i].volume ||
                        cache_window.references[i].file_hash != value.references[i].file_hash;
        cache_window = std::move(value);
        if (map_changed) mapChanged();
        cache_loading = false;
        if (different && !++cache_generation) ++cache_generation;
        if (!cache_window.ready || (detail_reference.valid && detail_reference.volume != cache_window.volume))
        {
            ++detail_generation;
            detail_pending = false;
            detail_record = {};
            detail_status = gc::DetailStatus::Failed;
            detail_error = "Offline cache storage is unavailable";
        }
        for (size_t i = 0; i < cache_window.count; ++i)
            if (cache_window.rows[i].id == detail_reference.id.bytes &&
                (!cache_window.rows[i].downloaded || cache_window.rows[i].revision_hash != detail_reference.hash.bytes))
            {
                ++detail_generation;
                detail_pending = false;
                detail_record = {};
                detail_status = gc::DetailStatus::Failed;
                detail_error = "Offline copy changed; reopen it";
            }
    }
    void run()
    {
        Store store(database);
        GeocachingOfflineCache offline(storage_root);
        Result storage_result = Result::Error;
        try
        {
            storage_result = store.open();
            Store::Id seed{};
            const bool has_id = storage_result == Result::Ready && newId(seed);
            {
                std::lock_guard<std::mutex> lock(mutex);
                ready = storage_result == Result::Ready;
                mapChanged();
                status = resultText(storage_result);
                if (has_id)
                {
                    next_id = seed;
                    id_ready = true;
                }
                changed();
            }
            if (!storage_root.empty())
            {
                offline.open(stopping);
                GeocachingOfflineCache::Window window;
                offline.window(0, window);
                std::lock_guard<std::mutex> lock(mutex);
                publishCache(std::move(window));
            }
            uint64_t catalog_seen = 0;
            store.catalogGeneration(catalog_seen);
            auto catalog_check = std::chrono::steady_clock::now();
            for (;;)
            {
                Mutation command;
                bool mutate = false, read = false, list = false, replenish = false;
                bool cache_list = false, cache_detail = false, cache_refresh = false;
                JournalReference cache_reference;
                uint64_t cache_epoch = 0, detail_epoch = 0;
                Store::Id read_id{};
                uint64_t read_epoch = 0, list_epoch = 0;
                size_t offset = 0;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    const auto pending = [&]
                    { return stopping || mutation_pending || read_pending || list_pending || detail_pending || cache_pending || (ready && !id_ready); };
                    wake.wait_for(lock, std::chrono::milliseconds(500), pending);
                    if (stopping && !mutation_pending) break;
                    if (mutation_pending)
                    {
                        command = std::move(mutation);
                        mutation_pending = false;
                        mutate = true;
                    }
                    else if (read_pending)
                    {
                        read_id = requested_read;
                        read_epoch = read_generation;
                        read_pending = false;
                        read = true;
                    }
                    else if (list_pending)
                    {
                        offset = requested_offset;
                        list_epoch = window_generation;
                        list_pending = false;
                        list = true;
                    }
                    else if (detail_pending)
                    {
                        cache_detail = true;
                        cache_reference = detail_reference;
                        detail_epoch = detail_generation;
                        detail_pending = false;
                    }
                    else if (cache_pending || (!storage_root.empty() && id_ready))
                    {
                        cache_list = true;
                        cache_epoch = cache_window_generation;
                        offset = cache_requested_offset;
                        cache_refresh = cache_refresh_pending;
                        cache_refresh_pending = cache_pending = false;
                    }
                    else replenish = true;
                }
                // SQLite owns the shared draft revision. Observe external
                // HostLink edits here, without storage work on UI callbacks.
                const auto now = std::chrono::steady_clock::now();
                if (now >= catalog_check)
                {
                    catalog_check = now + std::chrono::milliseconds(500);
                    uint64_t current_catalog = 0;
                    if (store.catalogGeneration(current_catalog) == Result::Ready && current_catalog != catalog_seen)
                    {
                        catalog_seen = current_catalog;
                        std::lock_guard<std::mutex> lock(mutex);
                        ++window_generation;
                        list_pending = true;
                        window_ready = false;
                        if (!empty(requested_read))
                        {
                            ++read_generation;
                            read_pending = true;
                            read_result = gc::DraftReadStatus::Pending;
                            read_row = {};
                        }
                        mapChanged();
                        changed();
                        wake.notify_all();
                    }
                }
                if (mutate)
                {
                    const auto result = command.remove ? store.erase(command.id, command.expected) : store.save(command.id, command.expected, {command.bytes.data(), command.bytes.size()});
                    if (result == Result::Ready) store.catalogGeneration(catalog_seen);
                    std::lock_guard<std::mutex> lock(mutex);
                    mutation_busy = false;
                    completion = result == Result::Ready ? gc::DraftSaveStatus::Saved : gc::DraftSaveStatus::Failed;
                    if (result == Result::Ready) mapChanged();
                    status = resultText(result);
                    list_pending = true;
                    ++window_generation;
                    window_ready = false;
                    if (requested_read == command.id)
                    {
                        ++read_generation;
                        requested_read = {};
                        read_pending = false;
                        read_row = {};
                        read_result = gc::DraftReadStatus::Failed;
                    }
                    changed();
                    wake.notify_all();
                }
                else if (read)
                {
                    Store::Row row;
                    const auto result = store.read(read_id, row);
                    std::lock_guard<std::mutex> lock(mutex);
                    if (read_epoch == read_generation && read_id == requested_read)
                    {
                        read_row = std::move(row);
                        read_result = result == Result::Ready ? gc::DraftReadStatus::Ready : gc::DraftReadStatus::Failed;
                    }
                }
                else if (list)
                {
                    Store::Page rows;
                    const auto result = store.page(offset, rows);
                    std::lock_guard<std::mutex> lock(mutex);
                    if (list_epoch == window_generation)
                    {
                        bool map_changed = page.total != rows.total;
                        if (page_offset == offset)
                        {
                            map_changed = map_changed || page.count != rows.count;
                            for (size_t i = 0; i < rows.count && !map_changed; ++i)
                                map_changed = !sameDraft(page.rows[i], rows.rows[i]);
                        }
                        if (result == Result::Ready && map_changed) mapChanged();
                        page = rows;
                        page_offset = offset;
                        window_ready = result == Result::Ready;
                        if (result != Result::Ready) status = resultText(result);
                        changed();
                    }
                }
                else if (cache_detail)
                {
                    LinuxGeocachingRecordStore::Record record;
                    std::string error;
                    const bool valid = offline.detail(cache_reference, record, error);
                    std::lock_guard<std::mutex> lock(mutex);
                    if (detail_epoch == detail_generation)
                    {
                        detail_record = std::move(record);
                        detail_error = std::move(error);
                        detail_status = valid ? gc::DetailStatus::Ready : gc::DetailStatus::Failed;
                    }
                }
                else if (cache_list)
                {
                    if (!offline.healthy() || cache_refresh) offline.open(stopping);
                    GeocachingOfflineCache::Window window;
                    offline.window(offset, window);
                    std::lock_guard<std::mutex> lock(mutex);
                    if (cache_epoch == cache_window_generation) publishCache(std::move(window));
                }
                else if (replenish)
                {
                    Store::Id id;
                    if (!newId(id)) throw std::runtime_error("Draft identity allocation failed");
                    std::lock_guard<std::mutex> lock(mutex);
                    next_id = id;
                    id_ready = true;
                    changed();
                }
            }
        }
        catch (...)
        {
            std::lock_guard<std::mutex> lock(mutex);
            ready = false;
            mutation_busy = mutation_pending = false;
            completion = gc::DraftSaveStatus::Failed;
            read_result = gc::DraftReadStatus::Failed;
            status = "Draft worker stopped after an error; data retained";
            cache_window = {};
            cache_window.status = "Offline cache worker stopped; files retained";
            cache_loading = false;
            ++cache_generation;
            ++detail_generation;
            detail_pending = false;
            detail_record = {};
            detail_status = gc::DetailStatus::Failed;
            detail_error = "Offline cache worker stopped; reopen after restart";
            changed();
        }
    }
    using JournalReference = LinuxGeocachingInstallJournal::InstalledReference;
    std::filesystem::path database, storage_root;
    std::mutex mutex;
    std::condition_variable wake;
    std::atomic_bool stopping{false};
    bool ready = false, id_ready = false, completion_unread = false;
    bool list_pending = true, window_ready = false, mutation_pending = false, mutation_busy = false, read_pending = false;
    uint64_t generation = 1, window_generation = 1, read_generation = 0, map_revision = 1;
    size_t requested_offset = 0, page_offset = 0;
    Store::Page page;
    Store::Id next_id{}, completion_id{}, requested_read{};
    uint64_t completion_expected = 0;
    gc::DraftSaveStatus completion = gc::DraftSaveStatus::Failed;
    gc::DraftReadStatus read_result = gc::DraftReadStatus::Failed;
    Store::Row read_row;
    Mutation mutation;
    std::string status = "Opening local draft storage...";
    GeocachingOfflineCache::Window cache_window;
    uint64_t cache_generation = 1, cache_window_generation = 1, detail_generation = 0;
    size_t cache_requested_offset = 0;
    bool cache_pending = false, cache_refresh_pending = false, cache_loading = false, detail_pending = false;
    JournalReference detail_reference;
    gc::DetailStatus detail_status = gc::DetailStatus::Failed;
    LinuxGeocachingRecordStore::Record detail_record;
    std::string detail_error;
    std::thread thread; // Last: all mailbox fields exist before the worker starts.
};
LinuxGeocachingSource::LinuxGeocachingSource(std::filesystem::path database, std::filesystem::path storage_root)
    : impl_(std::make_unique<Impl>(std::move(database), std::move(storage_root))) {}
LinuxGeocachingSource::~LinuxGeocachingSource() = default;
bool LinuxGeocachingSource::localMapRevision(uint64_t& out)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->stopping || (!impl_->ready && !impl_->cache_window.ready)) return false;
    out = impl_->map_revision;
    return true;
}
void LinuxGeocachingSource::mapSnapshot(gc::Section section, gc::Snapshot& out)
{
    snapshot(section, out);
    // Drafts live in the application database, independently of the removable
    // offline volume. Do not starve that section behind a known missing volume.
    if (section == gc::Section::Downloaded && !out.ready && !out.busy)
    {
        out.ready = true;
        out.count = 0;
    }
}
void LinuxGeocachingSource::snapshot(gc::Section section, gc::Snapshot& out)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    out = {};
    out.generation = impl_->generation;
    if (section == gc::Section::Downloaded)
    {
        out.generation = impl_->cache_generation;
        out.ready = impl_->cache_window.ready && !impl_->cache_loading;
        out.busy = impl_->cache_loading;
        out.count = impl_->cache_window.total;
        out.can_refresh = !impl_->storage_root.empty();
        const auto& status = impl_->cache_window.status;
        std::snprintf(out.status.data(), out.status.size(), "%s", status.empty() ? "Offline storage is not configured" : status.c_str());
        return;
    }
    if (section != gc::Section::Published)
    {
        out.ready = true;
        std::snprintf(out.status.data(), out.status.size(), "%s", section == gc::Section::Discover ? "Directory transport is not connected" : "Offline cache download service is not connected");
        return;
    }
    out.ready = impl_->ready && impl_->window_ready;
    out.count = impl_->page.total;
    out.can_refresh = impl_->ready;
    out.can_create = impl_->ready && impl_->id_ready && !impl_->mutation_busy && !impl_->completion_unread;
    std::snprintf(out.status.data(), out.status.size(), "%s", impl_->status.c_str());
}
void LinuxGeocachingSource::requestWindow(gc::Section section, size_t offset, size_t)
{
    if (section == gc::Section::Downloaded)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->cache_requested_offset == offset) return;
        impl_->cache_requested_offset = offset;
        ++impl_->cache_window_generation;
        ++impl_->cache_generation;
        impl_->cache_pending = true;
        impl_->cache_loading = true;
        impl_->wake.notify_all();
        return;
    }
    if (section != gc::Section::Published) return;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->requested_offset == offset) return;
    impl_->requested_offset = offset;
    ++impl_->window_generation;
    impl_->list_pending = true;
    impl_->window_ready = false;
    impl_->changed();
    impl_->wake.notify_all();
}
bool LinuxGeocachingSource::item(gc::Section section, size_t index, uint64_t generation, gc::Item& out)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (section == gc::Section::Downloaded)
    {
        if (impl_->cache_loading || !impl_->cache_window.ready || generation != impl_->cache_generation ||
            index < impl_->cache_window.offset || index - impl_->cache_window.offset >= impl_->cache_window.count) return false;
        out = impl_->cache_window.rows[index - impl_->cache_window.offset];
        return true;
    }
    if (section != gc::Section::Published || !impl_->window_ready || generation != impl_->generation ||
        index < impl_->page_offset || index - impl_->page_offset >= impl_->page.count) return false;
    const auto& row = impl_->page.rows[index - impl_->page_offset];
    out = {};
    out.is_draft = true;
    out.edit_generation = row.generation;
    std::copy(row.id.begin(), row.id.end(), out.id.begin());
    out.name = row.name;
    out.latitude_e7 = row.latitude_e7;
    out.longitude_e7 = row.longitude_e7;
    out.has_coordinates = row.has_coordinates;
    out.state = row.state;
    std::snprintf(out.detail.data(), out.detail.size(), "Local draft; revision %llu", static_cast<unsigned long long>(row.generation));
    return true;
}
void LinuxGeocachingSource::refresh(gc::Section section)
{
    if (section == gc::Section::Downloaded)
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->cache_pending = impl_->cache_loading = impl_->cache_refresh_pending = true;
        ++impl_->cache_window_generation;
        ++impl_->cache_generation;
        impl_->wake.notify_all();
        return;
    }
    if (section != gc::Section::Published) return;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->list_pending = true;
    impl_->window_ready = false;
    ++impl_->window_generation;
    impl_->changed();
    impl_->wake.notify_all();
}
void LinuxGeocachingSource::open(const gc::Item& item, uint64_t generation)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (generation != impl_->cache_generation || impl_->cache_loading) return;
    for (size_t i = 0; i < impl_->cache_window.count; ++i)
        if (impl_->cache_window.rows[i].id == item.id && impl_->cache_window.rows[i].revision_hash == item.revision_hash)
        {
            impl_->detail_reference = impl_->cache_window.references[i];
            ++impl_->detail_generation;
            impl_->detail_status = gc::DetailStatus::Pending;
            impl_->detail_pending = true;
            impl_->detail_record = {};
            impl_->detail_error.clear();
            impl_->wake.notify_all();
            return;
        }
}
bool LinuxGeocachingSource::readDetail(const std::array<uint8_t, 32>& id, const std::array<uint8_t, 32>& hash,
                                       void (*sink)(const gc::DetailView&, void*), void* context)
{
    if (!sink) return false;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (id != impl_->detail_reference.id.bytes || hash != impl_->detail_reference.hash.bytes) return false;
    gc::DetailView view;
    view.status = impl_->detail_status;
    view.error = impl_->detail_error;
    if (view.status == gc::DetailStatus::Ready)
    {
        ::geocaching::protocol::CmpReader reader({impl_->detail_record.bytes.data(), impl_->detail_record.bytes.size()});
        size_t fields = 0;
        ::geocaching::ByteView encoded, signature;
        ::geocaching::RecordView record;
        if (!reader.array(fields, 2) || fields != 2 || !reader.binary(encoded, 4096) || !reader.binary(signature, 64) || !reader.finished() || !::geocaching::protocol::decodeGeocacheRecord(encoded, record)) return false;
        view.description = record.description;
        view.hint = record.hint;
    }
    sink(view, context);
    return true;
}
void LinuxGeocachingSource::closeDetail()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ++impl_->detail_generation;
    impl_->detail_pending = false;
    impl_->detail_reference = {};
    impl_->detail_record = {};
    impl_->detail_status = gc::DetailStatus::Failed;
    impl_->detail_error.clear();
}
gc::DraftReadStatus LinuxGeocachingSource::readDraft(const Store::Id& id, void (*sink)(const gc::DraftInput&, void*), void* context)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->ready || empty(id) || !sink) return gc::DraftReadStatus::Failed;
    if (impl_->requested_read != id)
    {
        impl_->requested_read = id;
        ++impl_->read_generation;
        impl_->read_pending = true;
        impl_->read_result = gc::DraftReadStatus::Pending;
        impl_->read_row = {};
        impl_->wake.notify_all();
    }
    if (impl_->read_result == gc::DraftReadStatus::Ready)
    {
        ::geocaching::storage::DraftView view;
        if (!impl_->read_row.view(view)) return gc::DraftReadStatus::Failed;
        gc::DraftInput input;
        input.id = id;
        input.generation = view.generation;
        input.name = view.name;
        input.description = view.description;
        input.hint = view.hint;
        input.latitude_e7 = view.latitude_e7;
        input.longitude_e7 = view.longitude_e7;
        input.has_coordinates = view.has_coordinates;
        input.state = view.state;
        input.difficulty_x2 = view.difficulty_x2;
        input.terrain_x2 = view.terrain_x2;
        input.container_size = view.container_size;
        sink(input, context);
    }
    return impl_->read_result;
}
void LinuxGeocachingSource::cancelDraftRead(const Store::Id& id)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->requested_read != id) return;
    impl_->requested_read = {};
    ++impl_->read_generation;
    impl_->read_pending = false;
    impl_->read_row = {};
    impl_->read_result = gc::DraftReadStatus::Failed;
}
bool LinuxGeocachingSource::saveDraft(gc::DraftInput& input)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->ready || impl_->stopping || impl_->mutation_busy || impl_->completion_unread || input.generation == UINT64_MAX || (empty(input.id) && !impl_->id_ready)) return false;
    auto id = empty(input.id) ? impl_->next_id : input.id;
    ::geocaching::storage::DraftView draft;
    draft.generation = input.generation + 1;
    draft.name = input.name;
    draft.description = input.description;
    draft.hint = input.hint;
    draft.latitude_e7 = input.latitude_e7;
    draft.longitude_e7 = input.longitude_e7;
    draft.has_coordinates = input.has_coordinates;
    draft.state = input.state;
    draft.difficulty_x2 = input.difficulty_x2;
    draft.terrain_x2 = input.terrain_x2;
    draft.container_size = input.container_size;
    std::vector<uint8_t> bytes(4096);
    size_t written = 0;
    if (!::geocaching::storage::encodeDraft({id.data(), id.size()}, draft, bytes.data(), bytes.size(), written)) return false;
    bytes.resize(written);
    if (empty(input.id)) impl_->id_ready = false;
    input.id = id;
    impl_->mutation = {id, input.generation, false, std::move(bytes)};
    impl_->completion_id = id;
    impl_->completion_expected = input.generation;
    impl_->completion = gc::DraftSaveStatus::Pending;
    impl_->mutation_pending = impl_->mutation_busy = true;
    impl_->completion_unread = true;
    impl_->changed();
    impl_->wake.notify_all();
    return true;
}
bool LinuxGeocachingSource::deleteDraft(const Store::Id& id, uint64_t expected)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->ready || impl_->stopping || impl_->mutation_busy || impl_->completion_unread || empty(id) || !expected) return false;
    impl_->mutation = {id, expected, true, {}};
    impl_->completion_id = id;
    impl_->completion_expected = expected;
    impl_->completion = gc::DraftSaveStatus::Pending;
    impl_->mutation_pending = impl_->mutation_busy = true;
    impl_->completion_unread = true;
    impl_->changed();
    impl_->wake.notify_all();
    return true;
}
gc::DraftSaveStatus LinuxGeocachingSource::draftSaveStatus(const Store::Id& id, uint64_t expected)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->completion_id != id || impl_->completion_expected != expected) return gc::DraftSaveStatus::Failed;
    if (impl_->completion != gc::DraftSaveStatus::Pending && impl_->completion_unread)
    {
        impl_->completion_unread = false;
        impl_->changed();
    }
    return impl_->completion;
}
} // namespace platform::linux_runtime
