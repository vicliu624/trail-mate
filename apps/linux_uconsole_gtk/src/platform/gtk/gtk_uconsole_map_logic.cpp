#include "platform/gtk/gtk_uconsole_layout_spec.h"
#include "platform/gtk/gtk_uconsole_mqtt_settings.h"
#include "platform/gtk/gtk_uconsole_pages.h"
#include "platform/gtk/gtk_uconsole_shell.h"
#include "platform/gtk/gtk_uconsole_widgets.h"
#include "sys/clock.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <future>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace trailmate::uconsole::gtk
{

std::string tileKey(const ::platform::linux_runtime::MapTileId& tile)
{
    std::ostringstream out;
    out << static_cast<int>(tile.source) << ':' << tile.z << ':' << tile.x
        << ':' << tile.y;
    return out.str();
}

int mapFetchRetryDelaySeconds(unsigned attempts)
{
    const unsigned retry_index =
        attempts > 0U ? std::min<unsigned>(attempts - 1U, 6U) : 0U;
    const int delay = kMapFetchRetryBaseSeconds << retry_index;
    return std::clamp(delay,
                      kMapFetchRetryBaseSeconds,
                      kMapFetchRetryMaxSeconds);
}

std::string mapGridSignature(const MapWorkspaceSnapshot& snapshot)
{
    std::ostringstream out;
    out << snapshot.source_label << ':' << snapshot.zoom << ':'
        << snapshot.columns << 'x' << snapshot.rows << ':'
        << snapshot.center_tile_index << ':'
        << (snapshot.contour_enabled ? 'C' : '-');
    for (const auto& tile : snapshot.tiles)
    {
        out << '|' << tileKey(tile.id) << ':'
            << (tile.available ? '1' : '0') << ':'
            << tile.path.generic_string();
    }
    if (snapshot.contour_enabled)
    {
        for (const auto& contour : snapshot.contour_tiles)
        {
            out << "|c" << contour.base_tile_index << ':'
                << ::platform::linux_runtime::map_contour_profile_key(
                       contour.id.profile)
                << ':' << contour.id.z << ':' << contour.id.x << ':'
                << contour.id.y << ':' << (contour.available ? '1' : '0')
                << ':' << contour.path.generic_string();
        }
    }
    return out.str();
}

std::vector<::platform::linux_runtime::MapContourTileId> missingContourTiles(
    const MapWorkspaceSnapshot& snapshot)
{
    std::vector<::platform::linux_runtime::MapContourTileId> out;
    if (!snapshot.contour_enabled)
    {
        return out;
    }
    out.reserve(snapshot.contour_missing_count);
    for (const auto& tile : snapshot.contour_tiles)
    {
        if (!tile.available)
        {
            out.push_back(tile.id);
        }
    }
    return out;
}
void onMapSourceClicked(GtkButton* button, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    const guint source_value =
        GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button),
                                           "trailmate-source"));
    state.map_model.setSource(
        ::platform::linux_runtime::sanitize_map_base_source(source_value));
    state.map_failed_tiles.clear();
    state.map_fetch_status.clear();
    refreshUi(state);
}

void onMapZoomInClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    state.map_model.zoomIn();
    state.map_failed_tiles.clear();
    refreshUi(state);
}

void onMapZoomOutClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    state.map_model.zoomOut();
    state.map_failed_tiles.clear();
    refreshUi(state);
}

void onMapRecenterClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    state.map_model.clearManualCenter();
    state.map_failed_tiles.clear();
    state.map_fetch_status.clear();
    refreshUi(state);
}

std::string formatMapCoordinate(double lat, double lon)
{
    char buffer[64] = {};
    std::snprintf(buffer, sizeof(buffer), "%.5f, %.5f", lat, lon);
    return std::string(buffer);
}

std::string formatMapDecimal(double value)
{
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%.5f", value);
    return std::string(buffer);
}

std::string truncateMapStatusValue(const std::string& value)
{
    constexpr std::size_t kMaxChars = layout_spec::kMapStatusValueMaxChars;
    if (value.size() <= kMaxChars)
    {
        return value;
    }
    return value.substr(0, kMaxChars - 3U) + "...";
}

double mapDegToRad(double degrees)
{
    return degrees * 3.14159265358979323846 / 180.0;
}

double mapDistanceMeters(double lat_a,
                         double lon_a,
                         double lat_b,
                         double lon_b)
{
    constexpr double kEarthRadiusM = 6371000.0;
    const double d_lat = mapDegToRad(lat_b - lat_a);
    const double d_lon = mapDegToRad(lon_b - lon_a);
    const double a =
        std::sin(d_lat / 2.0) * std::sin(d_lat / 2.0) +
        std::cos(mapDegToRad(lat_a)) * std::cos(mapDegToRad(lat_b)) *
            std::sin(d_lon / 2.0) * std::sin(d_lon / 2.0);
    const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return kEarthRadiusM * c;
}

std::string formatMapDistance(double meters)
{
    if (!std::isfinite(meters) || meters < 0.0)
    {
        return "-";
    }
    char buffer[32] = {};
    if (meters < 1000.0)
    {
        std::snprintf(buffer, sizeof(buffer), "%.0f m", meters);
    }
    else
    {
        std::snprintf(buffer, sizeof(buffer), "%.2f km", meters / 1000.0);
    }
    return std::string(buffer);
}

