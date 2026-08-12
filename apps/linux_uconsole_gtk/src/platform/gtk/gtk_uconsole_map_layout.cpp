#include "platform/gtk/gtk_uconsole_layout_spec.h"
#include "platform/gtk/gtk_uconsole_pages.h"
#include "platform/gtk/gtk_uconsole_widgets.h"

namespace trailmate::uconsole::gtk
{

GtkWidget* buildMapSourceButton(GtkUConsoleAppState& state,
                                GtkWidget** out_button,
                                const char* label,
                                ::platform::linux_runtime::MapBaseSource source)
{
    GtkWidget* button = gtk_button_new_with_label(label);
    gtk_widget_add_css_class(button, "nav-button");
    gtk_widget_set_hexpand(button, TRUE);
    g_object_set_data(G_OBJECT(button), "trailmate-source",
                      GUINT_TO_POINTER(static_cast<guint>(source)));
    g_signal_connect(button, "clicked", G_CALLBACK(onMapSourceClicked),
                     &state);
    *out_button = button;
    return button;
}

GtkWidget* buildMapContextPopover(GtkUConsoleAppState& state,
                                  GtkWidget* parent)
{
    GtkWidget* popover = gtk_popover_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_add_css_class(box, "map-context-menu");

    state.map_context_label = makeLabel("Point", "row-meta");
    gtk_box_append(GTK_BOX(box), state.map_context_label);

    GtkWidget* center = gtk_button_new_with_label("Center here");
    gtk_widget_add_css_class(center, "nav-button");
    g_signal_connect(center,
                     "clicked",
                     G_CALLBACK(onMapContextCenterClicked),
                     &state);
    gtk_box_append(GTK_BOX(box), center);

    GtkWidget* zoom_in = gtk_button_new_with_label("Zoom in here");
    gtk_widget_add_css_class(zoom_in, "nav-button");
    g_signal_connect(zoom_in,
                     "clicked",
                     G_CALLBACK(onMapContextZoomInClicked),
                     &state);
    gtk_box_append(GTK_BOX(box), zoom_in);

    GtkWidget* zoom_out = gtk_button_new_with_label("Zoom out here");
    gtk_widget_add_css_class(zoom_out, "nav-button");
    g_signal_connect(zoom_out,
                     "clicked",
                     G_CALLBACK(onMapContextZoomOutClicked),
                     &state);
    gtk_box_append(GTK_BOX(box), zoom_out);

    GtkWidget* measure_start = gtk_button_new_with_label("Measure from here");
    gtk_widget_add_css_class(measure_start, "nav-button");
    g_signal_connect(measure_start,
                     "clicked",
                     G_CALLBACK(onMapContextMeasureStartClicked),
                     &state);
    gtk_box_append(GTK_BOX(box), measure_start);

    GtkWidget* measure_end = gtk_button_new_with_label("Measure to here");
    gtk_widget_add_css_class(measure_end, "nav-button");
    g_signal_connect(measure_end,
                     "clicked",
                     G_CALLBACK(onMapContextMeasureEndClicked),
                     &state);
    gtk_box_append(GTK_BOX(box), measure_end);

    gtk_popover_set_child(GTK_POPOVER(popover), box);
    gtk_widget_set_parent(popover, parent);
    return popover;
}

GtkWidget* makeMapRailViewport(GtkWidget* panel)
{
    GtkWidget* viewport = gtk_scrolled_window_new();
    gtk_widget_set_hexpand(viewport, FALSE);
    gtk_widget_set_vexpand(viewport, TRUE);
    gtk_widget_set_size_request(viewport,
                                layout_spec::kMapDrawerWidth,
                                1);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(viewport),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_width(
        GTK_SCROLLED_WINDOW(viewport),
        layout_spec::kMapDrawerWidth);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(viewport),
                                               1);
    gtk_scrolled_window_set_propagate_natural_width(
        GTK_SCROLLED_WINDOW(viewport),
        FALSE);
    gtk_scrolled_window_set_propagate_natural_height(
        GTK_SCROLLED_WINDOW(viewport),
        FALSE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(viewport), panel);
    return viewport;
}

