#ifndef GPS_PAGE_PRESENTER_H
#define GPS_PAGE_PRESENTER_H

// Presenter functions for updating UI based on state changes
void update_title_and_status();
void update_resolution_display();
void update_zoom_btn();
void reset_title_status_cache();  // Reset cached state to force next update

#endif // GPS_PAGE_PRESENTER_H
