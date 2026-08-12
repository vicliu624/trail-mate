#include "platform/gtk/gtk_uconsole_shell.h"

#include <array>
#include <string>

#include "platform/gtk/gtk_uconsole_layout_spec.h"
#include "platform/gtk/gtk_uconsole_pages.h"
#include "platform/gtk/gtk_uconsole_widgets.h"

namespace trailmate::uconsole::gtk
{

namespace
{

struct WorkspaceShortcut
{
    guint keyval = 0;
    const char* page = "";
};

constexpr std::array<WorkspaceShortcut, 11> kWorkspaceShortcuts{{
    {GDK_KEY_c, "chat"},
    {GDK_KEY_m, "map"},
    {GDK_KEY_n, "contacts"},
    {GDK_KEY_g, "gps"},
    {GDK_KEY_t, "team"},
    {GDK_KEY_r, "tracker"},
    {GDK_KEY_l, "radio-tools"},
    {GDK_KEY_e, "extensions"},
    {GDK_KEY_v, "hardware"},
    {GDK_KEY_d, "data"},
    {GDK_KEY_p, "logs"},
}};

bool textInputHasFocus(const GtkUConsoleAppState& state)
{
    if (state.window == nullptr)
    {
        return false;
    }
    GtkWidget* focused = gtk_window_get_focus(GTK_WINDOW(state.window));
    return focused != nullptr &&
           (GTK_IS_EDITABLE(focused) || GTK_IS_TEXT_VIEW(focused));
}

void onShortcutWindowDestroyed(GtkWidget* widget, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    if (state.shortcut_window == GTK_WINDOW(widget))
    {
        state.shortcut_window = nullptr;
    }
}

void onShortcutWindowCloseClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    if (state.shortcut_window != nullptr)
    {
        gtk_window_destroy(state.shortcut_window);
    }
}

gboolean onShortcutWindowKeyPressed(GtkEventControllerKey*,
                                    guint keyval,
                                    guint,
                                    GdkModifierType,
                                    gpointer data)
{
    if (keyval != GDK_KEY_Escape)
    {
        return GDK_EVENT_PROPAGATE;
    }
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    if (state.shortcut_window != nullptr)
    {
        gtk_window_destroy(state.shortcut_window);
    }
    return GDK_EVENT_STOP;
}

GtkWidget* makeShortcutRow(const char* keys, const char* action)
{
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(row, "shortcut-row");
    GtkWidget* key_label = makeLabel(keys, "shortcut-key");
    gtk_widget_set_size_request(key_label, 86, -1);
    gtk_box_append(GTK_BOX(row), key_label);
    gtk_box_append(GTK_BOX(row), makeLabel(action, "shortcut-action", true));
    return row;
}

void showShortcutReference(GtkUConsoleAppState& state)
{
    if (state.shortcut_window != nullptr)
    {
        gtk_window_present(state.shortcut_window);
        return;
    }

    GtkWidget* window = gtk_window_new();
    state.shortcut_window = GTK_WINDOW(window);
    gtk_window_set_title(state.shortcut_window,
                         "Trail Mate keyboard shortcuts");
    gtk_window_set_transient_for(state.shortcut_window,
                                 GTK_WINDOW(state.window));
    gtk_window_set_modal(state.shortcut_window, TRUE);
    gtk_window_set_default_size(state.shortcut_window, 500, 420);
    gtk_widget_add_css_class(window, "shortcut-window");
    g_signal_connect(window, "destroy", G_CALLBACK(onShortcutWindowDestroyed),
                     &state);
    GtkEventController* key_controller = gtk_event_controller_key_new();
    g_signal_connect(key_controller,
                     "key-pressed",
                     G_CALLBACK(onShortcutWindowKeyPressed),
                     &state);
    gtk_widget_add_controller(window, key_controller);

    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(content, "shortcut-content");
    GtkWidget* heading = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* title = makeLabel("Keyboard shortcuts", "shortcut-title");
    gtk_widget_set_hexpand(title, TRUE);
    gtk_box_append(GTK_BOX(heading), title);
    GtkWidget* close = gtk_button_new_with_label("Close");
    gtk_widget_add_css_class(close, "nav-button");
    g_signal_connect(close, "clicked", G_CALLBACK(onShortcutWindowCloseClicked),
                     &state);
    gtk_box_append(GTK_BOX(heading), close);
    gtk_box_append(GTK_BOX(content), heading);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);
    GtkWidget* rows = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(rows, "shortcut-list");
    gtk_box_append(GTK_BOX(rows), makeShortcutRow("C", "Chat"));
    gtk_box_append(GTK_BOX(rows), makeShortcutRow("M", "Map"));
    gtk_box_append(GTK_BOX(rows), makeShortcutRow("N", "Contacts & nodes"));
    gtk_box_append(GTK_BOX(rows), makeShortcutRow("G", "GPS & sky plot"));
    gtk_box_append(GTK_BOX(rows), makeShortcutRow("T", "Team operations"));
    gtk_box_append(GTK_BOX(rows), makeShortcutRow("R", "Tracker"));
    gtk_box_append(GTK_BOX(rows), makeShortcutRow("L", "Radio tools"));
    gtk_box_append(GTK_BOX(rows), makeShortcutRow("E", "Extensions"));
    gtk_box_append(GTK_BOX(rows),
                   makeShortcutRow("V / D / P", "Hardware / Data / Logs"));
    gtk_box_append(GTK_BOX(rows), makeShortcutRow("S", "Settings"));
    gtk_box_append(GTK_BOX(rows),
                   makeShortcutRow("[ / ]", "Previous / next workspace"));
    gtk_box_append(GTK_BOX(rows),
                   makeShortcutRow("\\", "Show or hide navigation"));
    gtk_box_append(GTK_BOX(rows),
                   makeShortcutRow("F1 / H", "This shortcut reference"));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), rows);
    gtk_box_append(GTK_BOX(content), scroll);
    gtk_window_set_child(state.shortcut_window, content);
    gtk_window_present(state.shortcut_window);
}

