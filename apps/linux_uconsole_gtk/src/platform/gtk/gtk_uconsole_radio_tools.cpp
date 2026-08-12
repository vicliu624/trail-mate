#include "platform/gtk/gtk_uconsole_pages.h"
#include "platform/gtk/gtk_uconsole_shell.h"
#include "platform/gtk/gtk_uconsole_widgets.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <future>
#include <string>
#include <thread>
#include <utility>

#include "chat/infra/meshtastic/mt_region.h"
#include "platform/ui/capability_status.h"
#include "platform/ui/lora_runtime.h"
#include "platform/ui/pack_repository_runtime.h"
#include "platform/ui/sstv_runtime.h"
#include "platform/ui/walkie_runtime.h"

namespace trailmate::uconsole::gtk
{
namespace
{

constexpr std::size_t kSweepPointCount = 48;
constexpr auto kSweepSettleTime = std::chrono::milliseconds(12);

struct SweepPlan
{
    float start_mhz = 433.0F;
    float end_mhz = 434.8F;
    ::platform::ui::lora::ReceiveConfig receive{};
};

std::string capabilityLabel(const ::platform::ui::CapabilityStatus& status)
{
    std::string label = ::platform::ui::capability_state_label(status.state);
    if (status.message != nullptr && status.message[0] != '\0')
    {
        label += " — ";
        label += status.message;
    }
    return label;
}

bool capabilityNeedsAttention(const ::platform::ui::CapabilityStatus& status)
{
    return status.state == ::platform::ui::CapabilityState::Unsupported ||
           status.state == ::platform::ui::CapabilityState::Degraded ||
           status.state == ::platform::ui::CapabilityState::Error;
}

GtkWidget* buildRadioStatusRow(const char* tool,
                               const char* activity,
                               const std::string& capability,
                               bool needs_attention)
{
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(row, "runtime-status-row");
    gtk_widget_set_hexpand(row, TRUE);

    GtkWidget* tool_label = makeLabel(tool, "runtime-status-tool");
    gtk_widget_set_size_request(tool_label, 116, -1);
    gtk_box_append(GTK_BOX(row), tool_label);

    GtkWidget* activity_label = makeLabel(activity, "runtime-status-value");
    gtk_widget_set_size_request(activity_label, 92, -1);
    if (needs_attention)
    {
        gtk_widget_add_css_class(activity_label, "runtime-status-attention");
    }
    gtk_box_append(GTK_BOX(row), activity_label);

    GtkWidget* capability_label =
        makeLabel(capability.c_str(), "runtime-status-detail", true);
    gtk_widget_set_hexpand(capability_label, TRUE);
    gtk_box_append(GTK_BOX(row), capability_label);
    return row;
}

SweepPlan makeSweepPlan(const GtkUConsoleAppState& state)
{
    SweepPlan plan{};
    const auto& config = state.services.config();
    const auto& mesh = config.mesh_protocol == ::chat::MeshProtocol::Meshtastic
                           ? config.meshtastic_config
                       : config.mesh_protocol == ::chat::MeshProtocol::MeshCore
                           ? config.meshcore_config
                           : config.reticulumConfig();
    plan.receive.bw_khz = displayBandwidthKHz(mesh, config.mesh_protocol);
    plan.receive.sf = displaySpreadFactor(mesh, config.mesh_protocol);
    plan.receive.cr = displayCodingRate(mesh, config.mesh_protocol);
    plan.receive.tx_power = static_cast<std::int8_t>(displayTxPowerDbm(mesh));

    if (config.mesh_protocol == ::chat::MeshProtocol::Meshtastic)
    {
        auto region_code = static_cast<meshtastic_Config_LoRaConfig_RegionCode>(
            mesh.region);
        if (region_code ==
            meshtastic_Config_LoRaConfig_RegionCode_UNSET)
        {
            region_code = meshtastic_Config_LoRaConfig_RegionCode_CN;
        }
        if (const auto* region = ::chat::meshtastic::findRegion(region_code);
            region != nullptr)
        {
            plan.start_mhz = region->freq_start_mhz;
            plan.end_mhz = region->freq_end_mhz;
            return plan;
        }
    }

    const float center = displayFrequencyMhz(mesh);
    if (std::isfinite(center) && center > 0.0F)
    {
        plan.start_mhz = center - 1.0F;
        plan.end_mhz = center + 1.0F;
    }
    return plan;
}

GtkUConsoleAppState::RadioSweepResult runSweep(SweepPlan plan)
{
    GtkUConsoleAppState::RadioSweepResult result{};
    if (!::platform::ui::lora::acquire())
    {
        result.message = "LoRa receiver could not be acquired.";
        return result;
    }

    struct RadioReleaseGuard
    {
        ~RadioReleaseGuard()
        {
            ::platform::ui::lora::release();
        }
    } guard;

    result.points.reserve(kSweepPointCount);
    for (std::size_t index = 0; index < kSweepPointCount; ++index)
    {
        const float ratio = static_cast<float>(index) /
                            static_cast<float>(kSweepPointCount - 1);
        const float frequency =
            plan.start_mhz + ((plan.end_mhz - plan.start_mhz) * ratio);
        if (!::platform::ui::lora::configure_receive(frequency,
                                                     plan.receive))
        {
            result.message = "LoRa receive configuration failed during scan.";
            return result;
        }
        std::this_thread::sleep_for(kSweepSettleTime);
        const float rssi = ::platform::ui::lora::read_instant_rssi();
        if (std::isfinite(rssi))
        {
            result.points.push_back({frequency, rssi});
        }
    }

    result.ok = !result.points.empty();
    result.message = result.ok ? "Sweep complete; mesh radio configuration restored."
                               : "No valid RSSI samples were returned.";
    return result;
}

void drawSpectrum(GtkDrawingArea*,
                  cairo_t* cr,
                  int width,
                  int height,
                  gpointer data)
{
    const auto& state = *static_cast<GtkUConsoleAppState*>(data);
    cairo_set_source_rgb(cr, 0.035, 0.055, 0.075);
    cairo_paint(cr);

    constexpr double margin = 24.0;
    const double graph_width = std::max(1.0, width - (margin * 2.0));
    const double graph_height = std::max(1.0, height - (margin * 2.0));
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, 0.25, 0.45, 0.55, 0.35);
    for (int line = 0; line <= 4; ++line)
    {
        const double y = margin + (graph_height * line / 4.0);
        cairo_move_to(cr, margin, y);
        cairo_line_to(cr, margin + graph_width, y);
    }
    cairo_stroke(cr);