std::string formatMapNodeAge(std::uint32_t timestamp)
{
    if (timestamp == 0)
    {
        return "seen: --";
    }
    const std::uint32_t now = sys::epoch_seconds_now();
    if (timestamp >= now)
    {
        return "seen: now";
    }
    const std::uint32_t age = now - timestamp;
    char buffer[32] = {};
    if (age < 60U)
    {
        std::snprintf(buffer, sizeof(buffer), "seen: now");
    }
    else if (age < 3600U)
    {
        std::snprintf(buffer,
                      sizeof(buffer),
                      "seen: %lum",
                      static_cast<unsigned long>(age / 60U));
    }
    else if (age < 86400U)
    {
        std::snprintf(buffer,
                      sizeof(buffer),
                      "seen: %luh",
                      static_cast<unsigned long>(age / 3600U));
    }
    else
    {
        std::snprintf(buffer,
                      sizeof(buffer),
                      "seen: %lud",
                      static_cast<unsigned long>(age / 86400U));
    }
    return buffer;
}

void updateMapMeasureStatus(GtkUConsoleAppState& state)
{
    if (state.map_measure_button != nullptr)
    {
        if (state.map_measure_enabled)
        {
            gtk_widget_add_css_class(state.map_measure_button,
                                     "nav-button-active");
        }
        else
        {
            gtk_widget_remove_css_class(state.map_measure_button,
                                        "nav-button-active");
        }
    }

    if (state.map_measure_status == nullptr)
    {
        return;
    }
    if (!state.map_measure_has_start)
    {
        setLabel(state.map_measure_status, "No measure points.");
        return;
    }

    std::string text = "A lat: " +
                       formatMapDecimal(state.map_measure_start_lat) +
                       "\nA lon: " +
                       formatMapDecimal(state.map_measure_start_lon);
    if (state.map_measure_has_end)
    {
        const double meters = mapDistanceMeters(state.map_measure_start_lat,
                                                state.map_measure_start_lon,
                                                state.map_measure_end_lat,
                                                state.map_measure_end_lon);
        text += "\nB lat: ";
        text += formatMapDecimal(state.map_measure_end_lat);
        text += "\nB lon: ";
        text += formatMapDecimal(state.map_measure_end_lon);
        text += "\ndist: ";
        text += formatMapDistance(meters);
    }
    else
    {
        text += "\nB pending";
    }
    setLabel(state.map_measure_status, text);
}

void setMapMeasurePoint(GtkUConsoleAppState& state,
                        double lat,
                        double lon,
                        bool end_point)
{
    if (end_point && state.map_measure_has_start)
    {
        state.map_measure_end_lat = lat;
        state.map_measure_end_lon = lon;
        state.map_measure_has_end = true;
    }
    else
    {
        state.map_measure_start_lat = lat;
        state.map_measure_start_lon = lon;
        state.map_measure_has_start = true;
        state.map_measure_has_end = false;
    }
    updateMapMeasureStatus(state);
    refreshMap(state);
}

void refreshAfterMapContextAction(GtkUConsoleAppState& state)
{
    if (state.map_context_popover != nullptr)
    {
        gtk_popover_popdown(GTK_POPOVER(state.map_context_popover));
    }
    state.map_failed_tiles.clear();
    state.map_fetch_status.clear();
    refreshUi(state);
}

void onMapContextCenterClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    if (!state.map_context_valid)
    {
        return;
    }

    state.map_model.centerOn(state.map_context_lat,
                             state.map_context_lon,
                             true);
    refreshAfterMapContextAction(state);
}

void onMapContextZoomInClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    if (!state.map_context_valid)
    {
        return;
    }

    state.map_model.zoomInAt(state.map_context_lat, state.map_context_lon);
    refreshAfterMapContextAction(state);
}

void onMapContextZoomOutClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    if (!state.map_context_valid)
    {
        return;
    }

    state.map_model.zoomOutAt(state.map_context_lat, state.map_context_lon);
    refreshAfterMapContextAction(state);
}

void onMapContextMeasureStartClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    if (!state.map_context_valid)
    {
        return;
    }
    state.map_measure_enabled = true;
    setMapMeasurePoint(state,
                       state.map_context_lat,
                       state.map_context_lon,
                       false);
    refreshAfterMapContextAction(state);
}

void onMapContextMeasureEndClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    if (!state.map_context_valid)
    {
        return;
    }
    state.map_measure_enabled = true;
    setMapMeasurePoint(state,
                       state.map_context_lat,
                       state.map_context_lon,
                       true);
    refreshAfterMapContextAction(state);
}

MapCoordinate coordinateAtPointer(GtkUConsoleAppState& state,
                                  double x,
                                  double y)
{
    const int width = state.map_marker_layer != nullptr
                          ? gtk_widget_get_width(state.map_marker_layer)
                          : 0;
    const int height = state.map_marker_layer != nullptr
                           ? gtk_widget_get_height(state.map_marker_layer)
                           : 0;
    const MapWorkspaceSnapshot snapshot = state.map_model.snapshot();
    return state.map_model.coordinateAtDisplayPoint(snapshot,
                                                    x,
                                                    y,
                                                    width,
                                                    height);
}

