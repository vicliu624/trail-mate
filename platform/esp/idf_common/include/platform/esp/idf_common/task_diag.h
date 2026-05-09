/**
 * @file task_diag.h
 * @brief Lightweight FreeRTOS task watermark + queue depth diagnostics.
 *
 * Intended for Tab5 / T-Display P4 to surface resource pressure during
 * development and stability testing.  Not a production monitoring solution;
 * call logTaskWatermarks() from the app loop or a slow timer for visibility.
 */

#pragma once

#include <cstdint>

namespace platform::esp::idf_common::task_diag
{

/// Log the high-water-mark (minimum free stack) of every FreeRTOS task
/// known to the system.  Uses uxTaskGetSystemState() internally.
/// Call at most once per second.
void logTaskWatermarks();

/// Log the number of messages waiting in a FreeRTOS queue (by handle).
void logQueueDepth(const char* label, void* queue_handle);

/// Simple macro to log a queue depth; pass a QueueHandle_t.
#define TASK_DIAG_LOG_QUEUE(label, handle) \
    ::platform::esp::idf_common::task_diag::logQueueDepth(label, handle)

} // namespace platform::esp::idf_common::task_diag
