/**
 * @file ui_gps.cpp
 * @brief GPS interface implementation
 * 
 * This file has been refactored into multiple modules:
 * - gps_state.h: State structure
 * - gps_modal.h/cpp: Modal framework
 * - map_tiles.h/cpp: Tile algorithm
 * - gps_page.h/cpp: Page-level scheduling
 * 
 * This file now only provides the public interface (ui_gps_enter/exit)
 * and forwards to gps_page.cpp implementation.
 */

#include "gps_page.h"

// All implementation is now in gps_page.cpp
// This file is kept for backward compatibility with existing includes
