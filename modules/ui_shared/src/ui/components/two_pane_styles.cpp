#include "ui/components/two_pane_styles.h"
#include "ui/theme/theme_component_style.h"
#include "ui/ui_theme.h"

#if defined(ESP_PLATFORM)
#include "esp_log.h"
#endif

namespace ui::components::two_pane_styles
{
namespace
{

#if defined(ESP_PLATFORM)
constexpr const char* kLogTag = "two-pane-style";
#endif

bool s_inited = false;
uint32_t s_theme_revision = 0;

lv_style_t s_panel_side;
lv_style_t s_panel_main;
lv_style_t s_container_main;

lv_style_t s_btn_basic;
lv_style_t s_btn_filter_checked;
lv_style_t s_btn_filter_focused;

lv_style_t s_item_base;
lv_style_t s_item_focused;

lv_style_t s_label_primary;
lv_style_t s_label_muted;
lv_style_t s_label_accent;

} // namespace

void init_once()
{
    const uint32_t active_revision = ::ui::theme::active_theme_revision();
    if (s_inited && s_theme_revision == active_revision)
    {
        return;
    }

    if (s_inited)
    {
        lv_style_reset(&s_panel_side);
        lv_style_reset(&s_panel_main);
        lv_style_reset(&s_container_main);
        lv_style_reset(&s_btn_basic);
        lv_style_reset(&s_btn_filter_checked);
        lv_style_reset(&s_btn_filter_focused);
        lv_style_reset(&s_item_base);
        lv_style_reset(&s_item_focused);
        lv_style_reset(&s_label_primary);
        lv_style_reset(&s_label_muted);
        lv_style_reset(&s_label_accent);
    }
    else
    {
        s_inited = true;
    }
    s_theme_revision = active_revision;

#if defined(ESP_PLATFORM)
    ESP_LOGI(kLogTag, "init_once");
#endif

    lv_style_init(&s_panel_side);
    lv_style_set_bg_opa(&s_panel_side, LV_OPA_COVER);
    lv_style_set_bg_color(&s_panel_side, ::ui::theme::surface_alt());
    lv_style_set_border_width(&s_panel_side, 0);
    lv_style_set_pad_all(&s_panel_side, 3);
    lv_style_set_radius(&s_panel_side, 0);
    ::ui::theme::ComponentProfile profile{};
    if (::ui::theme::resolve_component_profile(::ui::theme::ComponentSlot::DirectoryBrowserSelectorPanel,
                                               profile))
    {
        ::ui::theme::apply_component_profile_to_style(&s_panel_side, profile);
    }

    lv_style_init(&s_panel_main);
    lv_style_set_bg_opa(&s_panel_main, LV_OPA_COVER);
    lv_style_set_bg_color(&s_panel_main, ::ui::theme::surface());
    lv_style_set_border_width(&s_panel_main, 0);
    lv_style_set_pad_all(&s_panel_main, 3);
    lv_style_set_radius(&s_panel_main, 0);
    if (::ui::theme::resolve_component_profile(::ui::theme::ComponentSlot::DirectoryBrowserContentPanel,
                                               profile))
    {
        ::ui::theme::apply_component_profile_to_style(&s_panel_main, profile);
    }

    lv_style_init(&s_container_main);
    lv_style_set_bg_opa(&s_container_main, LV_OPA_COVER);
    lv_style_set_bg_color(&s_container_main, ::ui::theme::surface());
    lv_style_set_border_width(&s_container_main, 0);
    lv_style_set_pad_all(&s_container_main, 3);
    lv_style_set_radius(&s_container_main, 0);
    if (::ui::theme::resolve_component_profile(::ui::theme::ComponentSlot::DirectoryBrowserContentPanel,
                                               profile))
    {
        ::ui::theme::apply_component_profile_to_style(&s_container_main, profile);
    }

    lv_style_init(&s_btn_basic);
    lv_style_set_bg_opa(&s_btn_basic, LV_OPA_COVER);
    lv_style_set_bg_color(&s_btn_basic, ::ui::theme::surface());
    lv_style_set_border_width(&s_btn_basic, 1);
    lv_style_set_border_color(&s_btn_basic, ::ui::theme::border());
    lv_style_set_radius(&s_btn_basic, 12);
    lv_style_set_text_color(&s_btn_basic, ::ui::theme::text());
    if (::ui::theme::resolve_component_profile(::ui::theme::ComponentSlot::DirectoryBrowserSelectorButton,
                                               profile))
    {
        ::ui::theme::apply_component_profile_to_style(&s_btn_basic, profile);
    }

    lv_style_init(&s_btn_filter_checked);
    lv_style_set_bg_opa(&s_btn_filter_checked, LV_OPA_COVER);
    lv_style_set_bg_color(&s_btn_filter_checked,
                          profile.accent_color.present ? profile.accent_color.value
                                                       : ::ui::theme::accent());

    lv_style_init(&s_btn_filter_focused);
    lv_style_set_bg_opa(&s_btn_filter_focused, LV_OPA_COVER);
    lv_style_set_bg_color(&s_btn_filter_focused,
                          profile.accent_color.present ? profile.accent_color.value
                                                       : ::ui::theme::accent());

    lv_style_init(&s_item_base);
    lv_style_set_bg_opa(&s_item_base, LV_OPA_COVER);
    lv_style_set_bg_color(&s_item_base, ::ui::theme::surface());
    lv_style_set_border_width(&s_item_base, 1);
    lv_style_set_border_color(&s_item_base, ::ui::theme::border());
    lv_style_set_radius(&s_item_base, 6);
    if (::ui::theme::resolve_component_profile(::ui::theme::ComponentSlot::DirectoryBrowserListItem,
                                               profile))
    {
        ::ui::theme::apply_component_profile_to_style(&s_item_base, profile);
    }

    lv_style_init(&s_item_focused);
    lv_style_set_bg_opa(&s_item_focused, LV_OPA_COVER);
    lv_style_set_bg_color(&s_item_focused,
                          profile.accent_color.present ? profile.accent_color.value
                                                       : ::ui::theme::accent());
    lv_style_set_outline_width(&s_item_focused, 0);

    lv_style_init(&s_label_primary);
    lv_style_set_text_color(&s_label_primary, ::ui::theme::text());

    lv_style_init(&s_label_muted);
    lv_style_set_text_color(&s_label_muted, ::ui::theme::text_muted());

    lv_style_init(&s_label_accent);
    lv_style_set_text_color(&s_label_accent, ::ui::theme::accent());
}

void apply_panel_side(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_panel_side, 0);
}

