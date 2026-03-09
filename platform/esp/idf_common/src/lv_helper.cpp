#include "ui/LV_Helper.h"

namespace
{

lv_indev_t* find_indev(lv_indev_type_t type)
{
    for (lv_indev_t* indev = lv_indev_get_next(nullptr); indev != nullptr; indev = lv_indev_get_next(indev))
    {
        if (lv_indev_get_type(indev) == type)
        {
            return indev;
        }
    }
    return nullptr;
}

} // namespace

void beginLvglHelper(LilyGo_Display& display, bool debug)
{
    (void)display;
    (void)debug;
}

void lv_set_default_group(lv_group_t* group)
{
    lv_group_set_default(group);
}

lv_indev_t* lv_get_touch_indev()
{
    return find_indev(LV_INDEV_TYPE_POINTER);
}

lv_indev_t* lv_get_keyboard_indev()
{
    return find_indev(LV_INDEV_TYPE_KEYPAD);
}

lv_indev_t* lv_get_encoder_indev()
{
    return find_indev(LV_INDEV_TYPE_ENCODER);
}