#pragma once

#include "lvgl.h"
#include <cstdint>

namespace ui
{

class BlePairingPopup
{
  public:
    static void update(uint32_t passkey, bool is_fixed_pin, const char* device_name);
    static void hide();
    static bool isVisible();

  private:
    static void ensureCreated();
    static void applyLayout();
    static void show(uint32_t passkey, bool is_fixed_pin, const char* device_name);

    static lv_obj_t* overlay_;
    static lv_obj_t* panel_;
    static lv_obj_t* title_label_;
    static lv_obj_t* mode_label_;
    static lv_obj_t* hint_label_;
    static lv_obj_t* pin_label_;
    static lv_obj_t* device_label_;
    static lv_obj_t* footer_label_;
    static bool visible_;
    static uint32_t shown_passkey_;
    static bool shown_fixed_pin_;
};

} // namespace ui
