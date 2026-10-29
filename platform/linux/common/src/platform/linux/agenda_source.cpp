#include "platform/linux/agenda_source.h"
#include "agenda/domain/event_codec.h"
#include "platform/linux/agenda_clock.h"
#include "platform/linux/agenda_store.h"
#include "product_composition/agenda_composition.h"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#if defined(__linux__)
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace platform::linux_runtime
{
namespace
{
namespace agenda = ::agenda;
namespace presentation = ::ui::agenda;
using Result = agenda::StoreResult;
using State = presentation::CommandState;
using Failure = ::ui::UiActionFailure;
struct Projection
{
    int fd = -1;
    std::array<Result, agenda::kMaxActiveEvents> status{};
    ~Projection()
    {
#if defined(__linux__)
        if (fd >= 0) ::close(fd);
#endif
    }
};
int createProjection()
{
#if defined(__linux__)
    return ::memfd_create("trailmate-agenda-view", MFD_CLOEXEC | MFD_ALLOW_SEALING);
#else
    return -1;
#endif
}
std::shared_ptr<const Projection> project(agenda::IAgendaStore& store, const LinuxAgendaOptions& options)
try
{
#if defined(__linux__)
    auto view = std::make_shared<Projection>();
    view->fd = options.create_projection ? options.create_projection() : createProjection();
    if (view->fd < 0 || ::fcntl(view->fd, F_GET_SEALS) != 0 || ::fcntl(view->fd, F_SETFD, FD_CLOEXEC) != 0 ||
        ::ftruncate(view->fd, agenda::kMaxActiveEvents * agenda::kSlotBytes) != 0) return {};
    uint8_t bytes[agenda::kSlotBytes];
    agenda::EventRecord event;
    for (uint16_t slot = 0; slot < agenda::kMaxActiveEvents; ++slot)
    {
        view->status[slot] = store.readSlot(slot, event);
        if (view->status[slot] != Result::Ok) continue;
        if (!agenda::encodeEvent(event, bytes))
        {
            view->status[slot] = Result::InvalidRecord;
            continue;
        }
        if (::pwrite(view->fd, bytes, sizeof(bytes), off_t(slot) * sizeof(bytes)) != sizeof(bytes)) return {};
    }
    if (::fcntl(view->fd, F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL) != 0) return {};
    return view;
#else
    (void)store;
    (void)options;
    return {};
#endif
}
catch (...)
{
    return {};
}
struct View
{
    std::shared_ptr<const Projection> data;
    agenda::ClockSample clock;
    presentation::ReminderSnapshot reminder;
    uint32_t revision = 0;
    bool ready = false;
};
struct Clock final : agenda::IAgendaClock
{
    agenda::ClockSample value;
    agenda::ClockSample sample() const override { return value; }
};
// This read-only projection decodes one record at a time from a sealed RAM
// file. It cannot mutate persistent state or authorize an optimistic write.
struct ReadStore final : agenda::IAgendaStore
{
    std::shared_ptr<const View> view;
    explicit ReadStore(std::shared_ptr<const View> value) : view(std::move(value)) {}
    uint16_t slotCount() const override { return agenda::kMaxActiveEvents; }
    Result writeSlot(uint16_t, const agenda::EventRecord&) override { return Result::IoError; }
    Result eraseSlot(uint16_t) override { return Result::IoError; }
    Result readSlot(uint16_t slot, agenda::EventRecord& out) override
    {
        out = {};
        if (slot >= slotCount()) return Result::OutOfRange;
        if (!view->ready || !view->data) return Result::IoError;
        const auto result = view->data->status[slot];
        if (result != Result::Ok) return result;
#if defined(__linux__)
        uint8_t bytes[agenda::kSlotBytes];
        if (::pread(view->data->fd, bytes, sizeof(bytes), off_t(slot) * sizeof(bytes)) != sizeof(bytes)) return Result::IoError;
        const auto decoded = agenda::decodeEvent(bytes, out);
        if (decoded == agenda::DecodeResult::Ok) return Result::Ok;
        out = {};
        return decoded == agenda::DecodeResult::UnsupportedVersion ? Result::UnsupportedVersion : decoded == agenda::DecodeResult::InvalidRecord ? Result::InvalidRecord
                                                                                                                                                 : Result::Corrupt;
#else
        return Result::IoError;
#endif
    }
};
// Query semantics remain in the production presentation/core graph. Only the
// backing store and sampled clock differ from its worker-owned write graph.
struct Reader
{
    ReadStore store;
    Clock clock;
    agenda::AgendaService service{store};
    presentation::AgendaReminderModel reminders;
    agenda::ReminderScheduler scheduler{store, clock, reminders};
    presentation::AgendaWorkspaceModel model{store, service, clock, scheduler, reminders};
    explicit Reader(std::shared_ptr<const View> view, bool active) : store(std::move(view))
    {
        clock.value = store.view->clock;
        clock.value.valid = clock.value.valid && active;
        model.setStorageReady(store.view->ready && active);
    }
};
} // namespace
struct LinuxAgendaSource::Impl
{
    enum class Kind
    {
        Save,
        Remove,
        Dismiss,
        Snooze
    };
    struct Command
    {
        Kind kind = Kind::Save;
        agenda::EventRecord event;
        uint32_t id = 0, sequence = 0;
    };
    explicit Impl(LinuxAgendaOptions value) : options(std::move(value)), published(std::make_shared<View>()), worker([this]
                                                                                                                     { run(); }) {}
    ~Impl() { stop(); }
    std::shared_ptr<const View> view() const { return std::atomic_load(&published); }
    void publish(const View& view, const presentation::CommandResult* completion = nullptr)
    {
        auto next = std::make_shared<const View>(view);
        if (completion)
        {
            std::lock_guard<std::mutex> lock(mutex);
            std::atomic_store(&published, std::move(next));
            result = *completion;
            busy = false;
        }
        else std::atomic_store(&published, std::move(next));
    }
    void stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
            wake.notify_all();
        }
        if (worker.joinable()) worker.join();
    }
    ::ui::UiActionResult enqueue(Command command)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopping || !active || !view()->ready) return ::ui::UiActionResult::fail(Failure::NotReady);
        if (busy) return ::ui::UiActionResult::fail(Failure::Busy);
        if (command.kind == Kind::Save)
        {
            auto normalized = command.event;
            normalized.state = agenda::RecordState::Active;
            if (!normalized.id) normalized.id = 1;
            if (!agenda::validEvent(normalized)) return ::ui::UiActionResult::fail(Failure::InvalidInput);
        }
        if ((command.kind == Kind::Dismiss || command.kind == Kind::Snooze) &&
            (!view()->reminder.reminder.valid || command.id != view()->reminder.revision))
            return ::ui::UiActionResult::fail(Failure::InvalidInput);
        if (++result.sequence == 0) ++result.sequence;
        command.sequence = result.sequence;
        result.state = State::Pending;
        result.result = agenda::AgendaResult::Ok;
        result.event_id = 0;
        pending = command;
        has_command = busy = true;
        wake.notify_all();
        return ::ui::UiActionResult::success();
    }
    void run()
    {
        View current;
        presentation::CommandResult terminal;
        bool terminal_known = false;
        try
        {
            LinuxAgendaStore store(options.database);
            LinuxAgendaClock system;
            Clock clock;
            product_composition::AgendaComposition composition(store, clock);
            ::ui::workspace::PresentationWorkspace graph;
            composition.bind(graph);
            bool opened = false, dirty = true;
            auto retry = std::chrono::steady_clock::time_point::min();
            auto health_check = retry;
            for (;;)
            {
                const bool previous_ready = current.ready;
                terminal_known = false;
                Command command;
                bool have_command = false, allow = false, refresh = false;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    wake.wait_for(lock, std::chrono::milliseconds(100), [&]
                                  { return stopping || has_command || refresh_requested || presentation_changed; });
                    if (stopping && !has_command) break;
                    if (has_command)
                    {
                        command = pending;
                        has_command = false;
                        have_command = true;
                    }
                    allow = presentation_allowed;
                    refresh = refresh_requested;
                    refresh_requested = presentation_changed = false;
                }
                const auto now = std::chrono::steady_clock::now();
                bool just_opened = false;
                if (opened && (now >= health_check || refresh))
                {
                    opened = store.begin() == Result::Ok;
                    health_check = now + std::chrono::seconds(1);
                    if (!opened)
                    {
                        dirty = true;
                        retry = health_check;
                    }
                }
                if (!opened && now >= retry)
                {
                    opened = store.begin() == Result::Ok;
                    just_opened = opened;
                    dirty = true;
                    retry = now + std::chrono::seconds(1);
                    health_check = retry;
                }
                clock.value = options.clock ? options.clock->sample() : system.sample();
                if (opened && (dirty || refresh) && (now >= retry || just_opened || refresh))
                {
                    const auto data = project(store, options);
                    if (data)
                    {
                        current.data = data;
                        dirty = false;
                        ++current.revision;
                    }
                    else
                    {
                        dirty = true;
                        retry = now + std::chrono::seconds(1);
                    }
                }
                current.ready = opened && current.data && !dirty;
                composition.setStorageReady(current.ready);
                bool record_command = false;
                ::ui::UiActionResult admission;
                if (have_command)
                {
                    switch (command.kind)
                    {
                    case Kind::Save:
                        admission = graph.agenda->save(command.event);
                        record_command = true;
                        break;
                    case Kind::Remove:
                        admission = graph.agenda->remove(command.id);
                        record_command = true;
                        break;
                    case Kind::Dismiss:
                        admission = graph.agenda->dismissReminder(command.id);
                        break;
                    case Kind::Snooze:
                        admission = graph.agenda->snoozeReminder(command.id);
                        break;
                    }
                }
                composition.tick(allow);
                auto outcome = graph.agenda->commandResult();
                if (have_command)
                {
                    if (!admission.ok)
                    {
                        outcome = {};
                        outcome.state = State::Failed;
                        outcome.result = agenda::AgendaResult::StorageError;
                    }
                    outcome.sequence = command.sequence;
                    terminal = outcome;
                    terminal_known = true;
                }
                if (have_command && admission.ok && record_command && outcome.state == State::Succeeded)
                {
                    current.data = project(store, options);
                    dirty = !current.data;
                    ++current.revision;
                    current.ready = !dirty;
                    if (dirty)
                    {
                        composition.setStorageReady(false);
                        retry = now + std::chrono::seconds(1);
                    }
                }
                if (current.clock.revision != clock.value.revision || current.clock.valid != clock.value.valid ||
                    current.clock.calendar_seconds / 86400 != clock.value.calendar_seconds / 86400) ++current.revision;
                if (current.ready != previous_ready) ++current.revision;
                current.clock = clock.value;
                current.reminder = graph.agenda_reminders->reminderSnapshot();
                // Publish the committed read projection before making the
                // command terminal. No mutex spans storage, clock or factory I/O.
                publish(current, have_command ? &terminal : nullptr);
            }
        }
        catch (...)
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (busy)
            {
                if (terminal_known) result = terminal;
                else
                {
                    result.state = State::Failed;
                    result.result = agenda::AgendaResult::StorageError;
                }
                busy = has_command = false;
            }
        }
        active = false;
    }
    LinuxAgendaOptions options;
    std::shared_ptr<const View> published;
    std::atomic_bool active{true};
    mutable std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false, busy = false, has_command = false;
    bool presentation_allowed = false, presentation_changed = false, refresh_requested = false;
    Command pending;
    presentation::CommandResult result;
    std::thread worker;
};
LinuxAgendaSource::LinuxAgendaSource(LinuxAgendaOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {}
LinuxAgendaSource::~LinuxAgendaSource() = default;
void LinuxAgendaSource::shutdown() { impl_->stop(); }
void LinuxAgendaSource::setPresentationAllowed(bool allow)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (allow != impl_->presentation_allowed)
    {
        impl_->presentation_allowed = allow;
        impl_->presentation_changed = true;
        impl_->wake.notify_all();
    }
}
void LinuxAgendaSource::requestRefresh()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->refresh_requested = true;
    impl_->wake.notify_all();
}
::agenda::ClockSample LinuxAgendaSource::currentTime() const
{
    auto clock = impl_->view()->clock;
    clock.valid = clock.valid && impl_->active;
    return clock;
}
void LinuxAgendaSource::snapshot(const presentation::AgendaRequest& request, presentation::AgendaSnapshot& out)
{
    const auto view = impl_->view();
    const bool active = impl_->active;
    Reader reader(view, active);
    reader.model.snapshot(request, out);
    out.revision = view->revision + (active ? 0u : 1u);
}
::agenda::AgendaResult LinuxAgendaSource::detail(uint32_t id, ::agenda::EventRecord& out)
{
    Reader reader(impl_->view(), impl_->active);
    return reader.model.detail(id, out);
}
bool LinuxAgendaSource::newDraft(presentation::AgendaEditorModel& out) const
{
    Reader reader(impl_->view(), impl_->active);
    return reader.model.newDraft(out);
}
void LinuxAgendaSource::month(uint16_t year, uint8_t month, presentation::MonthSnapshot& out)
{
    Reader reader(impl_->view(), impl_->active);
    reader.model.month(year, month, out);
}
presentation::CommandResult LinuxAgendaSource::commandResult() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->result;
}
::ui::UiActionResult LinuxAgendaSource::save(const ::agenda::EventRecord& draft)
{
    Impl::Command command;
    command.event = draft;
    return impl_->enqueue(command);
}
::ui::UiActionResult LinuxAgendaSource::remove(uint32_t id)
{
    if (!id) return ::ui::UiActionResult::fail(Failure::InvalidInput);
    Impl::Command command;
    command.kind = Impl::Kind::Remove;
    command.id = id;
    return impl_->enqueue(command);
}
::ui::UiActionResult LinuxAgendaSource::dismissReminder(uint32_t revision)
{
    Impl::Command command;
    command.kind = Impl::Kind::Dismiss;
    command.id = revision;
    return impl_->enqueue(command);
}
::ui::UiActionResult LinuxAgendaSource::snoozeReminder(uint32_t revision)
{
    Impl::Command command;
    command.kind = Impl::Kind::Snooze;
    command.id = revision;
    return impl_->enqueue(command);
}
presentation::ReminderSnapshot LinuxAgendaSource::reminderSnapshot() const
{
    auto reminder = impl_->view()->reminder;
    if (!impl_->active) reminder.reminder = {};
    return reminder;
}
::ui::map::MapMarkerStatus LinuxAgendaSource::markerStatus() const
{
    const auto view = impl_->view();
    const bool active = impl_->active;
    Reader reader(view, active);
    auto status = reader.model.markerStatus();
    status.revision = view->revision + (active ? 0u : 1u);
    return status;
}
bool LinuxAgendaSource::visitMarkers(::ui::map::MapMarkerVisitor visitor, void* context, void* scratch, size_t size)
{
    Reader reader(impl_->view(), impl_->active);
    return reader.model.visitMarkers(visitor, context, scratch, size);
}
} // namespace platform::linux_runtime
