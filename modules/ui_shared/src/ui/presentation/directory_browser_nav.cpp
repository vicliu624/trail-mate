#include "ui/presentation/directory_browser_nav.h"

namespace ui::presentation::directory_browser_nav
{
namespace
{

using LegacyBackPlacement = ::ui::components::two_pane_nav::BackPlacement;
using LegacyFocusColumn = ::ui::components::two_pane_nav::FocusColumn;

static LegacyBackPlacement to_legacy_back_placement(BackPlacement placement)
{
    switch (placement)
    {
    case BackPlacement::None:
        return LegacyBackPlacement::None;
    case BackPlacement::Leading:
        return LegacyBackPlacement::Leading;
    case BackPlacement::Trailing:
        return LegacyBackPlacement::Trailing;
    }
    return LegacyBackPlacement::None;
}

static FocusRegion from_legacy_focus_column(LegacyFocusColumn column)
{
    return column == LegacyFocusColumn::Filter ? FocusRegion::Selector : FocusRegion::Content;
}

} // namespace

Controller* Controller::self(void* ctx)
{
    return static_cast<Controller*>(ctx);
}

bool Controller::legacy_is_alive(void* ctx)
{
    auto* controller = self(ctx);
    return controller && (!controller->adapter_.is_alive ||
                          controller->adapter_.is_alive(controller->adapter_.ctx));
}

lv_obj_t* Controller::legacy_get_key_target(void* ctx)
{
    auto* controller = self(ctx);
    return (controller && controller->adapter_.get_key_target)
               ? controller->adapter_.get_key_target(controller->adapter_.ctx)
               : nullptr;
}

lv_obj_t* Controller::legacy_get_top_back_button(void* ctx)
{
    auto* controller = self(ctx);
    return (controller && controller->adapter_.get_top_back_button)
               ? controller->adapter_.get_top_back_button(controller->adapter_.ctx)
               : nullptr;
}

size_t Controller::legacy_get_selector_count(void* ctx)
{
    auto* controller = self(ctx);
    return (controller && controller->adapter_.get_selector_count)
               ? controller->adapter_.get_selector_count(controller->adapter_.ctx)
               : 0;
}

lv_obj_t* Controller::legacy_get_selector_button(void* ctx, size_t index)
{
    auto* controller = self(ctx);
    return (controller && controller->adapter_.get_selector_button)
               ? controller->adapter_.get_selector_button(controller->adapter_.ctx, index)
               : nullptr;
}

int Controller::legacy_get_preferred_selector_index(void* ctx)
{
    auto* controller = self(ctx);
    return (controller && controller->adapter_.get_preferred_selector_index)
               ? controller->adapter_.get_preferred_selector_index(controller->adapter_.ctx)
               : -1;
}

size_t Controller::legacy_get_content_count(void* ctx)
{
    auto* controller = self(ctx);
    return (controller && controller->adapter_.get_content_count)
               ? controller->adapter_.get_content_count(controller->adapter_.ctx)
               : 0;
}

lv_obj_t* Controller::legacy_get_content_button(void* ctx, size_t index)
{
    auto* controller = self(ctx);
    return (controller && controller->adapter_.get_content_button)
               ? controller->adapter_.get_content_button(controller->adapter_.ctx, index)
               : nullptr;
}

int Controller::legacy_get_preferred_content_index(void* ctx)
{
    auto* controller = self(ctx);
    return (controller && controller->adapter_.get_preferred_content_index)
               ? controller->adapter_.get_preferred_content_index(controller->adapter_.ctx)
               : -1;
}

lv_obj_t* Controller::legacy_get_content_back_button(void* ctx)
{
    auto* controller = self(ctx);
    return (controller && controller->adapter_.get_content_back_button)
               ? controller->adapter_.get_content_back_button(controller->adapter_.ctx)
               : nullptr;
}

bool Controller::legacy_handle_selector_activate(void* ctx, lv_obj_t* selector_button)
{
    auto* controller = self(ctx);
    return controller && controller->adapter_.handle_selector_activate &&
           controller->adapter_.handle_selector_activate(controller->adapter_.ctx, selector_button);
}

bool Controller::legacy_handle_content_activate(void* ctx, lv_obj_t* content_button)
{
    auto* controller = self(ctx);
    return controller && controller->adapter_.handle_content_activate &&
           controller->adapter_.handle_content_activate(controller->adapter_.ctx, content_button);
}

bool Controller::legacy_handle_content_back_activate(void* ctx, lv_obj_t* back_button)
{
    auto* controller = self(ctx);
    return controller && controller->adapter_.handle_content_back_activate &&
           controller->adapter_.handle_content_back_activate(controller->adapter_.ctx, back_button);
}

bool Controller::legacy_handle_content_enter(void* ctx, lv_obj_t* focused)
{
    auto* controller = self(ctx);
    return controller && controller->adapter_.handle_content_enter &&
           controller->adapter_.handle_content_enter(controller->adapter_.ctx, focused);
}

void Controller::legacy_on_focus_region_changed(void* ctx, LegacyFocusColumn column)
{
    auto* controller = self(ctx);
    if (!controller || !controller->adapter_.on_focus_region_changed)
    {
        return;
    }
    controller->adapter_.on_focus_region_changed(controller->adapter_.ctx,
                                                 from_legacy_focus_column(column));
}

void Controller::init(const Adapter& adapter)
{
    adapter_ = adapter;

    legacy_adapter_ = {};
    legacy_adapter_.ctx = this;
    legacy_adapter_.is_alive = legacy_is_alive;
    legacy_adapter_.get_key_target = legacy_get_key_target;
    legacy_adapter_.get_top_back_button = legacy_get_top_back_button;
    legacy_adapter_.get_filter_count = legacy_get_selector_count;
    legacy_adapter_.get_filter_button = legacy_get_selector_button;
    legacy_adapter_.get_preferred_filter_index = legacy_get_preferred_selector_index;
    legacy_adapter_.get_list_count = legacy_get_content_count;
    legacy_adapter_.get_list_button = legacy_get_content_button;
    legacy_adapter_.get_preferred_list_index = legacy_get_preferred_content_index;
    legacy_adapter_.get_list_back_button = legacy_get_content_back_button;
    legacy_adapter_.handle_filter_activate = legacy_handle_selector_activate;
    legacy_adapter_.handle_list_activate = legacy_handle_content_activate;
    legacy_adapter_.handle_list_back_activate = legacy_handle_content_back_activate;
    legacy_adapter_.handle_list_enter = legacy_handle_content_enter;
    legacy_adapter_.on_column_changed = legacy_on_focus_region_changed;
    legacy_adapter_.filter_top_back_placement =
        to_legacy_back_placement(adapter.selector_top_back_placement);
    legacy_adapter_.list_top_back_placement =
        to_legacy_back_placement(adapter.content_top_back_placement);

    legacy_controller_.init(legacy_adapter_);
}

void Controller::cleanup()
{
    legacy_controller_.cleanup();
    legacy_adapter_ = {};
    adapter_ = {};
}

void Controller::on_ui_refreshed()
{
    legacy_controller_.on_ui_refreshed();
}

void Controller::focus_selector()
{
    legacy_controller_.focus_filter();
}

void Controller::focus_content()
{
    legacy_controller_.focus_list();
}

lv_group_t* Controller::group() const
{
    return legacy_controller_.group();
}

} // namespace ui::presentation::directory_browser_nav
