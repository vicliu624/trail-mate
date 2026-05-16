#pragma once

#include <vector>

#include <gtk/gtk.h>

#include "platform/gtk/gtk_uconsole_app_state.h"

namespace trailmate::uconsole::gtk
{

std::vector<GtkUConsolePageLifecycle> buildGtkUConsolePageRegistry();

GtkUConsolePageLifecycle makeOverviewPageLifecycle();
GtkWidget* launchOverviewLayout(GtkUConsoleAppState& state);
void refreshOverviewLogic(GtkUConsoleAppState& state,
                          const GtkUConsoleRefreshSnapshot& snapshot);
void drawOverviewGnssSkyplot(GtkDrawingArea* area,
                             cairo_t* cr,
                             int width,
                             int height,
                             gpointer data);
void onOverviewRecentContactClicked(GtkButton*, gpointer data);
void onOverviewTimelineFilterChanged(GtkComboBox*, gpointer data);
void onOverviewTimelineTopClicked(GtkButton*, gpointer data);
void onOverviewTimelineBottomClicked(GtkButton*, gpointer data);

GtkUConsolePageLifecycle makeChatPageLifecycle();
GtkWidget* launchChatLayout(GtkUConsoleAppState& state);
void refreshChatLogic(GtkUConsoleAppState& state,
                      const GtkUConsoleRefreshSnapshot& snapshot);
void onConversationActivated(GtkListBox*, GtkListBoxRow* row, gpointer data);
void onConversationButtonClicked(GtkButton* button, gpointer data);
void onChatSortChanged(GtkComboBox*, gpointer data);
void onChatGroupExpandedChanged(GObject* object, GParamSpec*, gpointer data);
void onSendClicked(GtkButton*, gpointer data);
void onChatEntryActivate(GtkEntry*, gpointer data);
void onChatAddContactClicked(GtkButton*, gpointer data);
void onChatRequestNodeInfoClicked(GtkButton*, gpointer data);
void onChatSendPositionClicked(GtkButton*, gpointer data);
void onChatSendPoiClicked(GtkButton*, gpointer data);
void onChatNodeChatClicked(GtkButton*, gpointer data);
void onChatNodeAddClicked(GtkButton*, gpointer data);
void onChatNodeInfoClicked(GtkButton*, gpointer data);
void onChatNodeIgnoreClicked(GtkButton*, gpointer data);
void onChatNodeExchangeUserInfoClicked(GtkButton*, gpointer data);
void onChatNodeVerifyKeyClicked(GtkButton*, gpointer data);

GtkUConsolePageLifecycle makeMapPageLifecycle();
GtkWidget* launchMapLayout(GtkUConsoleAppState& state);
void refreshMapLogic(GtkUConsoleAppState& state,
                     const GtkUConsoleRefreshSnapshot& snapshot);
void refreshMap(GtkUConsoleAppState& state);
void refreshMap(GtkUConsoleAppState& state, const MapWorkspaceSnapshot& snapshot);
void onMapSourceClicked(GtkButton* button, gpointer data);
void onMapZoomInClicked(GtkButton*, gpointer data);
void onMapZoomOutClicked(GtkButton*, gpointer data);
void onMapRecenterClicked(GtkButton*, gpointer data);
void onMapContextCenterClicked(GtkButton*, gpointer data);
void onMapContextZoomInClicked(GtkButton*, gpointer data);
void onMapContextZoomOutClicked(GtkButton*, gpointer data);
void onMapContextMeasureStartClicked(GtkButton*, gpointer data);
void onMapContextMeasureEndClicked(GtkButton*, gpointer data);
void onMapContextPressed(GtkGestureClick* gesture,
                         int,
                         double x,
                         double y,
                         gpointer data);
void onMapPrimaryPressed(GtkGestureClick* gesture,
                         int,
                         double x,
                         double y,
                         gpointer data);
void onMapDragBegin(GtkGestureDrag*, double, double, gpointer data);
void onMapDragUpdate(GtkGestureDrag*,
                     double offset_x,
                     double offset_y,
                     gpointer data);
void onMapDragEnd(GtkGestureDrag*,
                  double offset_x,
                  double offset_y,
                  gpointer data);
void onMapMqttNodesToggled(GObject*, GParamSpec*, gpointer data);
void onMapContourVisibleToggled(GObject*, GParamSpec*, gpointer data);
void onMapContourFillClicked(GtkButton*, gpointer data);
void onMapMeasureClicked(GtkButton*, gpointer data);
void onMapMeasureClearClicked(GtkButton*, gpointer data);

GtkUConsolePageLifecycle makeHardwarePageLifecycle();
GtkWidget* launchHardwareLayout(GtkUConsoleAppState& state);
void refreshHardwareLogic(GtkUConsoleAppState& state,
                          const GtkUConsoleRefreshSnapshot& snapshot);

GtkUConsolePageLifecycle makeDataPageLifecycle();
GtkWidget* launchDataLayout(GtkUConsoleAppState& state);
void refreshDataLogic(GtkUConsoleAppState& state,
                      const GtkUConsoleRefreshSnapshot& snapshot);

GtkUConsolePageLifecycle makeLogsPageLifecycle();
GtkWidget* launchLogsLayout(GtkUConsoleAppState& state);
void refreshLogsLogic(GtkUConsoleAppState& state,
                      const GtkUConsoleRefreshSnapshot& snapshot);
void onLogsSourceGpsClicked(GtkButton*, gpointer data);
void onLogsSourceLoraClicked(GtkButton*, gpointer data);
void onLogsSourceMqttClicked(GtkButton*, gpointer data);

GtkUConsolePageLifecycle makeSettingsPageLifecycle();
GtkWidget* launchSettingsLayout(GtkUConsoleAppState& state);
void refreshSettingsLogic(GtkUConsoleAppState& state,
                          const GtkUConsoleRefreshSnapshot& snapshot);
int protocolIndex(::chat::MeshProtocol protocol);
std::vector<const char*> meshtasticRegionLabels();
int meshtasticRegionIndex(std::uint8_t region_code);
std::uint8_t meshtasticRegionFromIndex(int index);
std::vector<const char*> meshtasticPresetLabels();
int meshtasticPresetIndex(std::uint8_t preset_value);
std::uint8_t meshtasticPresetFromIndex(int index);
float displayFrequencyMhz(const ::chat::MeshConfig& mesh);
float displayBandwidthKHz(const ::chat::MeshConfig& mesh,
                          ::chat::MeshProtocol protocol);
std::uint8_t displaySpreadFactor(const ::chat::MeshConfig& mesh,
                                 ::chat::MeshProtocol protocol);
std::uint8_t displayCodingRate(const ::chat::MeshConfig& mesh,
                               ::chat::MeshProtocol protocol);
int meshtasticRegionTxPowerLimitDbm(std::uint8_t region_code);
int displayTxPowerDbm(const ::chat::MeshConfig& mesh);
void setMeshtasticTxPowerControl(GtkWidget* control,
                                 const ::chat::MeshConfig& mesh);
void showSettingsNotice(GtkUConsoleAppState& state, const char* text);
void populateSettingsControls(GtkUConsoleAppState& state);
void onSettingsProtocolChanged(GtkComboBox*, gpointer data);
void onSettingsMeshtasticRegionChanged(GtkComboBox*, gpointer data);
void onSettingsApplyClicked(GtkButton*, gpointer data);
void onSettingsReloadClicked(GtkButton*, gpointer data);

} // namespace trailmate::uconsole::gtk
