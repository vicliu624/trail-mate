#pragma once

#include "lvgl.h"
#include "ui/components/two_pane_nav.h"

#include <cstddef>
#include <cstdint>

namespace ui::presentation::directory_browser_nav
{

enum class FocusRegion : std::uint8_t
{
    Selector = 0,
    Content = 1,
};

enum class BackPlacement : std::uint8_t
{
    None = 0,
    Leading,
    Trailing,
};

struct Adapter
{
    void* ctx = nullptr;
    bool (*is_alive)(void* ctx) = nullptr;
    lv_obj_t* (*get_key_target)(void* ctx) = nullptr;
    lv_obj_t* (*get_top_back_button)(void* ctx) = nullptr;

    size_t (*get_selector_count)(void* ctx) = nullptr;
    lv_obj_t* (*get_selector_button)(void* ctx, size_t index) = nullptr;
    int (*get_preferred_selector_index)(void* ctx) = nullptr;

    size_t (*get_content_count)(void* ctx) = nullptr;
    lv_obj_t* (*get_content_button)(void* ctx, size_t index) = nullptr;
    int (*get_preferred_content_index)(void* ctx) = nullptr;
    lv_obj_t* (*get_content_back_button)(void* ctx) = nullptr;

    bool (*handle_selector_activate)(void* ctx, lv_obj_t* selector_button) = nullptr;
    bool (*handle_content_activate)(void* ctx, lv_obj_t* content_button) = nullptr;
    bool (*handle_content_back_activate)(void* ctx, lv_obj_t* back_button) = nullptr;

    bool (*handle_content_enter)(void* ctx, lv_obj_t* focused) = nullptr;
    void (*on_focus_region_changed)(void* ctx, FocusRegion region) = nullptr;

    BackPlacement selector_top_back_placement = BackPlacement::Leading;
    BackPlacement content_top_back_placement = BackPlacement::None;
};

class Controller
{
  public:
    void init(const Adapter& adapter);
    void cleanup();
    void on_ui_refreshed();
    void focus_selector();
    void focus_content();
    lv_group_t* group() const;

  private:
    static Controller* self(void* ctx);
    static bool legacy_is_alive(void* ctx);
    static lv_obj_t* legacy_get_key_target(void* ctx);
    static lv_obj_t* legacy_get_top_back_button(void* ctx);
    static size_t legacy_get_selector_count(void* ctx);
    static lv_obj_t* legacy_get_selector_button(void* ctx, size_t index);
    static int legacy_get_preferred_selector_index(void* ctx);
    static size_t legacy_get_content_count(void* ctx);
    static lv_obj_t* legacy_get_content_button(void* ctx, size_t index);
    static int legacy_get_preferred_content_index(void* ctx);
    static lv_obj_t* legacy_get_content_back_button(void* ctx);
    static bool legacy_handle_selector_activate(void* ctx, lv_obj_t* selector_button);
    static bool legacy_handle_content_activate(void* ctx, lv_obj_t* content_button);
    static bool legacy_handle_content_back_activate(void* ctx, lv_obj_t* back_button);
    static bool legacy_handle_content_enter(void* ctx, lv_obj_t* focused);
    static void legacy_on_focus_region_changed(void* ctx,
                                               ::ui::components::two_pane_nav::FocusColumn column);

    Adapter adapter_{};
    ::ui::components::two_pane_nav::Adapter legacy_adapter_{};
    ::ui::components::two_pane_nav::Controller legacy_controller_{};
};

} // namespace ui::presentation::directory_browser_nav