void onMapContextPressed(GtkGestureClick* gesture,
                         int,
                         double x,
                         double y,
                         gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    if (state.map_context_popover == nullptr)
    {
        return;
    }

    const MapCoordinate coordinate = coordinateAtPointer(state, x, y);
    if (!coordinate.valid)
    {
        state.map_context_valid = false;
        return;
    }

    state.map_context_valid = true;
    state.map_context_lat = coordinate.lat;
    state.map_context_lon = coordinate.lon;
    setLabel(state.map_context_label,
             "Point " + formatMapCoordinate(coordinate.lat, coordinate.lon));

    const GdkRectangle rect{static_cast<int>(std::lround(x)),
                            static_cast<int>(std::lround(y)),
                            1,
                            1};
    gtk_popover_set_pointing_to(GTK_POPOVER(state.map_context_popover), &rect);
    gtk_popover_popup(GTK_POPOVER(state.map_context_popover));
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

void onMapPrimaryPressed(GtkGestureClick* gesture,
                         int,
                         double x,
                         double y,
                         gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    if (!state.map_measure_enabled)
    {
        return;
    }
    const MapCoordinate coordinate = coordinateAtPointer(state, x, y);
    if (!coordinate.valid)
    {
        return;
    }
    setMapMeasurePoint(state,
                       coordinate.lat,
                       coordinate.lon,
                       state.map_measure_has_start &&
                           !state.map_measure_has_end);
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

void panMapFromDrag(GtkUConsoleAppState& state,
                    double offset_x,
                    double offset_y,
                    bool persist)
{
    if (!state.map_dragging && !persist)
    {
        return;
    }

    const int width = state.map_marker_layer != nullptr
                          ? gtk_widget_get_width(state.map_marker_layer)
                          : 0;
    const int height = state.map_marker_layer != nullptr
                           ? gtk_widget_get_height(state.map_marker_layer)
                           : 0;
    state.map_model.panByDisplayDelta(offset_x,
                                      offset_y,
                                      width,
                                      height,
                                      state.map_drag_start_lat,
                                      state.map_drag_start_lon,
                                      state.map_drag_start_zoom,
                                      persist);
    state.map_failed_tiles.clear();
    refreshMap(state);
}

void onMapDragBegin(GtkGestureDrag*, double, double, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    if (state.map_measure_enabled)
    {
        state.map_dragging = false;
        return;
    }
    const MapWorkspaceSnapshot snapshot = state.map_model.snapshot();
    if (!snapshot.has_center)
    {
        state.map_dragging = false;
        return;
    }

    state.map_dragging = true;
    state.map_drag_start_lat = snapshot.lat;
    state.map_drag_start_lon = snapshot.lon;
    state.map_drag_start_zoom = snapshot.zoom;
    state.map_fetch_status.clear();
}

void onMapDragUpdate(GtkGestureDrag*, double offset_x, double offset_y,
                     gpointer data)
{
    panMapFromDrag(*static_cast<GtkUConsoleAppState*>(data),
                   offset_x,
                   offset_y,
                   false);
}

void onMapDragEnd(GtkGestureDrag*, double offset_x, double offset_y,
                  gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    if (!state.map_dragging)
    {
        return;
    }
    panMapFromDrag(state, offset_x, offset_y, true);
    state.map_dragging = false;
}

void onMapMqttNodesToggled(GObject*, GParamSpec*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    if (state.map_mqtt_nodes == nullptr)
    {
        return;
    }
    const bool enabled =
        gtk_switch_get_active(GTK_SWITCH(state.map_mqtt_nodes));
    state.map_model.setShowMqttNodes(enabled);
    if (state.settings_map_mqtt_nodes != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_map_mqtt_nodes),
                              enabled);
    }
    refreshUi(state);
}

void onMapContourVisibleToggled(GObject*, GParamSpec*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    if (state.map_contour_visible == nullptr)
    {
        return;
    }

    const bool enabled =
        gtk_switch_get_active(GTK_SWITCH(state.map_contour_visible));
    state.map_model.setContourEnabled(enabled);
    if (state.settings_map_contour != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_map_contour),
                              enabled);
    }
    state.map_grid_signature.clear();
    state.contour_fill_status =
        enabled ? "Contours visible." : "Contours hidden.";
    refreshUi(state);
}

void onMapContourFillClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    if (state.contour_fill_job.future.valid())
    {
        state.contour_fill_status = "Contour fill already running.";
        refreshMap(state);
        return;
    }

    const MapWorkspaceSnapshot snapshot = state.map_model.snapshot();
    if (!snapshot.contour_enabled)
    {
        state.contour_fill_status = "Turn on contours before filling.";
        refreshMap(state, snapshot);
        return;
    }
    if (!snapshot.earthdata_token_configured)
    {
        state.contour_fill_status = "Earthdata token missing.";
        refreshMap(state, snapshot);
        return;
    }

    auto tiles = missingContourTiles(snapshot);
    if (tiles.empty())
    {
        state.contour_fill_status = "Visible contour tiles already cached.";
        refreshMap(state, snapshot);
        return;
    }

    state.contour_fill_status =
        "Filling visible contours: " + std::to_string(tiles.size()) +
        " tiles queued.";
    auto* model = &state.map_model;
    state.contour_fill_job.future =
        std::async(std::launch::async,
                   [model, tiles = std::move(tiles)]()
                   {
                       return model->ensureContourTiles(tiles);
                   });
    refreshMap(state, snapshot);
}

void onMapMeasureClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    state.map_measure_enabled = !state.map_measure_enabled;
    updateMapMeasureStatus(state);
    refreshMap(state);
}

void onMapMeasureClearClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    state.map_measure_has_start = false;
    state.map_measure_has_end = false;
    updateMapMeasureStatus(state);
    refreshMap(state);
}
void setActiveMapSourceButton(
    GtkWidget* button,
    ::platform::linux_runtime::MapBaseSource expected,
    const std::string& active_label)
{
    if (button == nullptr) return;
    const bool active =
        active_label ==
        ::platform::linux_runtime::map_base_source_label(expected);
    if (active)
        gtk_widget_add_css_class(button, "source-button-active");
    else
        gtk_widget_remove_css_class(button, "source-button-active");
}

void pollMapFetch(GtkUConsoleAppState& state)
{
    using namespace std::chrono_literals;
    if (state.map_fetch_jobs.empty())
    {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    std::size_t completed = 0;
    std::size_t failed = 0;
    std::size_t ready = 0;
    std::string last_failure{};

    for (std::size_t index = 0; index < state.map_fetch_jobs.size();)
    {
        auto& job = state.map_fetch_jobs[index];
        if (!job.future.valid() ||
            job.future.wait_for(0ms) != std::future_status::ready)
        {
            ++index;
            continue;
        }

        ::platform::linux_runtime::MapTileResult result{};
        try
        {
            result = job.future.get();
        }
        catch (const std::exception& ex)
        {
            result.status = ::platform::linux_runtime::MapTileStatus::Failed;
            result.tile = job.tile;
            result.message = ex.what();
        }
        catch (...)
        {
            result.status = ::platform::linux_runtime::MapTileStatus::Failed;
            result.tile = job.tile;
            result.message = "Unknown tile fetch error.";
        }

        const std::string key = job.key;
        state.map_inflight_tiles.erase(key);
        ++completed;

        if (result.status == ::platform::linux_runtime::MapTileStatus::Failed)
        {
            auto& retry = state.map_failed_tiles[key];
            ++retry.attempts;
            const int delay_seconds =
                mapFetchRetryDelaySeconds(retry.attempts);
            retry.next_retry = now + std::chrono::seconds(delay_seconds);
            retry.last_error = result.message;
            last_failure = result.message;
            ++failed;
        }
        else
        {
            state.map_failed_tiles.erase(key);
            ++ready;
        }

        state.map_fetch_jobs.erase(state.map_fetch_jobs.begin() +
                                   static_cast<std::ptrdiff_t>(index));
    }

    if (completed == 0)
    {
        return;
    }

    if (failed > 0)
    {
        state.map_fetch_status = "state: retry queued\nready: " +
                                 std::to_string(ready) +
                                 "\nfailed: " +
                                 std::to_string(failed);
        if (!last_failure.empty())
        {
            state.map_fetch_status +=
                "\nlast: " + truncateMapStatusValue(last_failure);
        }
    }
    else
    {
        state.map_fetch_status =
            "state: updated\nready: " + std::to_string(ready);
    }
    if (!state.map_fetch_jobs.empty())
    {
        state.map_fetch_status += "\nactive: " +
                                  std::to_string(state.map_fetch_jobs.size());
    }
}

void maybeStartMapFetch(GtkUConsoleAppState& state,
                        const MapWorkspaceSnapshot& snapshot)
{
    pollMapFetch(state);
    if (!snapshot.has_center ||
        state.map_fetch_jobs.size() >= kMaxConcurrentMapFetches)
    {
        return;
    }

    struct Candidate
    {
        int priority = 0;
        std::size_t index = 0;
        ::platform::linux_runtime::MapTileId tile{};
        std::string key{};
    };

    const auto now = std::chrono::steady_clock::now();
    const auto center =
        snapshot.center_tile_index < snapshot.tiles.size()
            ? snapshot.tiles[snapshot.center_tile_index].id
            : ::platform::linux_runtime::MapTileId{};
    std::vector<Candidate> candidates;
    candidates.reserve(snapshot.tiles.size());

    for (const auto& item : snapshot.tiles)
    {
        if (item.available)
        {
            continue;
        }
        const std::string key = tileKey(item.id);
        if (state.map_inflight_tiles.find(key) !=
            state.map_inflight_tiles.end())
        {
            continue;
        }

        const auto failed_it = state.map_failed_tiles.find(key);
        if (failed_it != state.map_failed_tiles.end() &&
            now < failed_it->second.next_retry)
        {
            continue;
        }

        const int distance =
            std::abs(item.id.x - center.x) + std::abs(item.id.y - center.y);
        candidates.push_back(Candidate{
            .priority =
                item.id.x == center.x && item.id.y == center.y ? -1
                                                               : distance,
            .index = candidates.size(),
            .tile = item.id,
            .key = key,
        });
    }

    std::sort(candidates.begin(),
              candidates.end(),
              [](const Candidate& lhs, const Candidate& rhs)
              {
                  if (lhs.priority != rhs.priority)
                  {
                      return lhs.priority < rhs.priority;
                  }
                  return lhs.index < rhs.index;
              });

    std::size_t started = 0;
    auto* model = &state.map_model;
    for (const auto& candidate : candidates)
    {
        if (state.map_fetch_jobs.size() >= kMaxConcurrentMapFetches)
        {
            break;
        }
        if (!state.map_inflight_tiles.insert(candidate.key).second)
        {
            continue;
        }

        const auto tile = candidate.tile;
        state.map_fetch_jobs.push_back(GtkUConsoleAppState::MapFetchJob{
            .key = candidate.key,
            .tile = tile,
            .future = std::async(std::launch::async,
                                 [model, tile]()
                                 {
                                     return model->ensureTile(tile);
                                 }),
        });
        ++started;
    }

    if (started > 0)
    {
        state.map_fetch_status = "state: fetching\nsource: " +
                                 truncateMapStatusValue(
                                     snapshot.source_label) +
                                 "\nqueued: " + std::to_string(started) +
                                 "\nactive: " +
                                 std::to_string(state.map_fetch_jobs.size());
    }
    else if (!state.map_failed_tiles.empty() && state.map_fetch_jobs.empty())
    {
        state.map_fetch_status = "state: retry pending\nfailed: " +
                                 std::to_string(state.map_failed_tiles.size());
    }
}

void pollContourFill(GtkUConsoleAppState& state)
{
    if (!state.contour_fill_job.future.valid())
    {
        return;
    }

    using namespace std::chrono_literals;
    if (state.contour_fill_job.future.wait_for(0ms) !=
        std::future_status::ready)
    {
        state.contour_fill_status = "Contour fill running.";
        return;
    }

    try
    {
        const auto result = state.contour_fill_job.future.get();
        state.contour_fill_status = result.message.empty()
                                        ? "Contour fill complete."
                                        : result.message;
    }
    catch (const std::exception& ex)
    {
        state.contour_fill_status =
            std::string("Contour fill failed: ") + ex.what();
    }
    catch (...)
    {
        state.contour_fill_status = "Contour fill failed.";
    }
    state.map_grid_signature.clear();
}

void onMapNodeMarkerClicked(GtkButton* button, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    const auto node_id = static_cast<std::uint32_t>(
        GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button),
                                           "trailmate-map-node-id")));
    state.map_selected_node_id =
        state.map_selected_node_id == node_id ? 0U : node_id;
    refreshMap(state);
}

