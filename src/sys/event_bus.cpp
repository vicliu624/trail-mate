/**
 * @file event_bus.cpp
 * @brief Event bus implementation
 */

#include "event_bus.h"

namespace sys {

// Define static instance here (not in header to avoid multiple definition)
EventBus EventBus::instance_;

} // namespace sys
