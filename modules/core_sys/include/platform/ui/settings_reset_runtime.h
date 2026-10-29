/**
 * @file settings_reset_runtime.h
 * @brief Platform-owned preparation for a complete Settings factory reset.
 */

#pragma once

namespace platform::ui::settings_reset
{

// Starts removal of the platform's authoritative working configuration and
// suppresses configuration synchronization while legacy stores are cleared.
// Call finish() after those stores have been cleared, including after failure.
// A platform using orderly exec restart may keep a committed reset quiescent
// through teardown; the next process resumes normal persistence.
bool begin();
void finish();

} // namespace platform::ui::settings_reset
