#include "platform/gtk/gtk_uconsole_pages.h"
#include "platform/gtk/gtk_uconsole_shell.h"
#include "platform/gtk/gtk_uconsole_widgets.h"

#include <ctime>
#include <string>

namespace trailmate::uconsole::gtk
{
namespace
{

std::string formatLogTimestamp(std::uint64_t timestamp_ms)
{
    if (timestamp_ms == 0)
    {
        return "--";
    }

    const std::time_t seconds =
        static_cast<std::time_t>(timestamp_ms / 1000ULL);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);
    return buffer;
}

} // namespace

void onLogsSourceGpsClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    state.logs_source = ::platform::linux_runtime::PacketLogSource::Gps;
    refreshUi(state);
}

void onLogsSourceLoraClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    state.logs_source = ::platform::linux_runtime::PacketLogSource::Lora;
    refreshUi(state);
}

void onLogsSourceMqttClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    state.logs_source = ::platform::linux_runtime::PacketLogSource::Mqtt;
    refreshUi(state);
}
GtkWidget* buildPacketLogEntry(
    const ::platform::linux_runtime::PacketLogEntry& entry)
{
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(row, "log-entry");
    gtk_widget_set_hexpand(row, TRUE);

    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(header, "log-entry-header");
    gtk_widget_set_hexpand(header, TRUE);
    gtk_box_append(GTK_BOX(header),
                   makeLabel(formatLogTimestamp(entry.timestamp_ms).c_str(),
                             "log-time"));
    gtk_box_append(GTK_BOX(header),
                   makeLabel(::platform::linux_runtime::packet_log_source_label(
                                 entry.source),
                             "log-source"));
    gtk_box_append(GTK_BOX(header),
                   makeLabel(::platform::linux_runtime::
                                 packet_log_direction_label(entry.direction),
                             "log-direction"));

    GtkWidget* title_label = makeLabel(entry.title.c_str(), "row-title", true);
    gtk_widget_set_hexpand(title_label, TRUE);
    gtk_box_append(GTK_BOX(header), title_label);
    gtk_box_append(GTK_BOX(row), header);

    gtk_box_append(GTK_BOX(row),
                   makeLabel(entry.summary.c_str(), "row-meta", true));

    GtkWidget* segment_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(segment_box, "log-segments");
    gtk_widget_set_hexpand(segment_box, TRUE);
    for (const auto& segment : entry.segments)
    {
        const std::string text = segment.label.empty()
                                     ? segment.text
                                     : segment.label + ": " + segment.text;
        GtkWidget* label = makeLabel(
            text.c_str(),
            ::platform::linux_runtime::packet_log_segment_class(segment.kind),
            true);
        constrainLabelWidth(label, 118);
        gtk_box_append(GTK_BOX(segment_box), label);
    }
    gtk_box_append(GTK_BOX(row), segment_box);

    if (!entry.raw_hex.empty())
    {
        GtkWidget* raw = makeLabel(entry.raw_hex.c_str(), "log-hex", true);
        constrainLabelWidth(raw, 118);
        gtk_box_append(GTK_BOX(row), raw);
    }
    return row;
}

static void refreshLogsPage(GtkUConsoleAppState& state)
{
    clearBox(state.logs_page_box);
    const bool lora_active =
        state.logs_source == ::platform::linux_runtime::PacketLogSource::Lora;
    const bool mqtt_active =
        state.logs_source == ::platform::linux_runtime::PacketLogSource::Mqtt;
    const bool gps_active =
        state.logs_source == ::platform::linux_runtime::PacketLogSource::Gps;
    if (lora_active)
        gtk_widget_add_css_class(state.logs_source_lora, "nav-button-active");
    else
        gtk_widget_remove_css_class(state.logs_source_lora,
                                    "nav-button-active");
    if (mqtt_active)
        gtk_widget_add_css_class(state.logs_source_mqtt, "nav-button-active");
    else
        gtk_widget_remove_css_class(state.logs_source_mqtt,
                                    "nav-button-active");
    if (gps_active)
        gtk_widget_add_css_class(state.logs_source_gps, "nav-button-active");
    else
        gtk_widget_remove_css_class(state.logs_source_gps,
                                    "nav-button-active");

    const auto entries = ::platform::linux_runtime::recent_packet_logs(
        state.logs_source, 80);
    if (entries.empty())
    {
        gtk_box_append(GTK_BOX(state.logs_page_box),
                       makeLabel("No packet logs yet.", "empty-state"));
        return;
    }
    for (const auto& entry : entries)
    {
        gtk_box_append(GTK_BOX(state.logs_page_box),
                       buildPacketLogEntry(entry));
    }
}

void refreshLogsLogic(GtkUConsoleAppState& state,
                      const GtkUConsoleRefreshSnapshot&)
{
    refreshLogsPage(state);
}

GtkUConsolePageLifecycle makeLogsPageLifecycle()
{
    return {.name = "logs",
            .title = "Logs",
            .onLaunch = launchLogsLayout,
            .onShow = nullptr,
            .onHide = nullptr,
            .onRefresh = refreshLogsLogic,
            .onDestroy = nullptr};
}

} // namespace trailmate::uconsole::gtk
