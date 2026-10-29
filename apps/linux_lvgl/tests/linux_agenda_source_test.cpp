#include "agenda/domain/recurrence.h"
#include "agenda/usecase/agenda_service.h"
#include "platform/linux/agenda_source.h"
#include "platform/linux/agenda_store.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <future>
#include <mutex>
#include <set>
#include <sqlite3.h>
#include <stdexcept>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>

using namespace platform::linux_runtime;
using namespace std::chrono_literals;
namespace presentation = ui::agenda;
using State = presentation::CommandState;
template <class Predicate>
void wait(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + 20s;
    while (!predicate())
    {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(2ms);
    }
}
struct Clock final : agenda::IAgendaClock
{
    mutable std::mutex mutex;
    std::set<std::thread::id> frontend{std::this_thread::get_id()};
    agenda::ClockSample value;
    Clock()
    {
        assert(agenda::toCalendarSeconds({2026, 10, 21, 12, 0, 0}, value.calendar_seconds));
        value.valid = true;
    }
    agenda::ClockSample sample() const override
    {
        std::lock_guard<std::mutex> lock(mutex);
        assert(!frontend.count(std::this_thread::get_id()));
        return value;
    }
    void advance(uint64_t seconds)
    {
        std::lock_guard<std::mutex> lock(mutex);
        value.calendar_seconds += seconds;
        value.monotonic_seconds += seconds;
    }
    int64_t now() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return value.calendar_seconds;
    }
    void markFrontend()
    {
        std::lock_guard<std::mutex> lock(mutex);
        frontend.insert(std::this_thread::get_id());
    }
    void unmarkFrontend()
    {
        std::lock_guard<std::mutex> lock(mutex);
        frontend.erase(std::this_thread::get_id());
    }
};
struct Factory
{
    std::mutex mutex;
    std::condition_variable wake;
    bool blocked = false, entered = false, fail = false, throwing = false;
    unsigned calls = 0;
    int probe = -1;
    ~Factory()
    {
        if (probe >= 0) close(probe);
    }
    int create()
    {
        std::unique_lock<std::mutex> lock(mutex);
        ++calls;
        entered = true;
        wake.notify_all();
        wake.wait(lock, [&]
                  { return !blocked; });
        if (throwing) throw std::runtime_error("projection unavailable");
        if (fail) return -1;
        const int fd = memfd_create("agenda-test-projection", MFD_CLOEXEC | MFD_ALLOW_SEALING);
        if (probe >= 0) close(probe);
        probe = fd >= 0 ? dup(fd) : -1;
        return fd;
    }
    void block()
    {
        std::lock_guard<std::mutex> lock(mutex);
        blocked = true;
        entered = false;
    }
    void await()
    {
        std::unique_lock<std::mutex> lock(mutex);
        assert(wake.wait_for(lock, 5s, [&]
                             { return entered; }));
    }
    void release()
    {
        std::lock_guard<std::mutex> lock(mutex);
        blocked = false;
        wake.notify_all();
    }
    void throws(bool value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        throwing = value;
    }
    int file()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return dup(probe);
    }
};
agenda::EventRecord event(int64_t now)
{
    agenda::EventRecord value;
    value.state = agenda::RecordState::Active;
    value.start_time = now + 610;
    value.flags = agenda::HasReminder | agenda::HasLocation | agenda::HasNote;
    value.reminder_offset_sec = 600;
    value.location_type = agenda::LocationType::Coordinate;
    value.latitude_e7 = 311234567;
    value.longitude_e7 = 1212345678;
    std::strcpy(value.title, "Calendar worker");
    std::strcpy(value.note, "中文 😀");
    return value;
}
presentation::AgendaSnapshot snapshot(LinuxAgendaSource& source)
{
    presentation::AgendaSnapshot result;
    source.snapshot({0, 5, {}}, result);
    return result;
}
void finish(LinuxAgendaSource& source)
{
    wait([&]
         { return source.commandResult().state != State::Pending; });
}
void sql(const std::filesystem::path& path, const char* query)
{
    sqlite3* db = nullptr;
    assert(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
    assert(sqlite3_exec(db, query, nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
}
int main()
{
    char root[] = "/tmp/trailmate-agenda-source-XXXXXX";
    assert(mkdtemp(root));
    Clock clock;
    const auto database = std::filesystem::path(root) / "agenda.sqlite3";
    auto draft = event(clock.now());
    uint32_t first = 0, second = 0;
    {
        Factory factory;
        LinuxAgendaSource source({database, &clock, [&]
                                  { return factory.create(); }});
        wait([&]
             { return snapshot(source).storage_ready; });
        assert(snapshot(source).page.count == 0);
        const int fd = factory.file();
        assert(fd >= 0);
        assert((fcntl(fd, F_GET_SEALS) & F_SEAL_WRITE) != 0);
        const uint8_t byte = 0xff;
        assert(pwrite(fd, &byte, 1, 0) == -1);
        close(fd);
        presentation::AgendaEditorModel editor;
        assert(source.newDraft(editor) && editor.event.id == 0 && editor.event.start_time > clock.now());
        factory.block();
        assert(source.save(draft).ok);
        factory.await();
        assert(source.commandResult().state == State::Pending);
        assert(!source.remove(1).ok && source.remove(1).failure == ui::UiActionFailure::Busy);
        auto getters = std::async(std::launch::async, [&]
                                  {
            clock.markFrontend();
            for (unsigned i = 0; i < 10; ++i)
            {
                assert(snapshot(source).page.count == 0);
                agenda::EventRecord missing;
                assert(source.detail(1, missing) == agenda::AgendaResult::NotFound);
                presentation::MonthSnapshot month; source.month(2026, 10, month);
                assert(month.result == agenda::StoreResult::Ok && month.occupied_days == 0);
                assert(source.markerStatus().ready && source.currentTime().valid);
            }
            clock.unmarkFrontend(); });
        const bool responsive = getters.wait_for(1s) == std::future_status::ready;
        if (!responsive) factory.release();
        getters.get();
        assert(responsive);
        factory.release();
        finish(source);
        const auto result = source.commandResult();
        first = result.event_id;
        assert(result.state == State::Succeeded && first);
        agenda::EventRecord saved;
        assert(source.detail(first, saved) == agenda::AgendaResult::Ok && !std::strcmp(saved.note, draft.note));
        assert(snapshot(source).page.count == 1);
        alignas(std::max_align_t) uint8_t scratch[sizeof(agenda::EventRecord)];
        unsigned markers = 0;
        assert(source.visitMarkers([](const ui::map::MapMarker& marker, void* context)
                                   { assert(marker.latitude_e7 == 311234567); ++*static_cast<unsigned*>(context); },
                                   &markers, scratch, sizeof(scratch)) &&
               markers == 1);
        presentation::MonthSnapshot month;
        source.month(2026, 10, month);
        assert(month.occupied_days & (uint32_t{1} << 20));

        // A committed write stays successful even if a later read projection
        // cannot be allocated/published. Recovery must not duplicate the event.
        factory.throws(true);
        assert(source.save(draft).ok);
        finish(source);
        second = source.commandResult().event_id;
        assert(source.commandResult().state == State::Succeeded && second > first);
        assert(!snapshot(source).storage_ready && !source.save(draft).ok);
        factory.throws(false);
        source.requestRefresh();
        wait([&]
             { return snapshot(source).storage_ready; });
        assert(snapshot(source).page.count == 2 && source.detail(second, saved) == agenda::AgendaResult::Ok);
        LinuxAgendaSource competing({database, &clock, {}});
        wait([&]
             { return competing.currentTime().valid; });
        assert(!snapshot(competing).storage_ready && !competing.save(draft).ok);
        competing.shutdown();
        saved.id = first;
        std::strcpy(saved.title, "Drain before shutdown");
        assert(source.save(saved).ok);
        source.shutdown();
        assert(source.commandResult().state == State::Succeeded);
        assert(!snapshot(source).storage_ready && !source.currentTime().valid && !source.save(draft).ok);
    }
    {
        LinuxAgendaStore store(database);
        assert(store.begin() == agenda::StoreResult::Ok);
        agenda::AgendaService service(store);
        agenda::EventRecord saved;
        assert(service.read(first, saved) == agenda::AgendaResult::Ok && !std::strcmp(saved.title, "Drain before shutdown"));
        assert(service.read(second, saved) == agenda::AgendaResult::Ok);
    }
    {
        const auto reminders = std::filesystem::path(root) / "reminders.sqlite3";
        LinuxAgendaSource source({reminders, &clock, {}});
        wait([&]
             { return snapshot(source).storage_ready; });
        assert(source.save(event(clock.now())).ok);
        finish(source);
        clock.advance(10);
        source.requestRefresh();
        wait([&]
             { return source.currentTime().calendar_seconds == clock.now(); });
        assert(!source.reminderSnapshot().reminder.valid);
        source.setPresentationAllowed(true);
        wait([&]
             { return source.reminderSnapshot().reminder.valid; });
        const auto shown = source.reminderSnapshot();
        assert(!source.snoozeReminder(shown.revision + 1).ok);
        assert(source.snoozeReminder(shown.revision).ok);
        finish(source);
        assert(!source.reminderSnapshot().reminder.valid);
        clock.advance(599);
        source.requestRefresh();
        wait([&]
             { return source.currentTime().calendar_seconds == clock.now(); });
        assert(!source.reminderSnapshot().reminder.valid);
        clock.advance(1);
        source.requestRefresh();
        wait([&]
             { return source.reminderSnapshot().reminder.valid; });
        assert(!source.dismissReminder(shown.revision).ok);
        assert(source.dismissReminder(source.reminderSnapshot().revision).ok);
        finish(source);
        assert(!source.reminderSnapshot().reminder.valid);
        const auto ready_revision = snapshot(source).revision;
        sql(reminders, "UPDATE linux_agenda_meta SET version=99");
        source.requestRefresh();
        wait([&]
             { return !snapshot(source).storage_ready; });
        assert(snapshot(source).revision != ready_revision);
        assert(!source.save(draft).ok);
    }
    {
        const auto damaged = std::filesystem::path(root) / "damaged.sqlite3";
        {
            LinuxAgendaStore store(damaged);
            assert(store.begin() == agenda::StoreResult::Ok);
            auto healthy = draft;
            healthy.id = 100;
            assert(store.writeSlot(1, healthy) == agenda::StoreResult::Ok);
        }
        sql(damaged, "UPDATE linux_agenda_slots SET record=zeroblob(256) WHERE slot=0");
        LinuxAgendaSource source({damaged, &clock, {}});
        wait([&]
             { return snapshot(source).storage_ready; });
        const auto view = snapshot(source);
        assert(view.page.degraded && view.result == agenda::StoreResult::Ok && view.page.count == 1);
        presentation::MonthSnapshot month;
        source.month(2026, 10, month);
        assert(month.result == agenda::StoreResult::Corrupt);
        assert(source.save(draft).ok);
        finish(source);
        assert(source.commandResult().state == State::Failed && source.commandResult().result == agenda::AgendaResult::StorageError);
    }
    {
        const auto victim = std::filesystem::path(root) / "ordinary-file";
        {
            std::ofstream file(victim);
            file << "preserve";
        }
        LinuxAgendaSource source({std::filesystem::path(root) / "invalid-projection.sqlite3", &clock,
                                  [&]
                                  { return ::open(victim.c_str(), O_RDWR); }});
        wait([&]
             { return source.currentTime().valid; });
        assert(!snapshot(source).storage_ready);
        source.shutdown();
        std::ifstream file(victim);
        std::string contents;
        file >> contents;
        assert(contents == "preserve");
    }
    std::filesystem::remove_all(root);
}