GtkWidget* buildNodeMarker(GtkUConsoleAppState& state,
                           const MapNodeOverlayItem& node)
{
    const std::string label =
        node.via_mqtt ? ("MQ " + node.label) : node.label;
    GtkWidget* marker = gtk_button_new_with_label(label.c_str());
    gtk_widget_add_css_class(
        marker,
        node.via_mqtt ? "map-marker-mqtt" : "map-marker-local");
    gtk_widget_add_css_class(marker, "map-node-marker-button");
    std::ostringstream tip;
    tip << (node.via_mqtt ? "MQTT" : "Mesh") << " node 0x"
        << std::hex << std::uppercase << node.node_id << std::dec
        << " / lat " << node.lat << " / lon " << node.lon;
    gtk_widget_set_tooltip_text(marker, tip.str().c_str());
    g_object_set_data(G_OBJECT(marker),
                      "trailmate-map-node-id",
                      GUINT_TO_POINTER(static_cast<guint>(node.node_id)));
    g_signal_connect(marker,
                     "clicked",
                     G_CALLBACK(onMapNodeMarkerClicked),
                     &state);
    return marker;
}

GtkWidget* buildMapNodeBubble(const MapNodeOverlayItem& node)
{
    GtkWidget* bubble = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_add_css_class(bubble, "map-node-bubble");
    gtk_widget_set_size_request(bubble, 176, -1);

    GtkWidget* title_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* title = makeLabel(node.label.c_str(), "row-title", true);
    gtk_widget_set_hexpand(title, TRUE);
    gtk_box_append(GTK_BOX(title_row), title);
    gtk_box_append(GTK_BOX(title_row),
                   makeLabel(node.via_mqtt ? "MQTT" : "LoRa", "mini-chip"));
    gtk_box_append(GTK_BOX(bubble), title_row);

    char id[24] = {};
    std::snprintf(id,
                  sizeof(id),
                  "id: !%08lX",
                  static_cast<unsigned long>(node.node_id));
    gtk_box_append(GTK_BOX(bubble), makeLabel(id, "row-meta"));

    std::string position = "lat: " + formatMapDecimal(node.lat) +
                           "\nlon: " + formatMapDecimal(node.lon);
    if (node.has_altitude)
    {
        position += "\nalt: " + std::to_string(node.altitude_m) + " m";
    }
    gtk_box_append(GTK_BOX(bubble),
                   makeLabel(position.c_str(), "row-meta", true));

    std::string radio = formatMapNodeAge(node.last_seen);
    radio += "\nhops: ";
    radio += node.hops_away == 0xFF
                 ? "?"
                 : std::to_string(static_cast<unsigned>(node.hops_away));
    radio += "\nrssi: ";
    radio += std::isfinite(node.rssi)
                 ? std::to_string(static_cast<int>(std::lround(node.rssi)))
                 : "?";
    radio += " dBm\nsnr: ";
    if (std::isfinite(node.snr))
    {
        char snr[24] = {};
        std::snprintf(snr, sizeof(snr), "%.1f dB", node.snr);
        radio += snr;
    }
    else
    {
        radio += "?";
    }
    gtk_box_append(GTK_BOX(bubble),
                   makeLabel(radio.c_str(), "row-meta", true));
    return bubble;
}

double mapLongitudeToWorldPx(double lon, int zoom)
{
    constexpr double kTileSizePx = 256.0;
    const double tiles = static_cast<double>(1U << zoom);
    return ((lon + 180.0) / 360.0) * tiles * kTileSizePx;
}

double mapLatitudeToWorldPx(double lat, int zoom)
{
    constexpr double kTileSizePx = 256.0;
    constexpr double kMaxMercatorLat = 85.05112878;
    const double clamped_lat =
        std::clamp(lat, -kMaxMercatorLat, kMaxMercatorLat);
    const double lat_rad = mapDegToRad(clamped_lat);
    const double tiles = static_cast<double>(1U << zoom);
    const double mercator =
        std::log(std::tan(lat_rad) + (1.0 / std::cos(lat_rad)));
    return ((1.0 - mercator / 3.14159265358979323846) / 2.0) * tiles *
           kTileSizePx;
}

