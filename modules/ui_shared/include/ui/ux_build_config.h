#pragma once

// A presentation choice, independent of the board and operating system.
// Existing ESP targets retain their UX selection; Linux compositions may select
// this pack without defining an ESP board identity.
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) && !defined(TRAIL_MATE_UI_P4_TOUCH)
#define TRAIL_MATE_UI_P4_TOUCH 1
#endif
