#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <gtk/gtk.h>

#include "platform/gtk/gtk_uconsole_app_state.h"

namespace trailmate::uconsole::gtk
{

GtkWidget* makeLabel(const char* text,
                     const char* css_class = nullptr,
                     bool wrap = false);
void constrainLabelWidth(GtkWidget* label, int width_chars);
void setLabel(GtkWidget* label, const std::string& text);
void setLabel(GtkWidget* label, const char* text);
GtkWidget* makeBox(GtkOrientation orientation, int spacing);
void clearBox(GtkWidget* box);
void clearListBox(GtkWidget* list);
void clearGrid(GtkWidget* grid);
void clearFixed(GtkWidget* fixed);
GtkWidget* makePanel();
GtkWidget* makeRowBox(bool active = false);
GtkWidget* makeListRow(GtkWidget* child, std::size_t index);
GtkWidget* makeWorkbench(GtkOrientation orientation, int spacing);
GtkWidget* makeMetricCard(const std::string& label,
                          const std::string& value,
                          const std::string& detail,
                          bool attention = false);
GtkWidget* buildDetailsWorkspace(const char* title,
                                 const char* subtitle,
                                 GtkWidget** out_box);
GtkWidget* buildDetailRow(const std::string& title,
                          const std::string& detail,
                          bool attention = false);
std::string formatBytes(std::uint64_t bytes);
const HardwareStatusItem* findHardware(const UConsoleDashboardSnapshot& snapshot,
                                       const char* name);
void setStatusChip(GtkWidget* label, const HardwareStatusItem* item);
void setAttentionClass(GtkWidget* widget, bool attention);
void setBadgeCount(GtkWidget* badge, int count);

} // namespace trailmate::uconsole::gtk
