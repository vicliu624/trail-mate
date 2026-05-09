#include "ui/menu/menu_runtime.h"

namespace ui::menu_runtime
{
namespace
{

Scene s_scene = Scene::Menu;
bool s_menu_active = true;

} // namespace

void init(lv_obj_t* screen_root, lv_obj_t* main_screen, lv_obj_t* menu_panel, const Hooks& hooks)
{
    (void)screen_root;
    (void)main_screen;
    (void)menu_panel;
    (void)hooks;
    s_scene = Scene::Menu;
    s_menu_active = true;
}

void showWatchFace()
{
}

void onWakeFromSleep()
{
}

void setMenuActive(bool active)
{
    s_menu_active = active;
}

void setScene(Scene scene)
{
    s_scene = scene;
    s_menu_active = (scene == Scene::Menu);
}

Scene currentScene()
{
    return s_scene;
}

} // namespace ui::menu_runtime
