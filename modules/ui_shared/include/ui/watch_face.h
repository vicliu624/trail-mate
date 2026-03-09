#pragma once

#include "lvgl.h"
#include <cstdint>

#if defined(ARDUINO_T_WATCH_S3)
void watch_face_create(lv_obj_t* parent);
void watch_face_set_time(int hour, int minute, int month, int day, const char* weekday, int battery_percent);
void watch_face_set_node_id(uint32_t node_id);
void watch_face_set_placeholder();
void watch_face_show(bool show);
bool watch_face_is_ready();
bool watch_face_is_visible();
void watch_face_set_unlock_cb(void (*cb)(void));
#else
inline void watch_face_create(lv_obj_t*) {}
inline void watch_face_set_time(int, int, int, int, const char*, int) {}
inline void watch_face_set_node_id(uint32_t) {}
inline void watch_face_set_placeholder() {}
inline void watch_face_show(bool) {}
inline bool watch_face_is_ready() { return false; }
inline bool watch_face_is_visible() { return false; }
inline void watch_face_set_unlock_cb(void (*)(void)) {}
#endif
