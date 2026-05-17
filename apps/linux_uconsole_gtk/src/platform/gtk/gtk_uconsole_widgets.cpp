#include "platform/gtk/gtk_uconsole_widgets.h"

#include <cstdint>
#include <string>

namespace trailmate::uconsole::gtk
{

GtkWidget* makeLabel(const char* text,
                     const char* css_class,
                     bool wrap)
{
    GtkWidget* label = gtk_label_new(text ? text : "");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
    gtk_label_set_yalign(GTK_LABEL(label), 0.5F);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    if (wrap)
    {
        gtk_label_set_wrap(GTK_LABEL(label), TRUE);
        gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_NONE);
    }
    if (css_class != nullptr)
    {
        gtk_widget_add_css_class(label, css_class);
    }
    return label;
}

void constrainLabelWidth(GtkWidget* label, int width_chars)
{
    if (label == nullptr || width_chars <= 0)
    {
        return;
    }
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_NONE);
    gtk_label_set_width_chars(GTK_LABEL(label), width_chars);
    gtk_label_set_max_width_chars(GTK_LABEL(label), width_chars);
    gtk_widget_set_hexpand(label, FALSE);
}

void setLabel(GtkWidget* label, const std::string& text)
{
    if (label != nullptr)
    {
        gtk_label_set_text(GTK_LABEL(label), text.c_str());
    }
}

void setLabel(GtkWidget* label, const char* text)
{
    if (label != nullptr)
    {
        gtk_label_set_text(GTK_LABEL(label), text ? text : "");
    }
}

GtkWidget* makeBox(GtkOrientation orientation, int spacing)
{
    GtkWidget* box = gtk_box_new(orientation, spacing);
    gtk_widget_set_hexpand(box, TRUE);
    return box;
}

void clearBox(GtkWidget* box)
{
    if (box == nullptr) return;
    while (GtkWidget* child = gtk_widget_get_first_child(box))
    {
        gtk_box_remove(GTK_BOX(box), child);
    }
}

void clearListBox(GtkWidget* list)
{
    if (list == nullptr) return;
    while (GtkWidget* child = gtk_widget_get_first_child(list))
    {
        gtk_list_box_remove(GTK_LIST_BOX(list), child);
    }
}

void clearGrid(GtkWidget* grid)
{
    if (grid == nullptr) return;
    while (GtkWidget* child = gtk_widget_get_first_child(grid))
    {
        gtk_grid_remove(GTK_GRID(grid), child);
    }
}

void clearFixed(GtkWidget* fixed)
{
    if (fixed == nullptr) return;
    while (GtkWidget* child = gtk_widget_get_first_child(fixed))
    {
        gtk_fixed_remove(GTK_FIXED(fixed), child);
    }
}

GtkWidget* makePanel()
{
    GtkWidget* panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(panel, "panel");
    gtk_widget_set_hexpand(panel, TRUE);
    return panel;
}

GtkWidget* makeRowBox(bool active)
{
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(row, "row");
    if (active)
    {
        gtk_widget_add_css_class(row, "row-active");
    }
    gtk_widget_set_hexpand(row, TRUE);
    return row;
}

GtkWidget* makeListRow(GtkWidget* child, std::size_t index)
{
    GtkWidget* row = gtk_list_box_row_new();
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), child);
    g_object_set_data(G_OBJECT(row), "trailmate-index",
                      GUINT_TO_POINTER(static_cast<guint>(index)));
    return row;
}

std::string formatBytes(std::uint64_t bytes)
{
    if (bytes >= 1024ULL * 1024ULL)
    {
        return std::to_string(bytes / (1024ULL * 1024ULL)) + " MB";
    }
    if (bytes >= 1024ULL)
    {
        return std::to_string(bytes / 1024ULL) + " KB";
    }
    return std::to_string(bytes) + " B";
}

GtkWidget* makeWorkbench(GtkOrientation orientation, int spacing)
{
    GtkWidget* root = gtk_box_new(orientation, spacing);
    gtk_widget_add_css_class(root, "workbench");
    gtk_widget_set_hexpand(root, TRUE);
    gtk_widget_set_vexpand(root, TRUE);
    return root;
}

GtkWidget* makeMetricCard(const std::string& label,
                          const std::string& value,
                          const std::string& detail,
                          bool attention)
{
    GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(card, "metric-card");
    if (attention)
    {
        gtk_widget_add_css_class(card, "metric-alert");
    }
    gtk_widget_set_hexpand(card, TRUE);

    gtk_box_append(GTK_BOX(card), makeLabel(label.c_str(), "metric-label"));
    gtk_box_append(GTK_BOX(card), makeLabel(value.c_str(), "metric-value"));
    if (!detail.empty())
    {
        gtk_box_append(GTK_BOX(card), makeLabel(detail.c_str(), "row-meta",
                                                true));
    }
    return card;
}

const HardwareStatusItem* findHardware(const UConsoleDashboardSnapshot& snapshot,
                                       const char* name)
{
    for (const auto& item : snapshot.hardware)
    {
        if (item.name == name)
        {
            return &item;
        }
    }
    return nullptr;
}

std::string hardwareText(const HardwareStatusItem* item)
{
    if (item == nullptr)
    {
        return "-";
    }
    return item->name + ": " + item->state;
}

void setStatusChip(GtkWidget* label, const HardwareStatusItem* item)
{
    if (label == nullptr)
    {
        return;
    }
    setLabel(label, hardwareText(item));
    gtk_widget_remove_css_class(label, "status-alert");
    gtk_widget_remove_css_class(label, "status-ok");
    if (item != nullptr && item->attention)
    {
        gtk_widget_add_css_class(label, "status-alert");
    }
    else
    {
        gtk_widget_add_css_class(label, "status-ok");
    }
}

void setAttentionClass(GtkWidget* widget, bool attention)
{
    if (widget == nullptr)
    {
        return;
    }
    if (attention)
    {
        gtk_widget_add_css_class(widget, "panel-attention");
    }
    else
    {
        gtk_widget_remove_css_class(widget, "panel-attention");
    }
}
void setBadgeCount(GtkWidget* badge, int count)
{
    if (badge == nullptr)
    {
        return;
    }
    if (count <= 0)
    {
        gtk_widget_set_visible(badge, FALSE);
        setLabel(badge, "");
        return;
    }
    if (count > 99)
    {
        setLabel(badge, "99+");
    }
    else
    {
        setLabel(badge, std::to_string(count));
    }
    gtk_widget_set_visible(badge, TRUE);
}
GtkWidget* buildDetailsWorkspace(const char* title,
                                 const char* subtitle,
                                 GtkWidget** out_box)
{
    (void)title;
    (void)subtitle;
    *out_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_hexpand(*out_box, TRUE);
    gtk_widget_set_vexpand(*out_box, TRUE);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), *out_box);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_widget_set_vexpand(scroll, TRUE);

    GtkWidget* root = makeWorkbench(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(root), scroll);
    return root;
}
GtkWidget* buildDetailRow(const std::string& title,
                          const std::string& detail,
                          bool attention)
{
    GtkWidget* row = makeRowBox();
    if (attention)
    {
        gtk_widget_add_css_class(row, "hardware-card-alert");
    }
    gtk_box_append(GTK_BOX(row), makeLabel(title.c_str(), "row-title"));
    gtk_box_append(GTK_BOX(row), makeLabel(detail.c_str(), "row-meta", true));
    return row;
}

} // namespace trailmate::uconsole::gtk