void revealMapDrawer(GtkUConsoleAppState& state,
                     GtkWidget* requested_drawer)
{
    const bool will_reveal =
        !gtk_revealer_get_reveal_child(GTK_REVEALER(requested_drawer));
    gtk_revealer_set_reveal_child(GTK_REVEALER(state.map_layers_drawer),
                                  false);
    gtk_revealer_set_reveal_child(GTK_REVEALER(state.map_tools_drawer),
                                  false);
    gtk_revealer_set_reveal_child(GTK_REVEALER(requested_drawer), will_reveal);
}

void onMapLayersDrawerClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    revealMapDrawer(state, state.map_layers_drawer);
}

void onMapToolsDrawerClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    revealMapDrawer(state, state.map_tools_drawer);
}

GtkWidget* makeMapToolbarButton(const char* label,
                                GCallback callback,
                                GtkUConsoleAppState& state)
{
    GtkWidget* button = gtk_button_new_with_label(label);
    gtk_widget_add_css_class(button, "map-toolbar-button");
    g_signal_connect(button, "clicked", callback, &state);
    return button;
}

GtkWidget* launchMapLayout(GtkUConsoleAppState& state)
{
    GtkWidget* root = makeWorkbench(GTK_ORIENTATION_HORIZONTAL, 0);

    GtkWidget* left_panel = makePanel();
    gtk_widget_add_css_class(left_panel, "map-side-panel");
    gtk_widget_set_hexpand(left_panel, FALSE);
    gtk_widget_set_size_request(left_panel,
                                layout_spec::kMapDrawerWidth,
                                -1);
    gtk_widget_set_vexpand(left_panel, TRUE);

    state.map_title = makeLabel("OSM map", "pane-heading");
    constrainLabelWidth(state.map_title,
                        layout_spec::kMapDrawerTextWidthChars);
    gtk_box_append(GTK_BOX(left_panel), state.map_title);
    state.map_meta = makeLabel("", "row-meta", true);
    constrainLabelWidth(state.map_meta,
                        layout_spec::kMapDrawerTextWidthChars);
    gtk_box_append(GTK_BOX(left_panel), state.map_meta);

    GtkWidget* source_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_add_css_class(source_section, "map-tool-section");
    gtk_box_append(GTK_BOX(source_section),
                   makeLabel("Base layer", "map-tool-title"));
    GtkWidget* source_row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(source_row, "map-tool-row");
    gtk_box_append(GTK_BOX(source_row),
                   buildMapSourceButton(
                       state,
                       &state.map_source_osm,
                       "OSM",
                       ::platform::linux_runtime::MapBaseSource::Osm));
    gtk_box_append(GTK_BOX(source_row),
                   buildMapSourceButton(
                       state,
                       &state.map_source_terrain,
                       "Terrain",
                       ::platform::linux_runtime::MapBaseSource::Terrain));
    gtk_box_append(GTK_BOX(source_row),
                   buildMapSourceButton(
                       state,
                       &state.map_source_satellite,
                       "Satellite",
                       ::platform::linux_runtime::MapBaseSource::Satellite));
    gtk_box_append(GTK_BOX(source_section), source_row);
    gtk_box_append(GTK_BOX(left_panel), source_section);

    GtkWidget* layers_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(layers_section, "map-tool-section");
    gtk_box_append(GTK_BOX(layers_section),
                   makeLabel("Layers", "map-tool-title"));

    GtkWidget* mqtt_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(mqtt_row, "map-tool-row");
    GtkWidget* mqtt_label = makeLabel("MQTT", "row-meta");
    gtk_widget_set_hexpand(mqtt_label, TRUE);
    gtk_box_append(GTK_BOX(mqtt_row), mqtt_label);
    state.map_mqtt_nodes = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(state.map_mqtt_nodes),
                          state.map_model.snapshot().show_mqtt_nodes);
    g_signal_connect(state.map_mqtt_nodes,
                     "notify::active",
                     G_CALLBACK(onMapMqttNodesToggled),
                     &state);
    gtk_box_append(GTK_BOX(mqtt_row), state.map_mqtt_nodes);
    gtk_box_append(GTK_BOX(layers_section), mqtt_row);

    GtkWidget* contour_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(contour_row, "map-tool-row");
    GtkWidget* contour_label = makeLabel("Contours", "row-meta");
    gtk_widget_set_hexpand(contour_label, TRUE);
    gtk_box_append(GTK_BOX(contour_row), contour_label);
    state.map_contour_visible = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(state.map_contour_visible),
                          state.map_model.snapshot().contour_enabled);
    g_signal_connect(state.map_contour_visible,
                     "notify::active",
                     G_CALLBACK(onMapContourVisibleToggled),
                     &state);
    gtk_box_append(GTK_BOX(contour_row), state.map_contour_visible);
    gtk_box_append(GTK_BOX(layers_section), contour_row);

    state.map_contour_fill = gtk_button_new_with_label("Fill");
    gtk_widget_add_css_class(state.map_contour_fill, "nav-button");
    g_signal_connect(state.map_contour_fill,
                     "clicked",
                     G_CALLBACK(onMapContourFillClicked),
                     &state);
    gtk_box_append(GTK_BOX(layers_section), state.map_contour_fill);
    gtk_box_append(GTK_BOX(left_panel), layers_section);

    GtkWidget* status_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(status_section, "map-tool-section");
    state.map_status = makeLabel("", "row-meta", true);
    constrainLabelWidth(state.map_status,
                        layout_spec::kMapDrawerTextWidthChars);
    gtk_box_append(GTK_BOX(status_section), state.map_status);
    state.map_contour_status = makeLabel("", "row-meta", true);
    constrainLabelWidth(state.map_contour_status,
                        layout_spec::kMapDrawerTextWidthChars);
    gtk_box_append(GTK_BOX(status_section), state.map_contour_status);
    state.map_cache_status = makeLabel("", "row-meta", true);
    constrainLabelWidth(state.map_cache_status,
                        layout_spec::kMapDrawerTextWidthChars);
    gtk_box_append(GTK_BOX(status_section), state.map_cache_status);
    gtk_box_append(GTK_BOX(left_panel), status_section);

    state.map_canvas = gtk_overlay_new();
    gtk_widget_add_css_class(state.map_canvas, "map-canvas");
    gtk_widget_set_hexpand(state.map_canvas, TRUE);
    gtk_widget_set_vexpand(state.map_canvas, TRUE);
    gtk_widget_set_overflow(state.map_canvas, GTK_OVERFLOW_HIDDEN);

    state.map_grid = gtk_grid_new();
    gtk_widget_add_css_class(state.map_grid, "map-grid");
    gtk_widget_set_size_request(state.map_grid, 1, 1);
    gtk_grid_set_row_spacing(GTK_GRID(state.map_grid), 0);
    gtk_grid_set_column_spacing(GTK_GRID(state.map_grid), 0);
    gtk_grid_set_row_homogeneous(GTK_GRID(state.map_grid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(state.map_grid), TRUE);
    gtk_widget_set_hexpand(state.map_grid, TRUE);
    gtk_widget_set_vexpand(state.map_grid, TRUE);

    state.map_contour_grid = gtk_grid_new();
    gtk_widget_add_css_class(state.map_contour_grid, "map-contour-grid");
    gtk_widget_set_size_request(state.map_contour_grid, 1, 1);
    gtk_grid_set_row_spacing(GTK_GRID(state.map_contour_grid), 0);
    gtk_grid_set_column_spacing(GTK_GRID(state.map_contour_grid), 0);
    gtk_grid_set_row_homogeneous(GTK_GRID(state.map_contour_grid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(state.map_contour_grid), TRUE);
    gtk_widget_set_hexpand(state.map_contour_grid, TRUE);
    gtk_widget_set_vexpand(state.map_contour_grid, TRUE);
    gtk_widget_set_can_target(state.map_contour_grid, FALSE);

    GtkWidget* map_viewport = gtk_overlay_new();
    gtk_widget_set_hexpand(map_viewport, TRUE);
    gtk_widget_set_vexpand(map_viewport, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(map_viewport), state.map_grid);
    gtk_overlay_add_overlay(GTK_OVERLAY(map_viewport),
                            state.map_contour_grid);
    state.map_marker_layer = gtk_fixed_new();
    gtk_widget_set_hexpand(state.map_marker_layer, TRUE);
    gtk_widget_set_vexpand(state.map_marker_layer, TRUE);
    gtk_overlay_add_overlay(GTK_OVERLAY(map_viewport),
                            state.map_marker_layer);

    GtkGesture* drag = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag),
                                  GDK_BUTTON_PRIMARY);
    g_signal_connect(drag, "drag-begin", G_CALLBACK(onMapDragBegin), &state);
    g_signal_connect(drag, "drag-update", G_CALLBACK(onMapDragUpdate), &state);
    g_signal_connect(drag, "drag-end", G_CALLBACK(onMapDragEnd), &state);
    gtk_widget_add_controller(map_viewport, GTK_EVENT_CONTROLLER(drag));

    GtkGesture* context_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(context_click),
                                  GDK_BUTTON_SECONDARY);
    g_signal_connect(context_click,
                     "pressed",
                     G_CALLBACK(onMapContextPressed),
                     &state);
    gtk_widget_add_controller(map_viewport,
                              GTK_EVENT_CONTROLLER(context_click));
    state.map_context_popover = buildMapContextPopover(state, map_viewport);

    GtkGesture* primary_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(primary_click),
                                  GDK_BUTTON_PRIMARY);
    g_signal_connect(primary_click,
                     "pressed",
                     G_CALLBACK(onMapPrimaryPressed),
                     &state);
    gtk_widget_add_controller(map_viewport,
                              GTK_EVENT_CONTROLLER(primary_click));

    gtk_widget_set_size_request(map_viewport, 1, 1);
    gtk_widget_set_overflow(map_viewport, GTK_OVERFLOW_HIDDEN);
    gtk_overlay_set_child(GTK_OVERLAY(state.map_canvas), map_viewport);
    gtk_box_append(GTK_BOX(root), state.map_canvas);

    GtkWidget* right_panel = makePanel();
    gtk_widget_add_css_class(right_panel, "map-tools-panel");
    gtk_widget_set_hexpand(right_panel, FALSE);
    gtk_widget_set_size_request(right_panel,
                                layout_spec::kMapDrawerWidth,
                                -1);
    gtk_widget_set_vexpand(right_panel, TRUE);
    gtk_box_append(GTK_BOX(right_panel),
                   makeLabel("Tools", "pane-heading"));
    GtkWidget* distance_section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(distance_section, "map-tool-section");
    gtk_box_append(GTK_BOX(distance_section),
                   makeLabel("Distance", "map-tool-title"));
    state.map_measure_button = gtk_button_new_with_label("Measure");
    gtk_widget_add_css_class(state.map_measure_button, "nav-button");
    g_signal_connect(state.map_measure_button,
                     "clicked",
                     G_CALLBACK(onMapMeasureClicked),
                     &state);
    gtk_box_append(GTK_BOX(distance_section), state.map_measure_button);
    state.map_measure_clear = gtk_button_new_with_label("Clear");
    gtk_widget_add_css_class(state.map_measure_clear, "nav-button");
    g_signal_connect(state.map_measure_clear,
                     "clicked",
                     G_CALLBACK(onMapMeasureClearClicked),
                     &state);
    gtk_box_append(GTK_BOX(distance_section), state.map_measure_clear);
    state.map_measure_status = makeLabel("", "row-meta", true);
    constrainLabelWidth(state.map_measure_status,
                        layout_spec::kMapDrawerTextWidthChars);
    gtk_box_append(GTK_BOX(distance_section), state.map_measure_status);
    gtk_box_append(GTK_BOX(right_panel), distance_section);

    state.map_layers_drawer = gtk_revealer_new();
    gtk_revealer_set_transition_type(
        GTK_REVEALER(state.map_layers_drawer),
        GTK_REVEALER_TRANSITION_TYPE_SLIDE_RIGHT);
    gtk_revealer_set_transition_duration(GTK_REVEALER(state.map_layers_drawer),
                                         160);
    gtk_revealer_set_reveal_child(GTK_REVEALER(state.map_layers_drawer), false);
    gtk_revealer_set_child(GTK_REVEALER(state.map_layers_drawer),
                           makeMapRailViewport(left_panel));
    gtk_widget_add_css_class(state.map_layers_drawer, "map-drawer");
    gtk_widget_set_halign(state.map_layers_drawer, GTK_ALIGN_START);
    gtk_widget_set_valign(state.map_layers_drawer, GTK_ALIGN_FILL);
    gtk_overlay_add_overlay(GTK_OVERLAY(state.map_canvas),
                            state.map_layers_drawer);

    state.map_tools_drawer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(state.map_tools_drawer),
                                     GTK_REVEALER_TRANSITION_TYPE_SLIDE_LEFT);
    gtk_revealer_set_transition_duration(GTK_REVEALER(state.map_tools_drawer),
                                         160);
    gtk_revealer_set_reveal_child(GTK_REVEALER(state.map_tools_drawer), false);
    gtk_revealer_set_child(GTK_REVEALER(state.map_tools_drawer),
                           makeMapRailViewport(right_panel));
    gtk_widget_add_css_class(state.map_tools_drawer, "map-drawer");
    gtk_widget_set_halign(state.map_tools_drawer, GTK_ALIGN_END);
    gtk_widget_set_valign(state.map_tools_drawer, GTK_ALIGN_FILL);
    gtk_overlay_add_overlay(GTK_OVERLAY(state.map_canvas),
                            state.map_tools_drawer);

    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(toolbar, "map-toolbar");
    gtk_widget_set_halign(toolbar, GTK_ALIGN_START);
    gtk_widget_set_valign(toolbar, GTK_ALIGN_START);
    gtk_widget_set_margin_start(toolbar, 10);
    gtk_widget_set_margin_top(toolbar, 10);
    gtk_box_append(GTK_BOX(toolbar),
                   makeMapToolbarButton("Layers",
                                        G_CALLBACK(onMapLayersDrawerClicked),
                                        state));
    gtk_box_append(GTK_BOX(toolbar),
                   makeMapToolbarButton("−",
                                        G_CALLBACK(onMapZoomOutClicked),
                                        state));
    gtk_box_append(GTK_BOX(toolbar),
                   makeMapToolbarButton("+",
                                        G_CALLBACK(onMapZoomInClicked),
                                        state));
    state.map_recenter =
        makeMapToolbarButton("Locate", G_CALLBACK(onMapRecenterClicked), state);
    gtk_box_append(GTK_BOX(toolbar), state.map_recenter);
    gtk_box_append(GTK_BOX(toolbar),
                   makeMapToolbarButton("Tools",
                                        G_CALLBACK(onMapToolsDrawerClicked),
                                        state));
    gtk_overlay_add_overlay(GTK_OVERLAY(state.map_canvas), toolbar);
    return root;
}

} // namespace trailmate::uconsole::gtk