const GtkUConsolePageLifecycle* findPage(const GtkUConsoleAppState& state,
                                         const std::string& name)
{
    for (const auto& page : state.page_lifecycle)
    {
        if (name == page.name)
        {
            return &page;
        }
    }
    return nullptr;
}

GtkUConsolePageLifecycle* findPage(GtkUConsoleAppState& state,
                                   const std::string& name)
{
    for (auto& page : state.page_lifecycle)
    {
        if (name == page.name)
        {
            return &page;
        }
    }
    return nullptr;
}

void setActiveNav(GtkUConsoleAppState& state, const char* page)
{
    const std::string current(page ? page : "");
    auto apply = [&current](GtkWidget* button, const char* name)
    {
        if (button == nullptr)
        {
            return;
        }
        if (current == name)
        {
            gtk_widget_add_css_class(button, "nav-button-active");
        }
        else
        {
            gtk_widget_remove_css_class(button, "nav-button-active");
        }
    };

    apply(state.nav_chat, "chat");
    apply(state.nav_contacts, "contacts");
    apply(state.nav_map, "map");
    apply(state.nav_gps, "gps");
    apply(state.nav_team, "team");
    apply(state.nav_tracker, "tracker");
    apply(state.nav_radio_tools, "radio-tools");
    apply(state.nav_extensions, "extensions");
    apply(state.nav_hardware, "hardware");
    apply(state.nav_data, "data");
    apply(state.nav_logs, "logs");
    apply(state.nav_settings, "settings");
}

void showAdjacentPage(GtkUConsoleAppState& state, int direction)
{
    if (state.page_lifecycle.empty())
    {
        return;
    }

    std::size_t current = 0;
    for (std::size_t index = 0; index < state.page_lifecycle.size(); ++index)
    {
        if (state.active_page == state.page_lifecycle[index].name)
        {
            current = index;
            break;
        }
    }
    const auto total = static_cast<int>(state.page_lifecycle.size());
    const int next = (static_cast<int>(current) + direction + total) % total;
    showPage(state, state.page_lifecycle[static_cast<std::size_t>(next)].name);
}