    if (state.radio_sweep_points.empty())
    {
        return;
    }

    cairo_set_line_width(cr, 2.2);
    cairo_set_source_rgb(cr, 0.20, 0.86, 0.73);
    for (std::size_t index = 0; index < state.radio_sweep_points.size(); ++index)
    {
        const auto& point = state.radio_sweep_points[index];
        const double x = margin + graph_width * static_cast<double>(index) /
                                      static_cast<double>(
                                          state.radio_sweep_points.size() - 1);
        const double normalized = std::clamp(
            (static_cast<double>(point.rssi_dbm) + 140.0) / 112.0,
            0.0,
            1.0);
        const double y = margin + graph_height * (1.0 - normalized);
        if (index == 0)
        {
            cairo_move_to(cr, x, y);
        }
        else
        {
            cairo_line_to(cr, x, y);
        }
    }
    cairo_stroke(cr);
}

void drawSstvFrame(GtkDrawingArea*,
                   cairo_t* cr,
                   int width,
                   int height,
                   gpointer)
{
    cairo_set_source_rgb(cr, 0.025, 0.035, 0.05);
    cairo_paint(cr);
    const std::uint16_t* pixels = ::platform::ui::sstv::framebuffer();
    const auto frame_width = ::platform::ui::sstv::frame_width();
    const auto frame_height = ::platform::ui::sstv::frame_height();
    if (pixels == nullptr || frame_width == 0 || frame_height == 0)
    {
        return;
    }

    const double scale = std::min(static_cast<double>(width) / frame_width,
                                  static_cast<double>(height) / frame_height);
    const double left = (width - (frame_width * scale)) / 2.0;
    const double top = (height - (frame_height * scale)) / 2.0;
    constexpr std::uint16_t sample_step = 3;
    for (std::uint16_t y = 0; y < frame_height; y += sample_step)
    {
        for (std::uint16_t x = 0; x < frame_width; x += sample_step)
        {
            const std::uint16_t rgb565 =
                pixels[static_cast<std::size_t>(y) * frame_width + x];
            const double red = ((rgb565 >> 11U) & 0x1FU) / 31.0;
            const double green = ((rgb565 >> 5U) & 0x3FU) / 63.0;
            const double blue = (rgb565 & 0x1FU) / 31.0;
            cairo_set_source_rgb(cr, red, green, blue);
            cairo_rectangle(cr,
                            left + (x * scale),
                            top + (y * scale),
                            sample_step * scale + 0.5,
                            sample_step * scale + 0.5);
            cairo_fill(cr);
        }
    }
}

void onStartSweepClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    if (state.radio_sweep_running)
    {
        return;
    }
    state.radio_sweep_running = true;
    state.radio_tools_status = "Scanning the configured radio band...";
    const SweepPlan plan = makeSweepPlan(state);
    state.radio_sweep_future = std::async(
        std::launch::async,
        [plan]()
        { return runSweep(plan); });
    refreshUi(state);
}

void onSstvToggleClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    if (::platform::ui::sstv::is_active())
    {
        ::platform::ui::sstv::stop();
        state.radio_tools_status = "SSTV receiver stopped.";
    }
    else
    {
        state.radio_tools_status = ::platform::ui::sstv::start()
                                       ? "SSTV receiver started."
                                       : ::platform::ui::sstv::last_error();
    }
    refreshUi(state);
}

void onWalkieToggleClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    if (::platform::ui::walkie::is_active())
    {
        ::platform::ui::walkie::set_ptt(false);
        ::platform::ui::walkie::stop();
        state.radio_tools_status = "Walkie session stopped.";
    }
    else
    {
        state.radio_tools_status = ::platform::ui::walkie::start()
                                       ? "Walkie session started."
                                       : ::platform::ui::walkie::last_error();
    }
    refreshUi(state);
}

void onWalkiePttClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    const auto status = ::platform::ui::walkie::get_status();
    ::platform::ui::walkie::set_ptt(!status.tx);
    refreshUi(state);
}

void onWalkieMonitorClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    (void)::platform::ui::walkie::set_monitor_enabled(
        !::platform::ui::walkie::monitor_enabled());
    refreshUi(state);
}

void pollSweep(GtkUConsoleAppState& state)
{
    if (!state.radio_sweep_running || !state.radio_sweep_future.valid() ||
        state.radio_sweep_future.wait_for(std::chrono::seconds(0)) !=
            std::future_status::ready)
    {
        return;
    }
    auto result = state.radio_sweep_future.get();
    state.radio_sweep_running = false;
    state.radio_tools_status = std::move(result.message);
    if (result.ok)
    {
        state.radio_sweep_points = std::move(result.points);
    }
}

GtkWidget* launchRadioToolsLayout(GtkUConsoleAppState& state)
{
    return buildDetailsWorkspace(
        "Radio tools",
        "Desktop workbench for spectrum inspection, SSTV, and walkie controls.",
        &state.radio_tools_page_box);
}

