#pragma once

#include "lvgl.h"

class AppScreen
{
  public:
    virtual ~AppScreen() = default;
    virtual const char* name() const = 0;
    virtual const lv_image_dsc_t* icon() const = 0;
    virtual void enter(lv_obj_t* parent) = 0;
    virtual void exit(lv_obj_t* parent) = 0;
};