void toggleNavigationRail(GtkUConsoleAppState& state)
{
    if (state.navigation_rail == nullptr)
    {
        return;
    }
    state.navigation_collapsed = !state.navigation_collapsed;
    gtk_widget_set_visible(state.navigation_rail,
                           state.navigation_collapsed ? FALSE : TRUE);
    if (state.navigation_collapsed && state.stack != nullptr)
    {
        gtk_widget_grab_focus(state.stack);
    }
}

void onChatClicked(GtkButton*, gpointer data)
{
    showPage(*static_cast<GtkUConsoleAppState*>(data), "chat");
}

void onMapClicked(GtkButton*, gpointer data)
{
    showPage(*static_cast<GtkUConsoleAppState*>(data), "map");
}

void onContactsClicked(GtkButton*, gpointer data)
{
    showPage(*static_cast<GtkUConsoleAppState*>(data), "contacts");
}

void onGpsClicked(GtkButton*, gpointer data)
{
    showPage(*static_cast<GtkUConsoleAppState*>(data), "gps");
}

void onTeamClicked(GtkButton*, gpointer data)
{
    showPage(*static_cast<GtkUConsoleAppState*>(data), "team");
}

void onTrackerClicked(GtkButton*, gpointer data)
{
    showPage(*static_cast<GtkUConsoleAppState*>(data), "tracker");
}

void onRadioToolsClicked(GtkButton*, gpointer data)
{
    showPage(*static_cast<GtkUConsoleAppState*>(data), "radio-tools");
}

void onExtensionsClicked(GtkButton*, gpointer data)
{
    showPage(*static_cast<GtkUConsoleAppState*>(data), "extensions");
}

void onHardwareClicked(GtkButton*, gpointer data)
{
    showPage(*static_cast<GtkUConsoleAppState*>(data), "hardware");
}

void onDataClicked(GtkButton*, gpointer data)
{
    showPage(*static_cast<GtkUConsoleAppState*>(data), "data");
}

void onLogsClicked(GtkButton*, gpointer data)
{
    showPage(*static_cast<GtkUConsoleAppState*>(data), "logs");
}

void onSettingsClicked(GtkButton*, gpointer data)
{
    showPage(*static_cast<GtkUConsoleAppState*>(data), "settings");
}

void styleNavigationButton(GtkWidget* button)
{
    gtk_widget_add_css_class(button, "menu-button");
    gtk_widget_set_halign(button, GTK_ALIGN_FILL);
    if (GtkWidget* child = gtk_button_get_child(GTK_BUTTON(button));
        child != nullptr && GTK_IS_LABEL(child))
    {
        gtk_label_set_xalign(GTK_LABEL(child), 0.0F);
    }
}