void apply_panel_main(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_panel_main, 0);
}

void apply_container_main(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_container_main, 0);
}

void apply_btn_basic(lv_obj_t* btn)
{
    init_once();
    lv_obj_add_style(btn, &s_btn_basic, LV_PART_MAIN);
}

void apply_btn_filter(lv_obj_t* btn)
{
    init_once();
    lv_obj_add_style(btn, &s_btn_basic, LV_PART_MAIN);
    lv_obj_add_style(btn, &s_btn_filter_checked, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_add_style(btn, &s_btn_filter_focused, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_add_style(btn, &s_btn_filter_focused, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
}

void apply_list_item(lv_obj_t* item)
{
    init_once();
    lv_obj_add_style(item, &s_item_base, LV_PART_MAIN);
    lv_obj_add_style(item, &s_item_focused, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_add_style(item, &s_item_focused, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_add_style(item, &s_item_focused, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
}

void apply_label_primary(lv_obj_t* label)
{
    init_once();
    lv_obj_add_style(label, &s_label_primary, 0);
}

void apply_label_muted(lv_obj_t* label)
{
    init_once();
    lv_obj_add_style(label, &s_label_muted, 0);
}

void apply_label_accent(lv_obj_t* label)
{
    init_once();
    lv_obj_add_style(label, &s_label_accent, 0);
}

} // namespace ui::components::two_pane_styles