void refreshRadioToolsLogic(GtkUConsoleAppState& state,
                            const GtkUConsoleRefreshSnapshot&)
{
    pollSweep(state);
    clearBox(state.radio_tools_page_box);

    const auto lora_capability = ::platform::ui::lora::capability_status();
    const auto sstv_capability = ::platform::ui::sstv::capability_status();
    const auto walkie_capability = ::platform::ui::walkie::capability_status();
    const auto sstv_status = ::platform::ui::sstv::get_status();
    const auto walkie_status = ::platform::ui::walkie::get_status();

    GtkWidget* status_table = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(status_table, "runtime-status-table");
    gtk_box_append(GTK_BOX(status_table),
                   buildRadioStatusRow(
                       "LoRa sweep",
                       state.radio_sweep_running ? "Scanning" : "Ready",
                       capabilityLabel(lora_capability),
                       capabilityNeedsAttention(lora_capability)));
    gtk_box_append(
        GTK_BOX(status_table),
        buildRadioStatusRow("SSTV",
                            ::platform::ui::sstv::is_active() ? "Receiving"
                                                              : "Idle",
                            capabilityLabel(sstv_capability),
                            capabilityNeedsAttention(sstv_capability)));
    gtk_box_append(
        GTK_BOX(status_table),
        buildRadioStatusRow("Walkie",
                            walkie_status.tx
                                ? "Transmitting"
                                : (walkie_status.active ? "Listening"
                                                        : "Stopped"),
                            capabilityLabel(walkie_capability),
                            capabilityNeedsAttention(walkie_capability)));
    gtk_box_append(GTK_BOX(state.radio_tools_page_box), status_table);

    GtkWidget* spectrum = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(spectrum, "radio-sweep-workspace");
    GtkWidget* spectrum_header =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* spectrum_title = makeLabel("Energy sweep", "pane-heading");
    gtk_widget_set_hexpand(spectrum_title, TRUE);
    gtk_box_append(GTK_BOX(spectrum_header), spectrum_title);
    GtkWidget* sweep = gtk_button_new_with_label(
        state.radio_sweep_running ? "Scanning..." : "Run sweep");
    gtk_widget_add_css_class(sweep, "nav-button");
    gtk_widget_set_sensitive(
        sweep,
        !state.radio_sweep_running && ::platform::ui::lora::is_supported());
    g_signal_connect(sweep,
                     "clicked",
                     G_CALLBACK(onStartSweepClicked),
                     &state);
    gtk_box_append(GTK_BOX(spectrum_header), sweep);
    gtk_box_append(GTK_BOX(spectrum), spectrum_header);
    GtkWidget* graph = gtk_drawing_area_new();
    gtk_widget_add_css_class(graph, "radio-spectrum");
    gtk_widget_set_size_request(graph, -1, 210);
    gtk_widget_set_hexpand(graph, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(graph),
                                   drawSpectrum,
                                   &state,
                                   nullptr);
    gtk_box_append(GTK_BOX(spectrum), graph);
    if (!state.radio_sweep_points.empty())
    {
        const auto strongest = std::max_element(
            state.radio_sweep_points.begin(),
            state.radio_sweep_points.end(),
            [](const auto& lhs, const auto& rhs)
            { return lhs.rssi_dbm < rhs.rssi_dbm; });
        char summary[128] = {};
        std::snprintf(summary,
                      sizeof(summary),
                      "%.3f–%.3f MHz / strongest %.3f MHz at %.1f dBm",
                      static_cast<double>(
                          state.radio_sweep_points.front().frequency_mhz),
                      static_cast<double>(
                          state.radio_sweep_points.back().frequency_mhz),
                      static_cast<double>(strongest->frequency_mhz),
                      static_cast<double>(strongest->rssi_dbm));
        gtk_box_append(GTK_BOX(spectrum),
                       makeLabel(summary, "row-meta", true));
    }
    gtk_box_append(GTK_BOX(state.radio_tools_page_box), spectrum);

    GtkWidget* lower = makeWorkbench(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(lower, "radio-tool-columns");
    GtkWidget* sstv = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(sstv, "radio-tool-section");
    gtk_widget_set_hexpand(sstv, TRUE);
    GtkWidget* sstv_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* sstv_title = makeLabel("SSTV receiver", "pane-heading");
    gtk_widget_set_hexpand(sstv_title, TRUE);
    gtk_box_append(GTK_BOX(sstv_header), sstv_title);
    GtkWidget* sstv_toggle = gtk_button_new_with_label(
        ::platform::ui::sstv::is_active() ? "Stop" : "Start");
    gtk_widget_add_css_class(sstv_toggle, "nav-button");
    gtk_widget_set_sensitive(sstv_toggle,
                             ::platform::ui::sstv::is_supported());
    g_signal_connect(sstv_toggle,
                     "clicked",
                     G_CALLBACK(onSstvToggleClicked),
                     &state);
    gtk_box_append(GTK_BOX(sstv_header), sstv_toggle);
    gtk_box_append(GTK_BOX(sstv), sstv_header);
    GtkWidget* sstv_preview = gtk_drawing_area_new();
    gtk_widget_set_size_request(sstv_preview, 360, 230);
    gtk_widget_set_hexpand(sstv_preview, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(sstv_preview),
                                   drawSstvFrame,
                                   nullptr,
                                   nullptr);
    gtk_box_append(GTK_BOX(sstv), sstv_preview);
    gtk_box_append(GTK_BOX(sstv),
                   buildDetailRow(
                       "Progress",
                       std::to_string(static_cast<int>(
                           std::clamp(sstv_status.progress, 0.0F, 1.0F) *
                           100.0F)) +
                           "% / " + ::platform::ui::sstv::mode_name()));
    const char* saved_path = ::platform::ui::sstv::last_saved_path();
    gtk_box_append(GTK_BOX(sstv),
                   buildDetailRow("Last image",
                                  saved_path != nullptr && saved_path[0] != '\0'
                                      ? saved_path
                                      : "No decoded image yet"));

    GtkWidget* walkie = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(walkie, "radio-tool-section");
    gtk_widget_set_hexpand(walkie, TRUE);
    GtkWidget* walkie_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* walkie_title = makeLabel("Walkie", "pane-heading");
    gtk_widget_set_hexpand(walkie_title, TRUE);
    gtk_box_append(GTK_BOX(walkie_header), walkie_title);
    GtkWidget* walkie_toggle = gtk_button_new_with_label(
        walkie_status.active ? "Stop session" : "Start session");
    gtk_widget_add_css_class(walkie_toggle, "nav-button");
    gtk_widget_set_sensitive(walkie_toggle,
                             ::platform::ui::walkie::is_supported());
    g_signal_connect(walkie_toggle,
                     "clicked",
                     G_CALLBACK(onWalkieToggleClicked),
                     &state);
    gtk_box_append(GTK_BOX(walkie_header), walkie_toggle);
    gtk_box_append(GTK_BOX(walkie), walkie_header);
    gtk_box_append(GTK_BOX(walkie),
                   buildDetailRow("Frequency",
                                  std::to_string(walkie_status.freq_mhz) +
                                      " MHz"));
    gtk_box_append(GTK_BOX(walkie),
                   buildDetailRow("Levels",
                                  "RX " +
                                      std::to_string(walkie_status.rx_level) +
                                      " / TX " +
                                      std::to_string(walkie_status.tx_level)));
    gtk_box_append(GTK_BOX(walkie),
                   buildDetailRow("Volume",
                                  std::to_string(
                                      ::platform::ui::walkie::volume())));
    GtkWidget* walkie_actions =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* ptt = gtk_button_new_with_label(walkie_status.tx
                                                   ? "Release PTT"
                                                   : "Press PTT");
    GtkWidget* monitor = gtk_button_new_with_label(
        walkie_status.monitor_enabled ? "Monitor on" : "Monitor off");
    gtk_widget_add_css_class(ptt, "nav-button");
    gtk_widget_add_css_class(monitor, "nav-button");
    gtk_widget_set_sensitive(ptt, walkie_status.active);
    gtk_widget_set_sensitive(monitor, walkie_status.active);
    g_signal_connect(ptt,
                     "clicked",
                     G_CALLBACK(onWalkiePttClicked),
                     &state);
    g_signal_connect(monitor,
                     "clicked",
                     G_CALLBACK(onWalkieMonitorClicked),
                     &state);
    gtk_box_append(GTK_BOX(walkie_actions), ptt);
    gtk_box_append(GTK_BOX(walkie_actions), monitor);
    gtk_box_append(GTK_BOX(walkie), walkie_actions);
    gtk_box_append(GTK_BOX(lower), sstv);
    gtk_box_append(GTK_BOX(lower), walkie);
    gtk_box_append(GTK_BOX(state.radio_tools_page_box), lower);

    if (!state.radio_tools_status.empty())
    {
        gtk_box_append(GTK_BOX(state.radio_tools_page_box),
                       makeLabel(state.radio_tools_status.c_str(),
                                 "settings-status",
                                 true));
    }
}

void hideRadioTools(GtkUConsoleAppState&)
{
    ::platform::ui::walkie::set_ptt(false);
}

void destroyRadioTools(GtkUConsoleAppState& state)
{
    ::platform::ui::walkie::set_ptt(false);
    ::platform::ui::walkie::stop();
    ::platform::ui::sstv::stop();
    if (state.radio_sweep_future.valid())
    {
        state.radio_sweep_future.wait();
        (void)state.radio_sweep_future.get();
    }
    state.radio_sweep_running = false;
}

void loadExtensionCatalog(GtkUConsoleAppState& state)
{
    if (state.extensions_loaded)
    {
        return;
    }
    state.extension_catalog.clear();
    std::string error{};
    state.extensions_loaded =
        ::ui::runtime::packs::fetch_catalog(state.extension_catalog, error);
    state.extensions_status = state.extensions_loaded
                                  ? std::to_string(
                                        state.extension_catalog.size()) +
                                        " packages available locally."
                                  : error;
}

const char* packageIdFromButton(GtkButton* button)
{
    return static_cast<const char*>(
        g_object_get_data(G_OBJECT(button), "trailmate-package-id"));
}

void onExtensionActionClicked(GtkButton* button, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    const char* package_id = packageIdFromButton(button);
    const auto it = std::find_if(
        state.extension_catalog.begin(),
        state.extension_catalog.end(),
        [package_id](const auto& package)
        { return package.id == (package_id == nullptr ? "" : package_id); });
    if (it == state.extension_catalog.end())
    {
        return;
    }

    std::string error{};
    const bool ok = it->installed
                        ? ::ui::runtime::packs::uninstall_package(*it, error)
                        : ::ui::runtime::packs::start_install_package(*it,
                                                                      error);
    state.extensions_status = ok
                                  ? it->display_name +
                                        (it->installed ? " uninstalled."
                                                       : " installed.")
                                  : error;
    state.extensions_loaded = false;
    refreshUi(state);
}

GtkWidget* launchExtensionsLayout(GtkUConsoleAppState& state)
{
    return buildDetailsWorkspace(
        "Extensions",
        "Desktop package workspace backed by the existing Trail Mate pack repository.",
        &state.extensions_page_box);
}

void refreshExtensionsLogic(GtkUConsoleAppState& state,
                            const GtkUConsoleRefreshSnapshot&)
{
    loadExtensionCatalog(state);
    clearBox(state.extensions_page_box);

    std::size_t installed = 0;
    for (const auto& package : state.extension_catalog)
    {
        installed += package.installed ? 1U : 0U;
    }
    GtkWidget* catalog_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(catalog_header, "list-toolbar");
    GtkWidget* catalog_title =
        makeLabel("Available packages", "pane-heading");
    gtk_widget_set_hexpand(catalog_title, TRUE);
    gtk_box_append(GTK_BOX(catalog_header), catalog_title);
    gtk_box_append(GTK_BOX(catalog_header),
                   makeLabel((std::to_string(state.extension_catalog.size()) +
                              " catalog / " + std::to_string(installed) +
                              " installed")
                                 .c_str(),
                             "row-meta"));
    gtk_box_append(GTK_BOX(state.extensions_page_box), catalog_header);

    GtkWidget* catalog = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(catalog, "extension-list");
    if (state.extension_catalog.empty())
    {
        gtk_box_append(GTK_BOX(catalog),
                       makeLabel("No packages are available.",
                                 "empty-state"));
    }
    for (const auto& package : state.extension_catalog)
    {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_add_css_class(row, "extension-list-row");
        GtkWidget* details = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_hexpand(details, TRUE);
        gtk_box_append(GTK_BOX(details),
                       makeLabel(package.display_name.c_str(),
                                 "row-title"));
        gtk_box_append(GTK_BOX(details),
                       makeLabel((package.version + " / " +
                                  package.package_type + " / " +
                                  formatBytes(package.archive_size_bytes))
                                     .c_str(),
                                 "row-meta"));
        gtk_box_append(GTK_BOX(details),
                       makeLabel(package.summary.c_str(),
                                 "row-meta",
                                 true));
        gtk_box_append(GTK_BOX(row), details);
        GtkWidget* action = gtk_button_new_with_label(
            package.installed ? "Uninstall" : "Install");
        gtk_widget_add_css_class(action, "nav-button");
        g_object_set_data_full(G_OBJECT(action),
                               "trailmate-package-id",
                               g_strdup(package.id.c_str()),
                               g_free);
        g_signal_connect(action,
                         "clicked",
                         G_CALLBACK(onExtensionActionClicked),
                         &state);
        gtk_box_append(GTK_BOX(row), action);
        gtk_box_append(GTK_BOX(catalog), row);
    }
    gtk_box_append(GTK_BOX(state.extensions_page_box), catalog);
    if (!state.extensions_status.empty())
    {
        gtk_box_append(GTK_BOX(state.extensions_page_box),
                       makeLabel(state.extensions_status.c_str(),
                                 "settings-status",
                                 true));
    }
}

} // namespace

GtkUConsolePageLifecycle makeRadioToolsPageLifecycle()
{
    return {.name = "radio-tools",
            .title = "Radio tools",
            .onLaunch = launchRadioToolsLayout,
            .onShow = nullptr,
            .onHide = hideRadioTools,
            .onRefresh = refreshRadioToolsLogic,
            .onDestroy = destroyRadioTools};
}

GtkUConsolePageLifecycle makeExtensionsPageLifecycle()
{
    return {.name = "extensions",
            .title = "Extensions",
            .onLaunch = launchExtensionsLayout,
            .onShow = nullptr,
            .onHide = nullptr,
            .onRefresh = refreshExtensionsLogic,
            .onDestroy = nullptr};
}

} // namespace trailmate::uconsole::gtk