GtkWidget* buildMenuBar(GtkUConsoleAppState& state)
{
    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    state.navigation_rail = bar;
    gtk_widget_add_css_class(bar, "navigation-rail");
    gtk_widget_set_size_request(bar, layout_spec::kNavigationRailWidth, -1);
    gtk_widget_set_hexpand(bar, FALSE);
    gtk_widget_set_vexpand(bar, TRUE);

    GtkWidget* brand = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_add_css_class(brand, "navigation-brand");
    gtk_box_append(GTK_BOX(brand), makeLabel("TRAIL MATE", "menu-title"));
    gtk_box_append(GTK_BOX(brand),
                   makeLabel("uConsole field desk", "navigation-subtitle"));
    gtk_box_append(GTK_BOX(bar), brand);

    gtk_box_append(GTK_BOX(bar),
                   makeLabel("WORKSPACES", "navigation-section-label"));

    state.nav_chat = gtk_button_new();
    styleNavigationButton(state.nav_chat);
    GtkWidget* chat_nav_child = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget* chat_nav_label = makeLabel("Chat");
    gtk_widget_set_hexpand(chat_nav_label, TRUE);
    gtk_label_set_xalign(GTK_LABEL(chat_nav_label), 0.0F);
    gtk_box_append(GTK_BOX(chat_nav_child), chat_nav_label);
    state.nav_chat_badge = makeLabel("", "menu-badge");
    gtk_label_set_xalign(GTK_LABEL(state.nav_chat_badge), 0.5F);
    gtk_widget_set_visible(state.nav_chat_badge, FALSE);
    gtk_box_append(GTK_BOX(chat_nav_child), state.nav_chat_badge);
    gtk_button_set_child(GTK_BUTTON(state.nav_chat), chat_nav_child);
    g_signal_connect(state.nav_chat, "clicked", G_CALLBACK(onChatClicked),
                     &state);
    gtk_box_append(GTK_BOX(bar), state.nav_chat);

    state.nav_map = gtk_button_new_with_label("Map");
    styleNavigationButton(state.nav_map);
    g_signal_connect(state.nav_map, "clicked", G_CALLBACK(onMapClicked),
                     &state);
    gtk_box_append(GTK_BOX(bar), state.nav_map);

    gtk_box_append(GTK_BOX(bar),
                   makeLabel("FIELD", "navigation-section-label"));

    state.nav_contacts = gtk_button_new_with_label("Contacts & nodes");
    styleNavigationButton(state.nav_contacts);
    g_signal_connect(state.nav_contacts,
                     "clicked",
                     G_CALLBACK(onContactsClicked),
                     &state);
    gtk_box_append(GTK_BOX(bar), state.nav_contacts);

    state.nav_gps = gtk_button_new_with_label("GPS & sky plot");
    styleNavigationButton(state.nav_gps);
    g_signal_connect(state.nav_gps,
                     "clicked",
                     G_CALLBACK(onGpsClicked),
                     &state);
    gtk_box_append(GTK_BOX(bar), state.nav_gps);

    state.nav_team = gtk_button_new_with_label("Team operations");
    styleNavigationButton(state.nav_team);
    g_signal_connect(state.nav_team,
                     "clicked",
                     G_CALLBACK(onTeamClicked),
                     &state);
    gtk_box_append(GTK_BOX(bar), state.nav_team);

    state.nav_tracker = gtk_button_new_with_label("Tracker");
    styleNavigationButton(state.nav_tracker);
    g_signal_connect(state.nav_tracker,
                     "clicked",
                     G_CALLBACK(onTrackerClicked),
                     &state);
    gtk_box_append(GTK_BOX(bar), state.nav_tracker);

    state.nav_radio_tools = gtk_button_new_with_label("Radio tools");
    styleNavigationButton(state.nav_radio_tools);
    g_signal_connect(state.nav_radio_tools,
                     "clicked",
                     G_CALLBACK(onRadioToolsClicked),
                     &state);
    gtk_box_append(GTK_BOX(bar), state.nav_radio_tools);

    gtk_box_append(GTK_BOX(bar),
                   makeLabel("SYSTEM", "navigation-section-label"));

    state.nav_hardware = gtk_button_new_with_label("Hardware");
    styleNavigationButton(state.nav_hardware);
    g_signal_connect(state.nav_hardware, "clicked",
                     G_CALLBACK(onHardwareClicked), &state);
    gtk_box_append(GTK_BOX(bar), state.nav_hardware);

    state.nav_data = gtk_button_new_with_label("Data & maps");
    styleNavigationButton(state.nav_data);
    g_signal_connect(state.nav_data, "clicked", G_CALLBACK(onDataClicked),
                     &state);
    gtk_box_append(GTK_BOX(bar), state.nav_data);

    state.nav_extensions = gtk_button_new_with_label("Extensions");
    styleNavigationButton(state.nav_extensions);
    g_signal_connect(state.nav_extensions,
                     "clicked",
                     G_CALLBACK(onExtensionsClicked),
                     &state);
    gtk_box_append(GTK_BOX(bar), state.nav_extensions);

    state.nav_logs = gtk_button_new_with_label("Logs");
    styleNavigationButton(state.nav_logs);
    g_signal_connect(state.nav_logs, "clicked", G_CALLBACK(onLogsClicked),
                     &state);
    gtk_box_append(GTK_BOX(bar), state.nav_logs);

    state.nav_settings = gtk_button_new_with_label("Settings");
    styleNavigationButton(state.nav_settings);
    g_signal_connect(state.nav_settings, "clicked",
                     G_CALLBACK(onSettingsClicked), &state);
    gtk_box_append(GTK_BOX(bar), state.nav_settings);

    GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_vexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(bar), spacer);

    GtkWidget* hint_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(hint_box, "navigation-footer");
    gtk_box_append(GTK_BOX(hint_box),
                   makeLabel("DESKTOP MODE", "navigation-footer-title"));
    gtk_box_append(GTK_BOX(hint_box),
                   makeLabel("Keyboard + pointer", "navigation-subtitle"));
    gtk_box_append(GTK_BOX(bar), hint_box);
    return bar;
}