bool projectMapPoint(const MapWorkspaceSnapshot& snapshot,
                     double lat,
                     double lon,
                     double& out_x_fraction,
                     double& out_y_fraction)
{
    if (!snapshot.has_center || snapshot.tiles.empty() ||
        !std::isfinite(lat) || !std::isfinite(lon))
    {
        return false;
    }

    constexpr double kTileSizePx = 256.0;
    const auto top_left = snapshot.tiles.front().id;
    const double map_width_px =
        static_cast<double>(std::max<std::size_t>(1U, snapshot.columns)) *
        kTileSizePx;
    const double map_height_px =
        static_cast<double>(std::max<std::size_t>(1U, snapshot.rows)) *
        kTileSizePx;
    const double world_width_px =
        static_cast<double>(1U << snapshot.zoom) * kTileSizePx;
    const double left_px = static_cast<double>(top_left.x) * kTileSizePx;
    const double top_px = static_cast<double>(top_left.y) * kTileSizePx;

    double x = mapLongitudeToWorldPx(lon, snapshot.zoom) - left_px;
    if (x < 0.0)
    {
        x += world_width_px;
    }
    if (x > map_width_px && (x - world_width_px) >= 0.0)
    {
        x -= world_width_px;
    }
    const double y = mapLatitudeToWorldPx(lat, snapshot.zoom) - top_px;
    if (x < 0.0 || x > map_width_px || y < 0.0 || y > map_height_px)
    {
        return false;
    }
    out_x_fraction = map_width_px > 0.0 ? x / map_width_px : 0.0;
    out_y_fraction = map_height_px > 0.0 ? y / map_height_px : 0.0;
    return true;
}

GtkWidget* buildMeasureMarker(const char* label)
{
    GtkWidget* marker = makeLabel(label, "map-marker-measure");
    gtk_label_set_xalign(GTK_LABEL(marker), 0.5F);
    return marker;
}

void putMeasureMarker(GtkUConsoleAppState& state,
                      const MapWorkspaceSnapshot& snapshot,
                      double lat,
                      double lon,
                      const char* label,
                      int width,
                      int height)
{
    double x_fraction = 0.0;
    double y_fraction = 0.0;
    if (!projectMapPoint(snapshot, lat, lon, x_fraction, y_fraction))
    {
        return;
    }
    GtkWidget* marker = buildMeasureMarker(label);
    const double x = std::clamp(x_fraction, 0.0, 1.0) *
                     static_cast<double>(width);
    const double y = std::clamp(y_fraction, 0.0, 1.0) *
                     static_cast<double>(height);
    gtk_fixed_put(GTK_FIXED(state.map_marker_layer),
                  marker,
                  std::max(0.0, x - 10.0),
                  std::max(0.0, y - 10.0));
}

void refreshMapMarkers(GtkUConsoleAppState& state,
                       const MapWorkspaceSnapshot& snapshot)
{
    clearFixed(state.map_marker_layer);
    if (state.map_marker_layer == nullptr)
    {
        return;
    }

    const int width = gtk_widget_get_width(state.map_marker_layer);
    const int height = gtk_widget_get_height(state.map_marker_layer);
    if (width <= 0 || height <= 0)
    {
        return;
    }

    if (state.map_selected_node_id != 0 &&
        std::none_of(snapshot.nodes.begin(),
                     snapshot.nodes.end(),
                     [&](const auto& node)
                     {
                         return node.node_id == state.map_selected_node_id;
                     }))
    {
        state.map_selected_node_id = 0;
    }

    for (const auto& node : snapshot.nodes)
    {
        GtkWidget* marker = buildNodeMarker(state, node);
        const double x = std::clamp(node.x_fraction, 0.0, 1.0) *
                         static_cast<double>(width);
        const double y = std::clamp(node.y_fraction, 0.0, 1.0) *
                         static_cast<double>(height);
        gtk_fixed_put(GTK_FIXED(state.map_marker_layer),
                      marker,
                      std::max(0.0, x - 14.0),
                      std::max(0.0, y - 11.0));
        if (state.map_selected_node_id == node.node_id)
        {
            GtkWidget* bubble = buildMapNodeBubble(node);
            gtk_fixed_put(
                GTK_FIXED(state.map_marker_layer),
                bubble,
                std::clamp(x + 12.0,
                           4.0,
                           static_cast<double>(std::max(4, width - 178))),
                std::clamp(y - 18.0,
                           4.0,
                           static_cast<double>(std::max(4, height - 156))));
        }
    }

    if (state.map_measure_has_start)
    {
        putMeasureMarker(state,
                         snapshot,
                         state.map_measure_start_lat,
                         state.map_measure_start_lon,
                         "A",
                         width,
                         height);
    }
    if (state.map_measure_has_end)
    {
        putMeasureMarker(state,
                         snapshot,
                         state.map_measure_end_lat,
                         state.map_measure_end_lon,
                         "B",
                         width,
                         height);

        const double midpoint_lat =
            (state.map_measure_start_lat + state.map_measure_end_lat) * 0.5;
        const double midpoint_lon =
            (state.map_measure_start_lon + state.map_measure_end_lon) * 0.5;
        const double meters = mapDistanceMeters(state.map_measure_start_lat,
                                                state.map_measure_start_lon,
                                                state.map_measure_end_lat,
                                                state.map_measure_end_lon);
        putMeasureMarker(state,
                         snapshot,
                         midpoint_lat,
                         midpoint_lon,
                         formatMapDistance(meters).c_str(),
                         width,
                         height);
    }
}

