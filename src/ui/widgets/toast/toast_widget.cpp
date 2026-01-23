#include "toast_widget.h"
#include <Arduino.h>
#include <algorithm>

namespace ui::widgets {

struct Toast::Impl {
    lv_obj_t* root  = nullptr;
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
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

void Toast::show(lv_obj_t* parent, const char* text, Type type)
{
    Options opt = defaults();
    show(parent, text, type, opt);
}

void Toast::show(lv_obj_t* parent, const char* text, Type type, const Options& opt)
{
    if (!parent || !text || text[0] == '\0') return;

    // 保证不叠加：同一时刻只显示一个 toast
    hide();

    auto* impl = new Impl();
    impl->opt = opt;
    impl->start_ms = millis();

    // root：尺寸由内容决定
    impl->root = lv_obj_create(parent);
    lv_obj_remove_style_all(impl->root);
    lv_obj_clear_flag(impl->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(impl->root, LV_OBJ_FLAG_IGNORE_LAYOUT);

    // 样式
    lv_obj_set_style_radius(impl->root, 8, 0);
    lv_obj_set_style_bg_color(impl->root, bgColor(type), 0);
    lv_obj_set_style_bg_opa(impl->root, LV_OPA_0, 0); // 从 0 淡入
    lv_obj_set_style_pad_hor(impl->root, impl->opt.pad_h, 0);
    lv_obj_set_style_pad_ver(impl->root, impl->opt.pad_v, 0);

    // label：最大宽度用“像素”限制（不要用 lv_pct）
    impl->label = lv_label_create(impl->root);
    lv_label_set_text(impl->label, text);
    lv_label_set_long_mode(impl->label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(impl->label, lv_color_white(), 0);

    // 计算最大宽度：基于屏幕宽度（更稳定）
    // 注意：这里用 screen_active 的宽度更符合“设备屏幕”的语义
    lv_obj_t* scr = lv_screen_active();
    int scr_w = scr ? (int)lv_obj_get_width(scr) : 240;
    int max_w = (scr_w * (int)impl->opt.max_width_pct) / 100;
    max_w = clamp_int(max_w, 60, scr_w - 10); // 给一个合理下限/上限
    lv_obj_set_width(impl->label, max_w);

    // 让 root 根据 label 尺寸自动收缩
    lv_obj_set_size(impl->root, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    // ✅ 先让 LVGL 计算一次尺寸（很关键：否则 align 用的宽高可能是旧值）
    lv_obj_update_layout(impl->root);

    // ✅ 定位：底部居中 + y_offset
    lv_obj_align(impl->root, LV_ALIGN_BOTTOM_MID, 0, impl->opt.y_offset);

    // 淡入动画
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
    auto* impl = static_cast<Impl*>(t->user_data);
    if (!impl) return;

    uint32_t now = millis();
    if (now - impl->start_ms < impl->opt.duration_ms) return;

    // 到时间：淡出
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, impl->root);
    lv_anim_set_values(&a, LV_OPA_90, LV_OPA_0);
    lv_anim_set_time(&a, impl->opt.fade_ms);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)set_bg_opa);

    // 动画结束后 hide（注意：hide 会 destroy 并清 active）
    lv_anim_set_ready_cb(&a, [](lv_anim_t*) { Toast::hide(); });
    lv_anim_start(&a);

    // timer 只负责触发一次淡出
    lv_timer_del(t);
    impl->timer = nullptr;
}

void Toast::destroy(Impl* impl)
{
    if (!impl) return;

    if (impl->timer) {
        lv_timer_del(impl->timer);
        impl->timer = nullptr;
    }
    if (impl->root) {
        lv_obj_del(impl->root);
        impl->root = nullptr;
    }
    delete impl;
}

lv_color_t Toast::bgColor(Type type)
{
    switch (type)
    {
    case Type::Success: return lv_palette_main(LV_PALETTE_GREEN);
    case Type::Error:   return lv_palette_main(LV_PALETTE_RED);
    case Type::Info:
    default:            return lv_palette_main(LV_PALETTE_GREY);
    }
}

} // namespace ui::widgets