GtkWidget* buildStatusBar(GtkUConsoleAppState& state)
{
    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(bar, "statusbar");
    gtk_widget_set_hexpand(bar, TRUE);
    gtk_widget_set_vexpand(bar, FALSE);
    gtk_widget_set_halign(bar, GTK_ALIGN_FILL);
    gtk_widget_set_valign(bar, GTK_ALIGN_FILL);
    gtk_widget_set_size_request(bar, -1, layout_spec::kGlobalStatusBarHeight);

    state.status_aio2 = makeLabel("AIO2: -", "status-chip");
    state.status_lora = makeLabel("LoRa: -", "status-chip");
    state.status_gps = makeLabel("GPS: -", "status-chip");
    state.status_node = makeLabel("Node: -", "status-chip");
    state.status_unread = makeLabel("Unread: 0", "status-chip");

    gtk_box_append(GTK_BOX(bar), state.status_aio2);
    gtk_box_append(GTK_BOX(bar), state.status_lora);
    gtk_box_append(GTK_BOX(bar), state.status_gps);

    GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(bar), spacer);

    gtk_box_append(GTK_BOX(bar), state.status_node);
    gtk_box_append(GTK_BOX(bar), state.status_unread);
    return bar;
}

} // namespace

bool handleUConsoleShortcut(GtkUConsoleAppState& state,
                            guint keyval,
                            GdkModifierType modifiers)
{
    if ((modifiers & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK)) != 0)
    {
        return false;
    }
    if (keyval == GDK_KEY_F1)
    {
        showShortcutReference(state);
        return true;
    }
    if (keyval == GDK_KEY_Escape && state.shortcut_window != nullptr)
    {
        gtk_window_destroy(state.shortcut_window);
        return true;
    }
    if (textInputHasFocus(state))
    {
        return false;
    }
    if (keyval == GDK_KEY_backslash)
    {
        toggleNavigationRail(state);
        return true;
    }
    if (keyval == GDK_KEY_bracketleft)
    {
        showAdjacentPage(state, -1);
        return true;
    }
    if (keyval == GDK_KEY_bracketright)
    {
        showAdjacentPage(state, 1);
        return true;
    }

    const guint normalized = gdk_keyval_to_lower(keyval);
    if (normalized == GDK_KEY_h)
    {
        showShortcutReference(state);
        return true;
    }
    if (normalized == GDK_KEY_s)
    {
        showPage(state, "settings");
        return true;
    }
    for (const auto& shortcut : kWorkspaceShortcuts)
    {
        if (normalized == shortcut.keyval)
        {
            showPage(state, shortcut.page);
            return true;
        }
    }
    return false;
}

