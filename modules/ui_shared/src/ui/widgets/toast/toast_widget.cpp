#include "ui/widgets/toast/toast_widget.h"
#include "sys/clock.h"
#include "ui/ui_theme.h"

#include <algorithm>

namespace ui::widgets
{

struct Toast::Impl
{
    lv_obj_t* root = nullptr;
    lv_obj_t* label = nullptr;
    lv_timer_t* timer = nullptr;

    uint32_t start_ms = 0;
    Options opt{};
};

void* Toast::active_ = nullptr;

static void set_bg_opa(lv_obj_t* obj, int32_t v)
{
    if (!obj) return;
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)v, 0);
}

static int clamp_int(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi) ? hi
                                    : v;
}

void Toast::show(lv_obj_t* parent, const char* text, Type type)
{
    Options opt = defaults();
    show(parent, text, type, opt);
}

void Toast::show(lv_obj_t* parent, const char* text, Type type, const Options& opt)
{
    if (!parent || !text || text[0] == '\0') return;

    // Ensure we never stack multiple toasts: only one active at a time.
    hide();

    auto* impl = new Impl();
    impl->opt = opt;
    impl->start_ms = sys::millis_now();

    // Root container: size driven by content.
    impl->root = lv_obj_create(parent);
    lv_obj_remove_style_all(impl->root);
    lv_obj_clear_flag(impl->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(impl->root, LV_OBJ_FLAG_IGNORE_LAYOUT);

    // Style
    lv_obj_set_style_radius(impl->root, 8, 0);
    lv_obj_set_style_bg_color(impl->root, bgColor(type), 0);
    lv_obj_set_style_bg_opa(impl->root, LV_OPA_0, 0); // start fully transparent and fade in
    lv_obj_set_style_pad_hor(impl->root, impl->opt.pad_h, 0);
    lv_obj_set_style_pad_ver(impl->root, impl->opt.pad_v, 0);

    // Label: limit width in pixels (do not use lv_pct for this).
    impl->label = lv_label_create(impl->root);
    lv_label_set_text(impl->label, text);
    lv_label_set_long_mode(impl->label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(impl->label, ::ui::theme::text(), 0);

    // Compute max width based on screen width (more stable across layouts).
    // Use the active screen width as the device-screen reference.
    lv_obj_t* scr = lv_screen_active();
    int scr_w = scr ? (int)lv_obj_get_width(scr) : 240;
    int max_w = (scr_w * (int)impl->opt.max_width_pct) / 100;
    max_w = clamp_int(max_w, 60, scr_w - 10); // clamp to a reasonable min/max
    lv_obj_set_width(impl->label, max_w);

    // Let root size itself based on the label.
    lv_obj_set_size(impl->root, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    // Make LVGL compute size/layout once before aligning, otherwise width/height may be stale.
    lv_obj_update_layout(impl->root);

    // Position: bottom center + custom y_offset.
    lv_obj_align(impl->root, LV_ALIGN_BOTTOM_MID, 0, impl->opt.y_offset);

    // Fade‑in animation.
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, impl->root);
    lv_anim_set_values(&a, 0, LV_OPA_90);
    lv_anim_set_time(&a, impl->opt.fade_ms);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)set_bg_opa);
    lv_anim_start(&a);

    impl->timer = lv_timer_create(timer_cb, 50, impl);

    setActiveImpl(impl);
}

void Toast::hide()
{
    auto* impl = activeImpl();
    if (!impl) return;
    destroy(impl);
    setActiveImpl(nullptr);
}

void Toast::timer_cb(lv_timer_t* t)
{
    auto* impl = static_cast<Impl*>(lv_timer_get_user_data(t));
    if (!impl) return;

    uint32_t now = sys::millis_now();
    if (now - impl->start_ms < impl->opt.duration_ms) return;

    // Time reached: start fade‑out animation.
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, impl->root);
    lv_anim_set_values(&a, LV_OPA_90, LV_OPA_0);
    lv_anim_set_time(&a, impl->opt.fade_ms);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)set_bg_opa);

    // When the animation completes, hide() will destroy and clear the active toast.
    lv_anim_set_ready_cb(&a, [](lv_anim_t*)
                         { Toast::hide(); });
    lv_anim_start(&a);

    // The timer only needs to fire once to trigger fade‑out.
    lv_timer_del(t);
    impl->timer = nullptr;
}

void Toast::destroy(Impl* impl)
{
    if (!impl) return;

    if (impl->timer)
    {
        lv_timer_del(impl->timer);
        impl->timer = nullptr;
    }
    if (impl->root)
    {
        lv_obj_del(impl->root);
        impl->root = nullptr;
    }
    delete impl;
}

lv_color_t Toast::bgColor(Type type)
{
    switch (type)
    {
    case Type::Success:
        return lv_color_mix(::ui::theme::status_green(), ::ui::theme::surface_alt(), 64);
    case Type::Error:
        return lv_color_mix(::ui::theme::error(), ::ui::theme::surface_alt(), 64);
    case Type::Info:
    default:
        return lv_color_mix(::ui::theme::accent(), ::ui::theme::surface_alt(), 48);
    }
}

} // namespace ui::widgets