GtkWidget* buildTileCell(const MapTileItem& item, bool center)
{
    GtkWidget* cell = gtk_overlay_new();
    gtk_widget_add_css_class(cell, "tile-cell");
    gtk_widget_set_size_request(cell, 1, 1);
    gtk_widget_set_hexpand(cell, TRUE);
    gtk_widget_set_vexpand(cell, TRUE);

    GtkWidget* content = nullptr;
    if (item.available)
    {
        content =
            gtk_picture_new_for_filename(item.path.string().c_str());
        gtk_picture_set_content_fit(GTK_PICTURE(content),
                                    GTK_CONTENT_FIT_FILL);
        gtk_picture_set_can_shrink(GTK_PICTURE(content), TRUE);
    }
    else
    {
        content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_add_css_class(content, "map-tile-pending");
    }
    gtk_widget_set_size_request(content, 1, 1);
    gtk_widget_set_hexpand(content, TRUE);
    gtk_widget_set_vexpand(content, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(cell), content);

    if (center)
    {
        GtkWidget* marker = makeLabel("+", "map-marker");
        gtk_widget_set_halign(marker, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(marker, GTK_ALIGN_CENTER);
        gtk_overlay_add_overlay(GTK_OVERLAY(cell), marker);
    }
    return cell;
}

GtkWidget* buildContourTileCell(const MapWorkspaceSnapshot& snapshot,
                                std::size_t base_tile_index)
{
    GtkWidget* cell = gtk_overlay_new();
    gtk_widget_add_css_class(cell, "map-contour-cell");
    gtk_widget_set_size_request(cell, 1, 1);
    gtk_widget_set_hexpand(cell, TRUE);
    gtk_widget_set_vexpand(cell, TRUE);
    gtk_widget_set_can_target(cell, FALSE);

    GtkWidget* blank = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(blank, 1, 1);
    gtk_widget_set_hexpand(blank, TRUE);
    gtk_widget_set_vexpand(blank, TRUE);
    gtk_widget_set_can_target(blank, FALSE);
    gtk_overlay_set_child(GTK_OVERLAY(cell), blank);

    for (const auto& contour : snapshot.contour_tiles)
    {
        if (!contour.available || contour.base_tile_index != base_tile_index)
        {
            continue;
        }

        GtkWidget* image =
            gtk_picture_new_for_filename(contour.path.string().c_str());
        gtk_picture_set_content_fit(GTK_PICTURE(image), GTK_CONTENT_FIT_FILL);
        gtk_picture_set_can_shrink(GTK_PICTURE(image), TRUE);
        gtk_widget_set_size_request(image, 1, 1);
        gtk_widget_set_hexpand(image, TRUE);
        gtk_widget_set_vexpand(image, TRUE);
        gtk_widget_set_can_target(image, FALSE);
        gtk_widget_set_opacity(
            image,
            contour.id.profile.kind ==
                    ::platform::linux_runtime::MapContourKind::Minor
                ? 0.70
                : 0.85);
        gtk_overlay_add_overlay(GTK_OVERLAY(cell), image);
    }

    return cell;
}

void refreshMap(GtkUConsoleAppState& state,
                const MapWorkspaceSnapshot& snapshot)
{
    pollContourFill(state);
    maybeStartMapFetch(state, snapshot);

    setActiveMapSourceButton(
        state.map_source_osm,
        ::platform::linux_runtime::MapBaseSource::Osm,
        snapshot.source_label);
    setActiveMapSourceButton(
        state.map_source_terrain,
        ::platform::linux_runtime::MapBaseSource::Terrain,
        snapshot.source_label);
    setActiveMapSourceButton(
        state.map_source_satellite,
        ::platform::linux_runtime::MapBaseSource::Satellite,
        snapshot.source_label);
    if (state.map_mqtt_nodes != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.map_mqtt_nodes),
                              snapshot.show_mqtt_nodes);
    }
    if (state.map_contour_visible != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.map_contour_visible),
                              snapshot.contour_enabled);
    }
    if (state.map_contour_fill != nullptr)
    {
        gtk_widget_set_sensitive(
            state.map_contour_fill,
            snapshot.contour_enabled &&
                    !state.contour_fill_job.future.valid()
                ? TRUE
                : FALSE);
    }
    if (state.settings_map_mqtt_nodes != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_map_mqtt_nodes),
                              snapshot.show_mqtt_nodes);
    }
    if (state.map_recenter != nullptr)
    {
        gtk_widget_set_sensitive(state.map_recenter,
                                 snapshot.has_manual_center ? TRUE : FALSE);
    }
    if (state.map_measure_clear != nullptr)
    {
        gtk_widget_set_sensitive(
            state.map_measure_clear,
            state.map_measure_has_start || state.map_measure_has_end ? TRUE
                                                                     : FALSE);
    }
    updateMapMeasureStatus(state);

    std::string title = snapshot.source_label + " map";
    title += " / z" + std::to_string(snapshot.zoom);
    setLabel(state.map_title, title);

    if (snapshot.has_center)
    {
        std::string meta = truncateMapStatusValue(snapshot.fix_label);
        meta += "\nlat: " + formatMapDecimal(snapshot.lat);
        meta += "\nlon: " + formatMapDecimal(snapshot.lon);
        if (snapshot.has_altitude)
        {
            meta += "\nalt: " +
                    std::to_string(static_cast<int>(
                        std::lround(snapshot.altitude_m))) +
                    " m";
        }
        if (snapshot.satellites > 0)
        {
            meta += "\nsats: " + std::to_string(snapshot.satellites);
        }
        setLabel(state.map_meta, meta);
    }
    else
    {
        setLabel(state.map_meta, "No map center.");
    }

    const std::string grid_signature = mapGridSignature(snapshot);
    if (grid_signature != state.map_grid_signature)
    {
        clearGrid(state.map_grid);
        clearGrid(state.map_contour_grid);
        if (!snapshot.has_center)
        {
            GtkWidget* empty = makeRowBox();
            gtk_box_append(GTK_BOX(empty),
                           makeLabel("No map center", "row-title"));
            gtk_grid_attach(GTK_GRID(state.map_grid),
                            empty,
                            0,
                            0,
                            3,
                            1);
        }
        else
        {
            for (std::size_t index = 0; index < snapshot.tiles.size(); ++index)
            {
                const auto columns =
                    std::max<std::size_t>(1U, snapshot.columns);
                const int col = static_cast<int>(index % columns);
                const int row = static_cast<int>(index / columns);
                gtk_grid_attach(GTK_GRID(state.map_grid),
                                buildTileCell(
                                    snapshot.tiles[index],
                                    index == snapshot.center_tile_index),
                                col,
                                row,
                                1,
                                1);
                if (snapshot.contour_enabled &&
                    state.map_contour_grid != nullptr)
                {
                    gtk_grid_attach(GTK_GRID(state.map_contour_grid),
                                    buildContourTileCell(snapshot, index),
                                    col,
                                    row,
                                    1,
                                    1);
                }
            }
        }
        state.map_grid_signature = grid_signature;
    }
    refreshMapMarkers(state, snapshot);

    std::string tile_status = "Tiles\n";
    tile_status += state.map_fetch_status.empty() ? "state: idle"
                                                  : state.map_fetch_status;
    tile_status += "\nactive: " + std::to_string(state.map_fetch_jobs.size());
    tile_status += "\nretry: " +
                   std::to_string(state.map_failed_tiles.size());
    setLabel(state.map_status, tile_status);
    if (state.map_contour_status != nullptr)
    {
        std::string contour = "Contours\n";
        contour += snapshot.contour_enabled ? "visible: yes" : "visible: no";
        contour += "\ncached: " +
                   std::to_string(snapshot.contour_available_count) + "/" +
                   std::to_string(snapshot.contour_tiles.size());
        contour += snapshot.earthdata_token_configured
                       ? "\ntoken: set"
                       : "\ntoken: missing";
        if (!snapshot.contour_profiles.empty())
        {
            contour += "\nprofile: ";
            for (std::size_t i = 0; i < snapshot.contour_profiles.size(); ++i)
            {
                if (i > 0)
                {
                    contour += ",";
                }
                contour += snapshot.contour_profiles[i];
            }
        }
        if (!state.contour_fill_status.empty())
        {
            contour += "\nfill: " + state.contour_fill_status;
        }
        setLabel(state.map_contour_status, contour);
    }

    std::string cache = "Cache\nbase: " +
                        std::to_string(snapshot.cache_stats.cached_tiles) +
                        "\nbytes: " +
                        formatBytes(snapshot.cache_stats.total_bytes) +
                        "\nfailed: " +
                        std::to_string(snapshot.cache_stats.failed_tiles) +
                        "\nnodes: " +
                        std::to_string(snapshot.visible_node_count);
    if (snapshot.show_mqtt_nodes)
    {
        cache += "\nmqtt: " +
                 std::to_string(snapshot.visible_mqtt_node_count);
    }
    else if (snapshot.hidden_mqtt_node_count > 0)
    {
        cache += "\nmqtt hidden: " +
                 std::to_string(snapshot.hidden_mqtt_node_count);
    }
    const auto mqtt = loadMqttSettings();
    if (mqtt.enabled)
    {
        cache += "\nmqtt client: offline";
    }
    setLabel(state.map_cache_status, cache);
    if (state.map_cache_status != nullptr)
    {
        gtk_widget_set_tooltip_text(state.map_cache_status,
                                    snapshot.cache_stats.root.string().c_str());
    }
}

void refreshMap(GtkUConsoleAppState& state)
{
    refreshMap(state, state.map_model.snapshot());
}

void refreshMapLogic(GtkUConsoleAppState& state,
                     const GtkUConsoleRefreshSnapshot& snapshot)
{
    refreshMap(state, snapshot.map);
}

static void destroyMapLogic(GtkUConsoleAppState& state)
{
    for (auto& job : state.map_fetch_jobs)
    {
        if (job.future.valid())
        {
            job.future.wait();
        }
    }
    state.map_fetch_jobs.clear();
    state.map_inflight_tiles.clear();
    if (state.contour_fill_job.future.valid())
    {
        state.contour_fill_job.future.wait();
    }
}

GtkUConsolePageLifecycle makeMapPageLifecycle()
{
    return {.name = "map",
            .title = "Map",
            .onLaunch = launchMapLayout,
            .onShow = nullptr,
            .onHide = nullptr,
            .onRefresh = refreshMapLogic,
            .onDestroy = destroyMapLogic};
}

} // namespace trailmate::uconsole::gtk
