#include "platform/gtk/gtk_uconsole_style.h"

#include <gtk/gtk.h>

namespace trailmate::uconsole::gtk
{

constexpr const char* kCss = R"CSS(
window {
  background: #e8ebe5;
  color: #1b211e;
}
.menu-bar {
  background: #1d2221;
  color: #f6f8f4;
  padding: 3px 8px;
  min-height: 28px;
  border-bottom: 1px solid #111514;
}
.menu-title {
  color: #f8faf7;
  font-weight: 700;
  padding: 0 6px;
}
.menu-button {
  background: transparent;
  color: #dce3de;
  border: 1px solid transparent;
  border-radius: 4px;
  padding: 3px 8px;
}
.menu-badge {
  background: #dfeee7;
  color: #0e322b;
  border-radius: 999px;
  padding: 0 6px;
  font-size: 11px;
  font-weight: 700;
}
.chip {
  border-radius: 6px;
  padding: 3px 8px;
  color: #eef5f1;
  background: #3a4f4a;
}
.mini-chip {
  border-radius: 999px;
  padding: 1px 6px;
  color: #eef5f1;
  background: #3a4f4a;
  font-size: 11px;
  font-weight: 700;
}
.body {
  background: #e8ebe5;
  padding: 6px;
}
.nav-button {
  background: #f7f9f6;
  color: #202926;
  border: 1px solid #c8d1c7;
  border-radius: 5px;
  padding: 4px 9px;
}
.nav-button-active {
  background: #dfeee7;
  color: #12241e;
  border-color: #6f9f8d;
  font-weight: 700;
}
.workbench {
  background: #e8ebe5;
}
.panel {
  background: #fbfcfa;
  border: 1px solid #cbd4ca;
  border-radius: 6px;
  padding: 8px;
}
.pane {
  background: #fbfcfa;
  border: 1px solid #cbd4ca;
  border-radius: 6px;
  padding: 8px;
}
.pane-primary {
  background: #ffffff;
}
.rail-pane {
  background: #f4f7f2;
}
.inspector-pane {
  background: #f8faf6;
}
.pane-heading {
  color: #202926;
  font-weight: 700;
}
.pane-caption {
  color: #64706a;
  font-size: 12px;
}
.panel-attention {
  background: #fff7eb;
  border-color: #c47a25;
}
.overview-grid,
.detail-grid {
  padding: 0;
}
.overview-panel-title {
  color: #202926;
  font-weight: 700;
}
.overview-summary-panel {
  background: #ffffff;
}
.overview-recent-panel {
  background: #f7f9f6;
}
.summary-title {
  font-size: 17px;
  font-weight: 700;
}
.summary-detail {
  color: #5e6a65;
  font-size: 12px;
}
.location-map {
  background: #dce4dd;
  border: 1px solid #b4c1b7;
  border-radius: 5px;
  padding: 4px;
}
.location-picture {
  border-radius: 4px;
}
.gnss-skyplot {
  background: #f7faf5;
  border: 1px solid #c5d0c7;
  border-radius: 6px;
}
.gnss-satellite-list {
  padding: 0;
}
.gnss-sat-row {
  background: #ffffff;
  border: 1px solid #d6ded5;
  border-left: 4px solid #6d7771;
  border-radius: 5px;
  padding: 4px 6px;
}
.gnss-sat-used {
  background: #f6fbf7;
}
.gnss-signal-good {
  border-left-color: #2f7a54;
}
.gnss-signal-fair {
  border-left-color: #bf8b22;
}
.gnss-signal-weak {
  border-left-color: #b5523e;
}
.gnss-signal-idle {
  border-left-color: #7a8580;
}
.gnss-sat-title {
  color: #202926;
  font-weight: 700;
  font-size: 11px;
}
.gnss-sat-meta {
  color: #58645f;
  font-size: 11px;
}
.metric-strip {
  background: #dfe5dd;
  border: 1px solid #c8d2c8;
  border-radius: 6px;
  padding: 5px;
}
.metric-card {
  background: #fbfcfa;
  border: 1px solid #cbd4ca;
  border-radius: 5px;
  padding: 7px 8px;
}
.metric-alert {
  background: #fff4e4;
  border-color: #c47a25;
}
.metric-value {
  font-size: 20px;
  font-weight: 700;
}
.metric-label {
  color: #66726d;
  font-size: 12px;
}
.timeline-list {
  padding: 0;
}
.overview-timeline-panel {
  background: #f6f7f3;
}
.timeline-filter {
  min-width: 82px;
}
.timeline-jump-button {
  border-radius: 5px;
  padding: 3px 9px;
}
.timeline-row {
  background: #ffffff;
  border: 1px solid #d5ddd4;
  border-left: 4px solid #2f7768;
  border-radius: 5px;
  padding: 6px 8px;
}
.timeline-row-team {
  border-left-color: #8458a8;
}
.timeline-row-direct {
  background: #ffffff;
}
.timeline-row-broadcast {
  background: #fbfcfa;
}
.timeline-row-outgoing {
  border-right: 3px solid #4b7a5c;
}
.timeline-kind-position {
  border-left-color: #2671a5;
}
.timeline-kind-telemetry {
  border-left-color: #996f16;
}
.timeline-kind-node {
  border-left-color: #3f7c45;
}
.timeline-kind-message {
  border-left-color: #2f685e;
}
.timeline-kind-system {
  border-left-color: #626e68;
}
.timeline-time,
.timeline-badge-team,
.timeline-badge-mesh,
.timeline-badge-direct,
.timeline-badge-broadcast,
.timeline-badge-kind {
  border-radius: 999px;
  padding: 1px 6px;
  font-size: 11px;
  font-weight: 700;
}
.timeline-time {
  background: #e7ece8;
  color: #2e3834;
}
.timeline-badge-team {
  background: #efe5f6;
  color: #50306b;
}
.timeline-badge-mesh {
  background: #dfeee7;
  color: #163d35;
}
.timeline-badge-direct {
  background: #e3edf6;
  color: #204a68;
}
.timeline-badge-broadcast {
  background: #f3ead4;
  color: #604816;
}
.timeline-badge-kind {
  background: #edf0ed;
  color: #3c4641;
}
.timeline-row-alert {
  background: #fff5e8;
  border-left-color: #b7651f;
}
.recent-contact-list {
  padding: 0;
}
.recent-contact-row {
  background: #ffffff;
  border: 1px solid #d5ddd4;
  border-radius: 6px;
  padding: 7px 8px;
}
.recent-contact-unread {
  border-left: 4px solid #2f685e;
}
.recent-contact-chat {
  border-radius: 5px;
  padding: 2px 8px;
}
.detail-panel {
  min-width: 260px;
}
.row {
  background: #ffffff;
  border: 1px solid #d5ddd4;
  border-radius: 5px;
  padding: 7px 8px;
}
.row-active {
  background: #e6f0e9;
  border-color: #6f9f8d;
}
.row-title {
  font-weight: 700;
  color: #202926;
}
.row-meta {
  color: #66726d;
  font-size: 12px;
}
.message-out {
  background: #edf7f1;
}
.message-in {
  background: #ffffff;
}
.message-failed {
  background: #fff0ee;
}
.chat-root {
  background: #e8ebe5;
}
.chat-rail {
  background: #f5f7f3;
  padding: 7px;
}
.chat-sort {
  min-width: 84px;
}
.chat-thread-list {
  background: transparent;
}
.chat-thread-list row {
  background: transparent;
  padding: 2px 0;
}
.chat-thread-row {
  background: #ffffff;
  border: 1px solid #d5ddd4;
  border-radius: 6px;
  padding: 7px 8px;
}
.chat-thread-button {
  background: transparent;
  border: none;
  padding: 0;
}
.chat-thread-active {
  background: #e7f0ea;
  border-color: #6f9f8d;
}
.chat-thread-team {
  border-left: 4px solid #8458a8;
}
.chat-thread-broadcast {
  border-left: 4px solid #b08b2d;
}
.chat-thread-title {
  color: #202926;
  font-weight: 700;
}
.chat-thread-preview {
  color: #2d3733;
}
.chat-thread-unread {
  background: #2f685e;
  color: #f5fbf7;
  border-radius: 999px;
  padding: 1px 6px;
  font-size: 11px;
  font-weight: 700;
}
.chat-thread-unread-source {
  color: #1f5c51;
  font-size: 12px;
  font-weight: 700;
}
.chat-thread-facts {
  color: #3e5e58;
  font-size: 12px;
  font-weight: 700;
}
.chat-group {
  background: transparent;
}
.chat-group-list {
  padding: 5px 0 0 0;
}
.chat-main {
  background: #f9fbf8;
  border: 1px solid #cbd4ca;
  border-radius: 6px;
  padding: 0;
}
.chat-titlebar {
  background: #ffffff;
  border-bottom: 1px solid #d5ddd4;
  border-radius: 6px 6px 0 0;
  padding: 7px 10px;
}
.chat-action-row {
  padding: 0;
}
.chat-action-button {
  border-radius: 5px;
  padding: 3px 8px;
}
.chat-title-line {
  color: #19231f;
  font-size: 15px;
  font-weight: 700;
}
.chat-transcript {
  background: #eef2ed;
  padding: 8px 0;
}
.chat-transcript row {
  background: transparent;
  border: none;
  padding: 0;
}
.chat-message-shell {
  background: transparent;
  border: none;
  padding: 2px 8px;
}
.chat-message-row {
  background: transparent;
  padding: 0;
}
.chat-bubble {
  border: 1px solid #d1dbd3;
  border-radius: 7px;
  padding: 6px 8px;
}
.chat-bubble-in {
  background: #ffffff;
}
.chat-bubble-out {
  background: #dceee5;
  border-color: #9dc3b5;
}
.chat-bubble-failed {
  background: #fff0ee;
  border-color: #d59a90;
}
.chat-sender {
  color: #51605a;
  font-size: 11px;
  font-weight: 700;
}
.chat-text {
  color: #1d2521;
}
.chat-message-meta {
  color: #6a746f;
  font-size: 11px;
}
.chat-composer-shell {
  background: #ffffff;
  border-top: 1px solid #d5ddd4;
  border-radius: 0 0 6px 6px;
  padding: 7px 9px 6px 9px;
}
.chat-composer {
  padding: 0;
}
.chat-entry {
  min-height: 30px;
}
.chat-action-status {
  color: #5d6862;
  font-size: 11px;
}
.chat-send {
  border-radius: 6px;
  padding: 5px 13px;
}
.chat-node-panel {
  background: #f4f7f2;
  padding: 7px;
}
.chat-node-list {
  padding: 0;
}
.chat-node-card {
  background: #ffffff;
  border: 1px solid #d5ddd4;
  border-left: 3px solid #2f685e;
  border-radius: 6px;
  padding: 7px 8px;
}
.chat-node-mqtt {
  border-left-color: #7e4aa3;
}
.chat-node-position {
  color: #1f5c51;
  font-size: 12px;
  font-weight: 700;
}
.chat-node-actions {
  padding: 3px 0 0 0;
}
.chat-node-action {
  border-radius: 5px;
  padding: 2px 6px;
  font-size: 11px;
}
.node-info-dialog {
  background: #f3f6f1;
}
.node-info-dialog-body {
  padding: 12px;
}
.node-info-title {
  color: #17231f;
  font-size: 18px;
  font-weight: 700;
}
.node-info-section {
  background: #ffffff;
  border: 1px solid #d5ddd4;
  border-radius: 6px;
  padding: 8px;
}
.node-info-section-title {
  color: #202926;
  font-weight: 700;
}
.node-info-row {
  padding: 2px 0;
}
.node-info-key {
  color: #66726d;
  font-size: 12px;
  font-weight: 700;
}
.node-info-value {
  color: #1f2925;
  font-size: 12px;
}
.node-info-value-attention {
  color: #8a3f00;
  font-size: 12px;
  font-weight: 700;
}
.node-info-map-stage {
  background: #89968e;
  border: 1px solid #b7c1b8;
  border-radius: 6px;
}
.node-info-map-grid {
  background: #89968e;
}
.node-info-map-tile {
  background: transparent;
  border: none;
  padding: 0;
}
.node-info-map-tile-pending {
  background: #c8d0c8;
}
.node-info-marker-node,
.node-info-marker-self {
  border-radius: 999px;
  padding: 2px 7px;
  font-size: 11px;
  font-weight: 700;
}
.node-info-marker-node {
  background: rgba(255, 248, 221, 0.96);
  color: #493711;
  border: 2px solid #c28f2c;
}
.node-info-marker-self {
  background: rgba(224, 247, 239, 0.96);
  color: #0e3e37;
  border: 2px solid #1d685e;
}
.node-info-map-id,
.node-info-map-lon,
.node-info-map-lat,
.node-info-distance {
  background: rgba(28, 35, 32, 0.82);
  color: #eef4ef;
  border-radius: 4px;
  padding: 2px 6px;
  font-size: 12px;
  font-weight: 700;
}
.node-info-distance {
  background: rgba(255, 255, 255, 0.92);
  color: #18312b;
  border: 1px solid rgba(20, 66, 58, 0.34);
}
.node-info-map-panel {
  background: rgba(28, 35, 32, 0.86);
  border: 1px solid rgba(238, 244, 239, 0.18);
  border-radius: 6px;
  padding: 6px;
}
.node-info-map-protocol,
.node-info-map-rssi,
.node-info-map-snr,
.node-info-map-seen {
  color: #eef4ef;
  font-size: 12px;
  font-weight: 700;
}
.node-info-map-rssi {
  color: #f3df9b;
}
.node-info-map-snr {
  color: #a6d5ef;
}
.node-info-map-seen {
  color: #cfd9d2;
}
.node-info-map-empty {
  color: #50605a;
}
.empty-state {
  color: #66726d;
  padding: 12px;
}
.hardware-grid {
  padding: 0;
}
.hardware-card {
  background: #fbfcfa;
  border: 1px solid #cbd4ca;
  border-radius: 5px;
  padding: 7px 8px;
}
.hardware-card-alert {
  background: #fff4e4;
  border-color: #c47a25;
}
.hardware-state {
  font-size: 16px;
  font-weight: 700;
}
.hardware-state-alert {
  color: #8a3f00;
}
.statusbar {
  background: #1d2221;
  color: #e8eee9;
  padding: 4px 8px;
  min-height: 24px;
}
.status-chip {
  border-radius: 4px;
  padding: 2px 7px;
  background: #2d3633;
  color: #dce4df;
}
.status-alert {
  background: #713828;
  color: #fff4ed;
}
.status-ok {
  background: #2f685e;
  color: #ecf4ef;
}
.map-canvas {
  background: #89968e;
}
.map-side-panel,
.map-tools-panel {
  background: #f8faf6;
  padding: 6px 6px 32px 6px;
}
.map-tool-section {
  background: #ffffff;
  border: 1px solid #d5ddd4;
  border-radius: 6px;
  padding: 6px;
}
.map-tool-title {
  color: #202926;
  font-weight: 700;
  font-size: 12px;
}
.map-grid {
  background: #89968e;
  padding: 0;
}
.map-contour-grid {
  background: transparent;
  padding: 0;
}
.tile-cell {
  background: transparent;
  border: none;
  border-radius: 0;
  padding: 0;
}
.map-contour-cell {
  background: transparent;
  border: none;
  padding: 0;
}
.map-tile-pending {
  background: #c8d0c8;
}
.map-overlay-panel {
  background: rgba(28, 35, 32, 0.90);
  color: #eef4ef;
  border: 1px solid rgba(238, 244, 239, 0.20);
  border-radius: 6px;
  padding: 7px;
}
.map-overlay-panel .row-title,
.map-overlay-panel .pane-heading {
  color: #f6faf7;
}
.map-overlay-panel .row-meta {
  color: #cbd8d0;
}
.map-tool-row {
  padding: 0;
}
.map-tool-row switch {
  min-width: 44px;
}
.map-side-panel button,
.map-tools-panel button {
  padding: 3px 6px;
}
.map-marker {
  background: rgba(224, 247, 239, 0.92);
  color: #0e3e37;
  border: 2px solid #1d685e;
  border-radius: 999px;
  padding: 1px 6px;
  font-weight: 700;
}
.map-marker-mqtt {
  background: rgba(249, 235, 255, 0.92);
  color: #44205d;
  border: 2px solid #8b4fb2;
  border-radius: 999px;
  padding: 1px 6px;
  font-weight: 700;
}
.map-marker-local {
  background: rgba(255, 248, 221, 0.92);
  color: #493711;
  border: 2px solid #c28f2c;
  border-radius: 999px;
  padding: 1px 6px;
  font-weight: 700;
}
.map-marker-measure {
  background: rgba(255, 255, 255, 0.96);
  color: #19231f;
  border: 2px solid #1c5f91;
  border-radius: 999px;
  padding: 1px 6px;
  font-weight: 700;
}
.map-node-marker-button {
  min-height: 0;
  padding: 1px 6px;
}
.map-node-bubble {
  background: rgba(255, 255, 255, 0.96);
  color: #1b211e;
  border: 1px solid rgba(42, 54, 48, 0.26);
  border-left: 4px solid #2f685e;
  border-radius: 6px;
  padding: 7px;
  min-width: 166px;
}
.map-context-menu {
  background: #fbfcfa;
  border: 1px solid #aebaae;
  border-radius: 6px;
  padding: 6px;
}
.source-button-active {
  background: #dfeee7;
  color: #15251f;
  border-color: #6f9f8d;
}
.settings-section {
  background: #fbfcfa;
  border: 1px solid #cbd4ca;
  border-radius: 6px;
  padding: 8px;
}
.settings-row {
  background: #ffffff;
  border: 1px solid #d7ded5;
  border-radius: 5px;
  padding: 6px 8px;
}
.settings-control {
  min-width: 172px;
}
.settings-switch {
  min-width: 48px;
}
.settings-inline-control {
  min-width: 172px;
}
.settings-inline-control > entry,
.settings-inline-control > passwordentry {
  min-width: 172px;
}
.settings-actions {
  background: #f4f7f2;
  border: 1px solid #cbd4ca;
  border-radius: 6px;
  padding: 5px 7px;
}
.settings-body {
  padding: 0;
}
.settings-sidebar {
  min-width: 144px;
  background: #f4f7f2;
  border: 1px solid #cbd4ca;
  border-radius: 6px;
}
.settings-status {
  color: #315f57;
  font-size: 12px;
}
.log-toolbar {
  background: #f4f7f2;
  border: 1px solid #cbd4ca;
  border-radius: 6px;
  padding: 5px 7px;
}
.log-entry {
  background: #ffffff;
  border: 1px solid #d5ddd4;
  border-radius: 5px;
  padding: 7px 8px;
}
.log-entry-header {
  border-spacing: 7px;
}
.log-time {
  color: #52615a;
  font-family: monospace;
  font-size: 12px;
}
.log-source,
.log-direction {
  background: #e7efe7;
  color: #26352e;
  border: 1px solid #c8d5c7;
  border-radius: 4px;
  padding: 1px 5px;
  font-family: monospace;
  font-size: 12px;
}
.log-segments {
  padding: 3px 0;
}
.log-hex {
  font-family: monospace;
  color: #303b37;
  background: #f1f4ef;
  border-radius: 4px;
  padding: 5px 6px;
}
.log-segment-header {
  color: #155c8a;
  font-family: monospace;
  font-weight: 700;
}
.log-segment-body {
  color: #2b6a34;
  font-family: monospace;
}
.log-segment-checksum {
  color: #8c4a13;
  font-family: monospace;
  font-weight: 700;
}
.log-segment-meta {
  color: #5f6671;
  font-family: monospace;
}
.log-segment-error {
  color: #9b2f22;
  font-family: monospace;
  font-weight: 700;
}
button.send {
  border-radius: 6px;
  padding: 6px 13px;
}
)CSS";
void installCss()
{
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, kCss, -1);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

} // namespace trailmate::uconsole::gtk