void showPage(GtkUConsoleAppState& state, const char* page_name)
{
    const std::string next_name(page_name ? page_name : "map");
    auto* next = findPage(state, next_name);
    if (next == nullptr)
    {
        return;
    }

    const std::string previous_name = state.active_page;
    if (!previous_name.empty() && previous_name != next_name)
    {
        if (auto* previous = findPage(state, previous_name);
            previous != nullptr && previous->onHide != nullptr)
        {
            previous->onHide(state);
        }
    }

    gtk_stack_set_visible_child_name(GTK_STACK(state.stack), next->name);
    state.active_page = next->name;
    setActiveNav(state, next->name);
    if (previous_name != next_name && next->onShow != nullptr)
    {
        next->onShow(state);
    }
    refreshUi(state);
}

GtkWidget* buildRoot(GtkUConsoleAppState& state)
{
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(root, TRUE);
    gtk_widget_set_vexpand(root, TRUE);

    GtkWidget* body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(body, "body");
    gtk_widget_set_hexpand(body, TRUE);
    gtk_widget_set_vexpand(body, TRUE);
    gtk_widget_set_overflow(body, GTK_OVERFLOW_HIDDEN);
    gtk_box_append(GTK_BOX(body), buildMenuBar(state));

    state.page_lifecycle = buildGtkUConsolePageRegistry();
    state.stack = gtk_stack_new();
    gtk_widget_set_hexpand(state.stack, TRUE);
    gtk_widget_set_vexpand(state.stack, TRUE);
    for (const auto& page : state.page_lifecycle)
    {
        if (page.onLaunch == nullptr)
        {
            continue;
        }
        gtk_stack_add_named(GTK_STACK(state.stack), page.onLaunch(state),
                            page.name);
    }
    gtk_box_append(GTK_BOX(body), state.stack);

    gtk_box_append(GTK_BOX(root), body);
    gtk_box_append(GTK_BOX(root), buildStatusBar(state));
    showPage(state, "map");
    return root;
}

void refreshUi(GtkUConsoleAppState& state)
{
    GtkUConsoleRefreshSnapshot snapshot{
        .dashboard = state.dashboard_model.snapshot(),
        .map = state.map_model.snapshot()};
    serviceMapDownloads(state, snapshot.map);
    snapshot.map = state.map_model.snapshot();
    setStatusChip(state.status_aio2, findHardware(snapshot.dashboard, "AIO2"));
    setStatusChip(state.status_lora, findHardware(snapshot.dashboard, "LoRa"));
    setStatusChip(state.status_gps, findHardware(snapshot.dashboard, "GPS"));
    setLabel(state.status_node, "Node: " + snapshot.dashboard.self_node);
    setBadgeCount(state.nav_chat_badge, snapshot.dashboard.unread_count);
    setLabel(state.status_unread,
             "Unread: " + std::to_string(snapshot.dashboard.unread_count));

    if (const auto* page = findPage(state, state.active_page);
        page != nullptr && page->onRefresh != nullptr)
    {
        page->onRefresh(state, snapshot);
    }
}

gboolean onRefresh(gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    state.services.tick();
    refreshUi(state);
    return G_SOURCE_CONTINUE;
}

void shutdownGtkUConsoleApp(GtkUConsoleAppState& state)
{
    if (state.shutdown_complete)
    {
        return;
    }
    state.shutdown_complete = true;

    if (state.refresh_source != 0)
    {
        g_source_remove(state.refresh_source);
        state.refresh_source = 0;
    }
    for (auto it = state.page_lifecycle.rbegin();
         it != state.page_lifecycle.rend();
         ++it)
    {
        if (it->onDestroy != nullptr)
        {
            it->onDestroy(state);
        }
    }
    state.services.shutdown();
}

void onWindowDestroy(GtkWidget*, gpointer data)
{
    shutdownGtkUConsoleApp(*static_cast<GtkUConsoleAppState*>(data));
}

} // namespace trailmate::uconsole::gtk
