/**
 * @file reticulum_call_overlay.h
 * @brief Global Reticulum call interruption page runtime.
 */

#pragma once

namespace ui::widgets::reticulum_call_overlay
{

void tick();
bool visible();
// UI-thread shutdown: dismiss the interruption and restore the previous app.
// The host separately hangs up and stops its protocol/audio owners.
void release();

} // namespace ui::widgets::reticulum_call_overlay
