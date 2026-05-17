#include "platform/gtk/gtk_uconsole_layout_spec.h"
#include "platform/gtk/gtk_uconsole_pages.h"
#include "platform/gtk/gtk_uconsole_widgets.h"

namespace trailmate::uconsole::gtk
{

GtkWidget* launchChatLayout(GtkUConsoleAppState& state)
{
    GtkWidget* root = makeWorkbench(GTK_ORIENTATION_HORIZONTAL, 7);
    gtk_widget_add_css_class(root, "chat-root");

    GtkWidget* conversation_panel = makePanel();
    gtk_widget_add_css_class(conversation_panel, "chat-rail");
    gtk_widget_set_hexpand(conversation_panel, FALSE);
    gtk_widget_set_size_request(conversation_panel,
                                layout_spec::kChatConversationRailWidth,
                                -1);
    gtk_widget_set_vexpand(conversation_panel, TRUE);

    GtkWidget* rail_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
    GtkWidget* rail_title = makeLabel("Chats", "pane-heading");
    gtk_widget_set_hexpand(rail_title, TRUE);
    gtk_box_append(GTK_BOX(rail_header), rail_title);
    state.chat_sort_combo = gtk_combo_box_text_new();
    gtk_widget_add_css_class(state.chat_sort_combo, "chat-sort");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state.chat_sort_combo),
                                   "Recent");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state.chat_sort_combo),
                                   "Hops");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state.chat_sort_combo),
                                   "Distance");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state.chat_sort_combo),
                                   "Last seen");
    gtk_combo_box_set_active(GTK_COMBO_BOX(state.chat_sort_combo), 0);
    g_signal_connect(state.chat_sort_combo,
                     "changed",
                     G_CALLBACK(onChatSortChanged),
                     &state);
    gtk_box_append(GTK_BOX(rail_header), state.chat_sort_combo);
    gtk_box_append(GTK_BOX(conversation_panel), rail_header);

    state.chat_conversation_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
    gtk_widget_add_css_class(state.chat_conversation_list,
                             "chat-thread-list");
    gtk_widget_set_vexpand(state.chat_conversation_list, TRUE);
    GtkWidget* conversation_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(conversation_scroll),
                                  state.chat_conversation_list);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(conversation_scroll),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(conversation_scroll, TRUE);
    gtk_box_append(GTK_BOX(conversation_panel), conversation_scroll);
    gtk_box_append(GTK_BOX(root), conversation_panel);

    GtkWidget* message_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(message_panel, "chat-main");
    gtk_widget_set_hexpand(message_panel, TRUE);
    gtk_widget_set_vexpand(message_panel, TRUE);
    GtkWidget* thread_header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_add_css_class(thread_header, "chat-titlebar");

    GtkWidget* title_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* thread_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_hexpand(thread_text, TRUE);
    state.chat_title = makeLabel("Chat", "chat-title-line");
    state.chat_meta = makeLabel("", "row-meta");
    gtk_box_append(GTK_BOX(thread_text), state.chat_title);
    gtk_box_append(GTK_BOX(thread_text), state.chat_meta);
    gtk_box_append(GTK_BOX(title_row), thread_text);
    gtk_box_append(GTK_BOX(thread_header), title_row);

    GtkWidget* action_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(action_row, "chat-action-row");
    state.chat_add_contact_button = gtk_button_new_with_label("Contact");
    state.chat_request_nodeinfo_button = gtk_button_new_with_label("NodeInfo");
    state.chat_send_position_button = gtk_button_new_with_label("Position");
    state.chat_send_poi_button = gtk_button_new_with_label("POI");
    gtk_widget_add_css_class(state.chat_add_contact_button,
                             "chat-action-button");
    gtk_widget_add_css_class(state.chat_request_nodeinfo_button,
                             "chat-action-button");
    gtk_widget_add_css_class(state.chat_send_position_button,
                             "chat-action-button");
    gtk_widget_add_css_class(state.chat_send_poi_button,
                             "chat-action-button");
    g_signal_connect(state.chat_add_contact_button,
                     "clicked",
                     G_CALLBACK(onChatAddContactClicked),
                     &state);
    g_signal_connect(state.chat_request_nodeinfo_button,
                     "clicked",
                     G_CALLBACK(onChatRequestNodeInfoClicked),
                     &state);
    g_signal_connect(state.chat_send_position_button,
                     "clicked",
                     G_CALLBACK(onChatSendPositionClicked),
                     &state);
    g_signal_connect(state.chat_send_poi_button,
                     "clicked",
                     G_CALLBACK(onChatSendPoiClicked),
                     &state);
    gtk_box_append(GTK_BOX(action_row), state.chat_add_contact_button);
    gtk_box_append(GTK_BOX(action_row), state.chat_request_nodeinfo_button);
    gtk_box_append(GTK_BOX(action_row), state.chat_send_position_button);
    gtk_box_append(GTK_BOX(action_row), state.chat_send_poi_button);
    gtk_box_append(GTK_BOX(thread_header), action_row);
    gtk_box_append(GTK_BOX(message_panel), thread_header);

    state.chat_message_list = gtk_list_box_new();
    gtk_widget_add_css_class(state.chat_message_list, "chat-transcript");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(state.chat_message_list),
                                    GTK_SELECTION_NONE);
    state.chat_message_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(state.chat_message_scroll),
                                  state.chat_message_list);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(state.chat_message_scroll),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(state.chat_message_scroll, TRUE);
    gtk_box_append(GTK_BOX(message_panel), state.chat_message_scroll);

    GtkWidget* composer_shell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(composer_shell, "chat-composer-shell");
    GtkWidget* composer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
    gtk_widget_add_css_class(composer, "chat-composer");
    state.chat_entry = gtk_entry_new();
    gtk_widget_add_css_class(state.chat_entry, "chat-entry");
    gtk_entry_set_placeholder_text(GTK_ENTRY(state.chat_entry), "Message");
    gtk_widget_set_hexpand(state.chat_entry, TRUE);
    g_signal_connect(state.chat_entry, "activate",
                     G_CALLBACK(onChatEntryActivate), &state);
    state.chat_send_button = gtk_button_new_with_label("Send");
    gtk_widget_add_css_class(state.chat_send_button, "chat-send");
    g_signal_connect(state.chat_send_button, "clicked",
                     G_CALLBACK(onSendClicked), &state);
    gtk_box_append(GTK_BOX(composer), state.chat_entry);
    gtk_box_append(GTK_BOX(composer), state.chat_send_button);
    gtk_box_append(GTK_BOX(composer_shell), composer);

    state.chat_status = makeLabel("Ready.", "chat-action-status");
    gtk_box_append(GTK_BOX(composer_shell), state.chat_status);
    gtk_box_append(GTK_BOX(message_panel), composer_shell);
    gtk_box_append(GTK_BOX(root), message_panel);

    GtkWidget* node_panel = makePanel();
    gtk_widget_add_css_class(node_panel, "chat-node-panel");
    gtk_widget_set_hexpand(node_panel, FALSE);
    gtk_widget_set_size_request(node_panel,
                                layout_spec::kChatNodeInspectorWidth,
                                -1);
    gtk_widget_set_vexpand(node_panel, TRUE);
    gtk_box_append(GTK_BOX(node_panel),
                   makeLabel("Nodes", "pane-heading"));
    state.chat_node_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(state.chat_node_box, "chat-node-list");
    gtk_widget_set_vexpand(state.chat_node_box, TRUE);
    GtkWidget* node_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(node_scroll),
                                  state.chat_node_box);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(node_scroll),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(node_scroll, TRUE);
    gtk_box_append(GTK_BOX(node_panel), node_scroll);
    gtk_box_append(GTK_BOX(root), node_panel);
    return root;
}

} // namespace trailmate::uconsole::gtk
