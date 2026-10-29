#pragma once
#include "agenda/ports/agenda_clock.h"
#include "ui_presentation/agenda/agenda_action_sink.h"
#include "ui_presentation/agenda/agenda_reminder_source.h"
#include "ui_presentation/agenda/agenda_source.h"
#include "ui_presentation/map/map_marker_source.h"
#include <filesystem>
#include <functional>
#include <memory>

namespace platform::linux_runtime
{
struct LinuxAgendaOptions
{
    std::filesystem::path database;
    ::agenda::IAgendaClock* clock = nullptr; // Optional owner must outlive shutdown.
    // Worker-only factory; returned memory file must support sealing. Empty
    // selects the Linux implementation. The service takes ownership of the fd.
    std::function<int()> create_projection;
};
class LinuxAgendaSource final : public ::ui::agenda::IAgendaSource,
                                public ::ui::agenda::IAgendaActionSink,
                                public ::ui::agenda::IAgendaReminderSource,
                                public ::ui::map::IMapMarkerSource
{
  public:
    explicit LinuxAgendaSource(LinuxAgendaOptions);
    ~LinuxAgendaSource() override;
    void shutdown(); // Drains an accepted command, joins, then invalidates reads.
    void setPresentationAllowed(bool);
    void requestRefresh();
    ::agenda::ClockSample currentTime() const override;
    void snapshot(const ::ui::agenda::AgendaRequest&, ::ui::agenda::AgendaSnapshot&) override;
    ::agenda::AgendaResult detail(uint32_t, ::agenda::EventRecord&) override;
    bool newDraft(::ui::agenda::AgendaEditorModel&) const override;
    void month(uint16_t, uint8_t, ::ui::agenda::MonthSnapshot&) override;
    ::ui::agenda::CommandResult commandResult() const override;
    ::ui::UiActionResult save(const ::agenda::EventRecord&) override;
    ::ui::UiActionResult remove(uint32_t) override;
    ::ui::UiActionResult dismissReminder(uint32_t) override;
    ::ui::UiActionResult snoozeReminder(uint32_t) override;
    ::ui::agenda::ReminderSnapshot reminderSnapshot() const override;
    ::ui::map::MapMarkerStatus markerStatus() const override;
    bool visitMarkers(::ui::map::MapMarkerVisitor, void*, void*, size_t) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
