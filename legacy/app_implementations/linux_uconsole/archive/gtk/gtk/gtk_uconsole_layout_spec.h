#pragma once

namespace trailmate::uconsole::gtk::layout_spec
{

// These constants mirror docs/specs/uconsole-aio2-linux.md.
// Update the specification before changing product geometry here.

constexpr int kGlobalStatusBarHeight = 28;

constexpr int kOverviewGpsRailWidth = 208;
constexpr int kOverviewTimelineRailWidth = 252;
constexpr int kOverviewLocationMapWidth = 200;
constexpr int kOverviewLocationMapHeight = 128;
constexpr int kOverviewLocationPictureHeight = 104;
constexpr int kOverviewSkyplotWidth = 200;
constexpr int kOverviewSkyplotHeight = 120;
constexpr int kOverviewSatelliteListWidth = 200;
constexpr int kOverviewSatelliteListHeight = 132;

constexpr int kChatConversationRailWidth = 216;
constexpr int kChatNodeInspectorWidth = 220;

constexpr int kMapSideRailWidth = 152;
constexpr int kMapRailTextWidthChars = 15;
constexpr int kMapStatusValueMaxChars = 28;

} // namespace trailmate::uconsole::gtk::layout_spec
