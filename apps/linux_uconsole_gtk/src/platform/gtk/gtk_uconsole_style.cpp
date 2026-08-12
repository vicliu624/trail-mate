#include "platform/gtk/gtk_uconsole_style.h"

#include <gtk/gtk.h>

namespace trailmate::uconsole::gtk
{

constexpr const char* kCss = R"CSS(
window {
  background: #e8ebe5;
  color: #1b211e;
}
.uconsole-titlebar {
  background: #1d2221;
  color: #f6f8f4;
  min-height: 30px;
  padding: 0 5px;
  border-bottom: 1px solid #39413e;
}
.titlebar-title {
  color: #f6f8f4;
  font-size: 12px;
  font-weight: 700;
}
.titlebar-button {
  min-width: 32px;
  min-height: 26px;
  padding: 0 7px;
  margin: 2px 0 2px 2px;
  border: 1px solid transparent;
  border-radius: 4px;
  background: transparent;
  color: #dce3de;
  font-size: 15px;
}
.titlebar-button:hover,
.titlebar-button:focus-visible {
  background: #2a312f;
  border-color: #52615a;
}
.titlebar-close:hover,
.titlebar-close:focus-visible {
  background: #a84635;
  border-color: #d56e5b;
  color: #ffffff;
}
.navigation-rail {
  background: #1d2221;
  color: #f6f8f4;
  padding: 10px 9px 9px 9px;
  border-right: 1px solid #111514;
}
.navigation-brand {
  padding: 2px 7px 8px 7px;
}
.navigation-subtitle {
  color: #9da9a3;
  font-size: 11px;
}
.navigation-section-label {
  color: #7f8e87;
  font-size: 10px;
  font-weight: 700;
  letter-spacing: 0.08em;
  padding: 7px 7px 2px 7px;
}
.navigation-footer {
  border-top: 1px solid #39413e;
  padding: 7px 7px 2px 7px;
}
.navigation-footer-title {
  color: #d5ddd8;
  font-size: 10px;
  font-weight: 700;
  letter-spacing: 0.06em;
}
.menu-title {
  color: #f8faf7;
  font-weight: 700;
  font-size: 16px;
}
.menu-button {
  background: transparent;
  color: #dce3de;
  border: 1px solid transparent;
  border-radius: 5px;
  padding: 5px 9px;
  min-height: 18px;
}
.menu-button:hover {
  background: #2a312f;
  border-color: #3d4844;
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
  background: #d9e8df;
  color: #10271f;
  border-color: #7ca18f;
  font-weight: 700;
}
.workbench {
  background: #e8ebe5;
}
.panel {
  background: transparent;
  border: none;
  border-radius: 0;
  padding: 4px 0;
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
  background: transparent;
  border: none;
  border-bottom: 1px solid #d6ded5;
  border-left: 4px solid #6d7771;
  border-radius: 0;
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
  background: transparent;
  border: none;
  border-bottom: 1px solid #cbd4ca;
  border-radius: 0;
  padding: 7px 4px;
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
  background: transparent;
  border: none;
  border-bottom: 1px solid #cbd4ca;
  border-radius: 0;
  padding: 7px 4px;
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
.map-drawer {
  background: rgba(248, 250, 246, 0.96);
  border-right: 1px solid #aebaae;
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
.map-toolbar {
  background: rgba(29, 34, 33, 0.90);
  border: 1px solid rgba(235, 243, 237, 0.42);
  border-radius: 5px;
  padding: 4px;
}
.map-toolbar-button {
  background: transparent;
  color: #f4f8f5;
  border: 1px solid transparent;
  border-radius: 3px;
  padding: 4px 8px;
  font-weight: 700;
}
.map-toolbar-button:hover,
.map-toolbar-button:focus-visible {
  background: #3b574e;
  border-color: #9fc0ae;
}
.map-side-panel button,
.map-tools-panel button {
  padding: 3px 6px;
}
.field-list,
.tracker-controls,
.tracker-history,
.gps-skyplot-workspace,
.gps-satellite-list {
  background: transparent;
  border: none;
  padding: 4px 0;
}
.field-contact-row {
  background: transparent;
  border: none;
  border-bottom: 1px solid #cbd4ca;
  border-radius: 0;
  padding: 8px 4px;
}
.list-summary,
.receiver-status-row {
  background: #f1e1be;
  border-top: 1px solid #d9b06a;
  border-bottom: 1px solid #d9b06a;
  border-radius: 0;
  padding: 6px 8px;
}
.list-summary-item,
.receiver-status-detail {
  color: #5c472f;
  font-size: 12px;
}
.receiver-status-state,
.tracker-state-active,
.tracker-state-idle {
  color: #2a1a05;
  font-weight: 700;
}
.receiver-status-coordinate {
  color: #2a1a05;
  font-family: monospace;
  font-weight: 700;
}
.tracker-state-active {
  color: #245c2b;
}
.tracker-state-idle {
  color: #6a5646;
}
.hardware-table {
  background: transparent;
  border-top: 1px solid #c0aa82;
  border-bottom: 1px solid #c0aa82;
  padding: 0;
}
.hardware-table-header {
  color: #5c472f;
  background: #f1e1be;
  border-bottom: 1px solid #c0aa82;
  padding: 6px 8px;
  font-size: 11px;
  font-weight: 700;
}
.hardware-table-name,
.hardware-table-state {
  color: #2a1a05;
  border-bottom: 1px solid #dfceb0;
  padding: 7px 8px;
  font-weight: 700;
}
.hardware-capabilities {
  border-top: 1px solid #c0aa82;
  padding: 8px 0;
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
  background: transparent;
  border: none;
  border-radius: 0;
  padding: 6px 0;
}
.settings-row {
  background: transparent;
  border: none;
  border-bottom: 1px solid #d7ded5;
  border-radius: 0;
  padding: 7px 4px;
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
.shortcut-content {
  background: #fff7e9;
  color: #3a2a1a;
  padding: 12px;
}
.shortcut-title {
  color: #2a1a05;
  font-size: 17px;
  font-weight: 700;
}
.shortcut-list {
  border-top: 1px solid #d9b06a;
  border-bottom: 1px solid #d9b06a;
}
.shortcut-row {
  border-bottom: 1px solid #ead7b7;
  padding: 6px 3px;
}
.shortcut-key {
  background: #f0d3a4;
  color: #2a1a05;
  border: 1px solid #d9b06a;
  border-radius: 3px;
  padding: 2px 5px;
  font-family: monospace;
  font-weight: 700;
}
.shortcut-action {
  color: #5c472f;
}
/* uConsole embedded visual language: warm paper surfaces and amber focus. */
window,
.body,
.workbench,
.chat-root {
  background: #fff3df;
  color: #3a2a1a;
}
.uconsole-titlebar {
  background: #f0d3a4;
  color: #2a1a05;
  border-bottom-color: #d9b06a;
}
.titlebar-title,
.titlebar-button {
  color: #2a1a05;
}
.titlebar-button:hover,
.titlebar-button:focus-visible {
  background: #e8b45f;
  border-color: #c98118;
}
.titlebar-close:hover,
.titlebar-close:focus-visible {
  background: #c94c2c;
  border-color: #a53b24;
}
.navigation-rail {
  background: #fff0d3;
  color: #3a2a1a;
  border-right-color: #d9b06a;
}
.navigation-subtitle,
.navigation-section-label {
  color: #8a6a3a;
}
.menu-title {
  color: #2a1a05;
}
.menu-button {
  color: #3a2a1a;
  background: #fff7e9;
  border-color: #f0d3a4;
}
.menu-button:hover {
  background: #f3d39c;
  border-color: #d9b06a;
}
.panel,
.pane,
.overview-summary-panel,
.overview-recent-panel,
.row,
.recent-contact-row,
.chat-main,
.chat-titlebar,
.log-entry,
.settings-section {
  background: #fff7e9;
  border-color: #d9b06a;
  color: #3a2a1a;
}
.pane-primary,
.overview-location-panel,
.overview-messages-panel {
  background: #fffaf1;
}
.pane-heading,
.overview-panel-title,
.row-title,
.summary-title,
.chat-title-line {
  color: #2a1a05;
}
.pane-caption,
.row-meta,
.summary-detail,
.navigation-footer-title,
.log-time,
.settings-status {
  color: #6a5646;
}
.nav-button-active,
.source-button-active {
  background: #eba341;
  color: #2a1a05;
  border-color: #c98118;
}
.statusbar {
  background: #f0d3a4;
  color: #3a2a1a;
  border-top: 1px solid #d9b06a;
}
.status-chip {
  background: #f3d39c;
  color: #3a2a1a;
}
.status-ok {
  background: #dcefd8;
  color: #245c2b;
}
.status-alert {
  background: #f5d9d1;
  color: #8b2e1d;
}
.map-side-panel,
.map-tools-panel,
.rail-pane,
.inspector-pane,
.settings-sidebar,
.log-toolbar {
  background: #faf0d8;
  border-color: #d9b06a;
}
.map-drawer {
  background: rgba(255, 247, 233, 0.97);
  border-color: #d9b06a;
}
.map-canvas,
.map-grid,
.location-map,
.gnss-skyplot {
  background: #f6e7c8;
  border-color: #d9b06a;
}
.metric-strip {
  background: #f0d3a4;
  border-color: #d9b06a;
}
.metric-card,
.hardware-card,
.settings-row {
  background: #fff7e9;
  border-color: #e8d2ab;
}
button.send {
  border-radius: 6px;
  padding: 6px 13px;
}
/* Workspaces use hierarchy and dividers. Only purpose-built surfaces such as
   map drawers, message bubbles, and status chips receive a card treatment. */
.panel,
.settings-section {
  background: transparent;
  border: none;
  border-radius: 0;
}
.row,
.settings-row,
.field-contact-row,
.gnss-sat-row {
  background: transparent;
  border-radius: 0;
}
.runtime-status-table,
.data-status-table,
.extension-list {
  background: transparent;
  border-top: 1px solid #d9b06a;
  border-bottom: 1px solid #d9b06a;
}
.runtime-status-row,
.data-status-row,
.extension-list-row {
  background: transparent;
  border-bottom: 1px solid #ead7b7;
  border-radius: 0;
  padding: 7px 4px;
}
.runtime-status-row:last-child,
.data-status-row:last-child,
.extension-list-row:last-child {
  border-bottom: none;
}
.runtime-status-tool,
.data-status-category {
  color: #2a1a05;
  font-weight: 700;
}
.runtime-status-value,
.data-status-value {
  color: #5c472f;
  font-family: monospace;
  font-weight: 700;
}
.runtime-status-detail,
.data-status-detail {
  color: #6a5646;
  font-size: 12px;
}
.runtime-status-attention,
.data-status-attention {
  color: #9b2f22;
}
.radio-sweep-workspace,
.radio-tool-section,
.data-operation-section {
  background: transparent;
  border-top: 1px solid #d9b06a;
  border-radius: 0;
  padding: 10px 0;
}
.radio-tool-columns {
  margin-top: 4px;
}
.list-toolbar {
  border-bottom: 1px solid #d9b06a;
  padding: 2px 0 8px;
}
.log-entry {
  background: transparent;
  border: none;
  border-bottom: 1px solid #d9b06a;
  border-radius: 0;
  padding: 8px 0;
}
.log-entry:first-child {
  border-top: 1px solid #d9b06a;
}
.log-source,
.log-direction {
  background: transparent;
  border: none;
  border-radius: 0;
  padding: 0;
}
.log-hex {
  background: #f6e7c8;
  border-radius: 0;
}
.map-side-panel,
.map-tools-panel {
  background: #faf0d8;
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
