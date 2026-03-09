#include "ui/components/two_pane_nav.h"

#include "ui/app_runtime.h"

#include <cstdio>

namespace ui
{
namespace components
{
namespace two_pane_nav
{
namespace
{

#define TWO_PANE_NAV_LOG(...) std::printf(__VA_ARGS__)

static const char* column_name(FocusColumn column)
{
    return column == FocusColumn::Filter ? "filter" : "list";
}

static bool is_valid(lv_obj_t* obj)
{
    return obj && lv_obj_is_valid(obj);
}

static bool is_visible(lv_obj_t* obj)
{
    if (!is_valid(obj)) return false;
    for (lv_obj_t* cur = obj; cur != nullptr; cur = lv_obj_get_parent(cur))
    {
        if (lv_obj_has_flag(cur, LV_OBJ_FLAG_HIDDEN))
        {
            return false;
        }
    }
    return true;
}

static bool binding_alive(const Binding* binding)
{
    if (!binding || !binding->bound) return false;
    if (!binding->adapter.is_alive) return true;
    return binding->adapter.is_alive(binding->adapter.ctx);
}

static lv_indev_t* find_encoder_indev()
{
    lv_indev_t* indev = nullptr;
    while ((indev = lv_indev_get_next(indev)) != nullptr)
    {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER)
        {
            return indev;
        }
    }
    return nullptr;
}

static bool is_encoder_active()
{
    lv_indev_t* indev = lv_indev_get_act();
    return indev && lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER;
}

static void group_clear_all(lv_group_t* group)
{
    if (!group) return;
    lv_group_remove_all_objs(group);
}

static lv_obj_t* get_key_target(Binding* binding);
static lv_obj_t* get_top_back_button(Binding* binding);
static lv_obj_t* get_filter_button(Binding* binding, size_t index);
static lv_obj_t* get_list_button(Binding* binding, size_t index);
static lv_obj_t* get_list_back_button(Binding* binding);
static size_t get_filter_count(Binding* binding);
static size_t get_list_count(Binding* binding);
static bool has_activation_handlers(Binding* binding);
static void prepare_touch_routing(Binding* binding);
static void rebind_by_column(Binding* binding);

static void clear_focus_state(lv_obj_t* obj)
{
    if (!is_valid(obj))
    {
        return;
    }
    lv_obj_clear_state(obj, static_cast<lv_state_t>(LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY));
}

static void clear_managed_focus_states(Binding* binding)
{
    if (!binding)
    {
        return;
    }

    clear_focus_state(get_top_back_button(binding));
    clear_focus_state(get_list_back_button(binding));

    const size_t filter_count = get_filter_count(binding);
    for (size_t index = 0; index < filter_count; ++index)
    {
        clear_focus_state(get_filter_button(binding, index));
    }

    const size_t list_count = get_list_count(binding);
    for (size_t index = 0; index < list_count; ++index)
    {
        clear_focus_state(get_list_button(binding, index));
    }
}

static void root_key_event_cb(lv_event_t* e);
static void root_click_event_cb(lv_event_t* e);
static void managed_click_event_cb(lv_event_t* e);

static void attach_key_handler(Binding* binding, lv_obj_t* obj)
{
    if (!binding || !binding->group || !is_valid(obj)) return;
    lv_obj_remove_event_cb(obj, root_key_event_cb);
    lv_obj_add_event_cb(obj, root_key_event_cb, LV_EVENT_KEY, binding);
}

static void attach_click_handler(Binding* binding, lv_obj_t* obj)
{
    if (!binding || !has_activation_handlers(binding) || !is_valid(obj)) return;
    lv_obj_remove_event_cb(obj, managed_click_event_cb);
    lv_obj_add_event_cb(obj, managed_click_event_cb, LV_EVENT_CLICKED, binding);
    TWO_PANE_NAV_LOG("[TwoPaneNav] attach_click obj=%p column=%s\n",
                     obj,
                     column_name(binding->column));
}

static void group_add_if_visible(Binding* binding, lv_obj_t* obj)
{
    if (!binding || !binding->group || !is_visible(obj)) return;
    lv_group_add_obj(binding->group, obj);
    attach_key_handler(binding, obj);
}

static void focus_first_valid(Binding* binding, lv_obj_t* obj)
{
    if (!binding || !binding->group || !is_valid(obj)) return;
    lv_group_focus_obj(obj);
    if (is_valid(obj))
    {
        lv_obj_scroll_to_view(obj, LV_ANIM_OFF);
    }
}

static lv_obj_t* get_key_target(Binding* binding)
{
    return (binding && binding->adapter.get_key_target)
               ? binding->adapter.get_key_target(binding->adapter.ctx)
               : nullptr;
}

static lv_obj_t* get_top_back_button(Binding* binding)
{
    return (binding && binding->adapter.get_top_back_button)
               ? binding->adapter.get_top_back_button(binding->adapter.ctx)
               : nullptr;
}

static lv_obj_t* get_filter_button(Binding* binding, size_t index)
{
    return (binding && binding->adapter.get_filter_button)
               ? binding->adapter.get_filter_button(binding->adapter.ctx, index)
               : nullptr;
}

static lv_obj_t* get_list_button(Binding* binding, size_t index)
{
    return (binding && binding->adapter.get_list_button)
               ? binding->adapter.get_list_button(binding->adapter.ctx, index)
               : nullptr;
}

static lv_obj_t* get_list_back_button(Binding* binding)
{
    return (binding && binding->adapter.get_list_back_button)
               ? binding->adapter.get_list_back_button(binding->adapter.ctx)
               : nullptr;
}

static void notify_column_changed(Binding* binding)
{
    if (!binding || !binding->adapter.on_column_changed)
    {
        return;
    }
    binding->adapter.on_column_changed(binding->adapter.ctx, binding->column);
}

static void set_column(Binding* binding, FocusColumn column)
{
    if (!binding)
    {
        return;
    }
    if (binding->column == column)
    {
        return;
    }
    binding->column = column;
    notify_column_changed(binding);
}

static int get_preferred_filter_index(Binding* binding)
{
    return (binding && binding->adapter.get_preferred_filter_index)
               ? binding->adapter.get_preferred_filter_index(binding->adapter.ctx)
               : -1;
}

static int get_preferred_list_index(Binding* binding)
{
    return (binding && binding->adapter.get_preferred_list_index)
               ? binding->adapter.get_preferred_list_index(binding->adapter.ctx)
               : -1;
}

static size_t get_filter_count(Binding* binding)
{
    return (binding && binding->adapter.get_filter_count)
               ? binding->adapter.get_filter_count(binding->adapter.ctx)
               : 0;
}

static size_t get_list_count(Binding* binding)
{
    return (binding && binding->adapter.get_list_count)
               ? binding->adapter.get_list_count(binding->adapter.ctx)
               : 0;
}

static bool has_activation_handlers(Binding* binding)
{
    return binding && (binding->adapter.handle_filter_activate ||
                       binding->adapter.handle_list_activate ||
                       binding->adapter.handle_list_back_activate);
}

static void enable_event_bubble_subtree(lv_obj_t* obj)
{
    if (!is_valid(obj))
    {
        return;
    }
    lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    const uint32_t child_count = lv_obj_get_child_count(obj);
    for (uint32_t index = 0; index < child_count; ++index)
    {
        enable_event_bubble_subtree(lv_obj_get_child(obj, index));
    }
}

static void enable_event_bubble_route(lv_obj_t* obj, lv_obj_t* stop)
{
    if (!is_valid(obj))
    {
        return;
    }
    enable_event_bubble_subtree(obj);
    for (lv_obj_t* cur = lv_obj_get_parent(obj); cur != nullptr; cur = lv_obj_get_parent(cur))
    {
        lv_obj_add_flag(cur, LV_OBJ_FLAG_EVENT_BUBBLE);
        if (cur == stop)
        {
            break;
        }
    }
}

static void prepare_touch_routing(Binding* binding)
{
    if (!has_activation_handlers(binding))
    {
        return;
    }

    lv_obj_t* key_target = get_key_target(binding);
    if (!is_valid(key_target))
    {
        return;
    }

    TWO_PANE_NAV_LOG("[TwoPaneNav] prepare_touch_routing key_target=%p column=%s filter_count=%u list_count=%u\n",
                     key_target,
                     column_name(binding->column),
                     (unsigned)get_filter_count(binding),
                     (unsigned)get_list_count(binding));

    const size_t filter_count = get_filter_count(binding);
    for (size_t index = 0; index < filter_count; ++index)
    {
        lv_obj_t* button = get_filter_button(binding, index);
        attach_click_handler(binding, button);
        enable_event_bubble_route(button, key_target);
    }

    const size_t list_count = get_list_count(binding);
    for (size_t index = 0; index < list_count; ++index)
    {
        lv_obj_t* button = get_list_button(binding, index);
        attach_click_handler(binding, button);
        enable_event_bubble_route(button, key_target);
    }

    lv_obj_t* back_button = get_list_back_button(binding);
    attach_click_handler(binding, back_button);
    enable_event_bubble_route(back_button, key_target);
}

static void add_top_back(Binding* binding, BackPlacement placement, BackPlacement desired)
{
    if (placement != desired) return;
    group_add_if_visible(binding, get_top_back_button(binding));
}

static lv_obj_t* first_visible_filter_button(Binding* binding)
{
    const size_t count = get_filter_count(binding);
    for (size_t index = 0; index < count; ++index)
    {
        if (lv_obj_t* btn = get_filter_button(binding, index))
        {
            if (is_visible(btn))
            {
                return btn;
            }
        }
    }
    return nullptr;
}

static lv_obj_t* first_visible_list_button(Binding* binding)
{
    const size_t count = get_list_count(binding);
    for (size_t index = 0; index < count; ++index)
    {
        if (lv_obj_t* btn = get_list_button(binding, index))
        {
            if (is_visible(btn))
            {
                return btn;
            }
        }
    }
    return nullptr;
}

static bool is_filter_button(Binding* binding, lv_obj_t* obj)
{
    if (!is_visible(obj))
    {
        return false;
    }
    const size_t count = get_filter_count(binding);
    for (size_t index = 0; index < count; ++index)
    {
        if (obj == get_filter_button(binding, index))
        {
            return true;
        }
    }
    return false;
}

static bool is_list_button(Binding* binding, lv_obj_t* obj)
{
    if (!is_visible(obj))
    {
        return false;
    }
    const size_t count = get_list_count(binding);
    for (size_t index = 0; index < count; ++index)
    {
        if (obj == get_list_button(binding, index))
        {
            return true;
        }
    }
    return false;
}

static bool activate_filter_button(Binding* binding, lv_obj_t* obj)
{
    if (!binding || !is_filter_button(binding, obj) ||
        !binding->adapter.handle_filter_activate)
    {
        return false;
    }
    if (!binding->adapter.handle_filter_activate(binding->adapter.ctx, obj))
    {
        TWO_PANE_NAV_LOG("[TwoPaneNav] activate_filter rejected obj=%p\n", obj);
        return false;
    }
    TWO_PANE_NAV_LOG("[TwoPaneNav] activate_filter obj=%p -> switch_to=list\n", obj);
    if (!binding_alive(binding) || !binding->group)
    {
        return true;
    }
    set_column(binding, FocusColumn::List);
    rebind_by_column(binding);
    return true;
}

static bool activate_list_back(Binding* binding, lv_obj_t* obj)
{
    lv_obj_t* back = get_list_back_button(binding);
    if (!binding || obj != back || !is_visible(back))
    {
        return false;
    }
    if (binding->adapter.handle_list_back_activate)
    {
        TWO_PANE_NAV_LOG("[TwoPaneNav] activate_list_back custom obj=%p\n", obj);
        return binding->adapter.handle_list_back_activate(binding->adapter.ctx, obj);
    }
    if (!binding->group)
    {
        return true;
    }
    TWO_PANE_NAV_LOG("[TwoPaneNav] activate_list_back obj=%p -> switch_to=filter\n", obj);
    set_column(binding, FocusColumn::Filter);
    rebind_by_column(binding);
    return true;
}

static bool activate_list_button(Binding* binding, lv_obj_t* obj)
{
    if (!binding || !is_list_button(binding, obj) ||
        !binding->adapter.handle_list_activate)
    {
        return false;
    }
    const bool handled = binding->adapter.handle_list_activate(binding->adapter.ctx, obj);
    TWO_PANE_NAV_LOG("[TwoPaneNav] activate_list obj=%p handled=%d\n", obj, handled ? 1 : 0);
    return handled;
}

static void bind_filter_column(Binding* binding)
{
    if (!binding || !binding->group) return;

    lv_group_focus_freeze(binding->group, true);
    clear_managed_focus_states(binding);
    group_clear_all(binding->group);
    lv_group_set_editing(binding->group, false);

    add_top_back(binding, binding->adapter.filter_top_back_placement, BackPlacement::Leading);

    const size_t count = get_filter_count(binding);
    for (size_t index = 0; index < count; ++index)
    {
        group_add_if_visible(binding, get_filter_button(binding, index));
    }

    add_top_back(binding, binding->adapter.filter_top_back_placement, BackPlacement::Trailing);
    lv_group_focus_freeze(binding->group, false);

    lv_obj_t* target = nullptr;
    const int preferred = get_preferred_filter_index(binding);
    if (preferred >= 0 && static_cast<size_t>(preferred) < count)
    {
        target = get_filter_button(binding, static_cast<size_t>(preferred));
        if (!is_visible(target))
        {
            target = nullptr;
        }
    }
    if (!target)
    {
        target = first_visible_filter_button(binding);
    }
    if (!target)
    {
        target = get_top_back_button(binding);
        if (!is_visible(target))
        {
            target = nullptr;
        }
    }
    prepare_touch_routing(binding);
    if (target)
    {
        focus_first_valid(binding, target);
    }
}

static void bind_list_column(Binding* binding)
{
    if (!binding || !binding->group) return;

    lv_group_focus_freeze(binding->group, true);
    clear_managed_focus_states(binding);
    group_clear_all(binding->group);
    lv_group_set_editing(binding->group, false);

    add_top_back(binding, binding->adapter.list_top_back_placement, BackPlacement::Leading);

    const size_t count = get_list_count(binding);
    for (size_t index = 0; index < count; ++index)
    {
        group_add_if_visible(binding, get_list_button(binding, index));
    }

    if (lv_obj_t* back = get_list_back_button(binding))
    {
        group_add_if_visible(binding, back);
    }

    add_top_back(binding, binding->adapter.list_top_back_placement, BackPlacement::Trailing);
    lv_group_focus_freeze(binding->group, false);

    lv_obj_t* target = nullptr;
    const int preferred = get_preferred_list_index(binding);
    if (preferred >= 0 && static_cast<size_t>(preferred) < count)
    {
        target = get_list_button(binding, static_cast<size_t>(preferred));
        if (!is_visible(target))
        {
            target = nullptr;
        }
    }
    if (!target)
    {
        target = first_visible_list_button(binding);
    }
    if (!target)
    {
        target = get_list_back_button(binding);
        if (!is_visible(target))
        {
            target = nullptr;
        }
    }
    if (!target)
    {
        target = get_top_back_button(binding);
        if (!is_visible(target))
        {
            target = nullptr;
        }
    }
    prepare_touch_routing(binding);
    if (target)
    {
        focus_first_valid(binding, target);
    }
}

static void rebind_by_column(Binding* binding)
{
    if (!binding) return;
    if (binding->column == FocusColumn::Filter)
    {
        bind_filter_column(binding);
    }
    else
    {
        bind_list_column(binding);
    }
}

static void root_key_event_cb(lv_event_t* e)
{
    auto* binding = static_cast<Binding*>(lv_event_get_user_data(e));
    if (!binding_alive(binding)) return;

    const uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_BACKSPACE)
    {
        if (lv_obj_t* back = get_top_back_button(binding))
        {
            lv_obj_send_event(back, LV_EVENT_CLICKED, nullptr);
        }
        return;
    }

    if (!is_encoder_active()) return;

    if (key == LV_KEY_ESC)
    {
        set_column(binding, FocusColumn::Filter);
        rebind_by_column(binding);
        return;
    }

    if (key != LV_KEY_ENTER) return;

    lv_obj_t* focused = binding->group ? lv_group_get_focused(binding->group) : nullptr;
    if (!focused) return;

    if (binding->column == FocusColumn::Filter)
    {
        if (focused == get_top_back_button(binding) && is_visible(focused))
        {
            lv_obj_send_event(focused, LV_EVENT_CLICKED, nullptr);
            return;
        }

        set_column(binding, FocusColumn::List);
        rebind_by_column(binding);
        return;
    }

    if (binding->column == FocusColumn::List)
    {
        if (activate_list_back(binding, focused))
        {
            return;
        }

        if (focused == get_top_back_button(binding) && is_visible(focused))
        {
            lv_obj_send_event(focused, LV_EVENT_CLICKED, nullptr);
            return;
        }

        if (binding->adapter.handle_list_enter &&
            binding->adapter.handle_list_enter(binding->adapter.ctx, focused))
        {
            return;
        }

        if (activate_list_button(binding, focused))
        {
            return;
        }

        if (is_list_button(binding, focused))
        {
            lv_obj_send_event(focused, LV_EVENT_CLICKED, nullptr);
            return;
        }
    }
}

static void root_click_event_cb(lv_event_t* e)
{
    auto* binding = static_cast<Binding*>(lv_event_get_user_data(e));
    if (!binding_alive(binding) || !has_activation_handlers(binding))
    {
        return;
    }

    lv_obj_t* key_target = get_key_target(binding);
    if (!is_valid(key_target))
    {
        return;
    }

    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    TWO_PANE_NAV_LOG("[TwoPaneNav] root_click current=%p target=%p column=%s\n",
                     lv_event_get_current_target(e),
                     target,
                     column_name(binding->column));
    while (target && target != key_target)
    {
        if (activate_filter_button(binding, target))
        {
            return;
        }
        if (activate_list_back(binding, target))
        {
            return;
        }
        if (activate_list_button(binding, target))
        {
            return;
        }
        target = lv_obj_get_parent(target);
    }
}

static void managed_click_event_cb(lv_event_t* e)
{
    auto* binding = static_cast<Binding*>(lv_event_get_user_data(e));
    if (!binding_alive(binding) || !has_activation_handlers(binding))
    {
        return;
    }

    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    if (!is_valid(target))
    {
        return;
    }

    TWO_PANE_NAV_LOG("[TwoPaneNav] managed_click code=%d current=%p target=%p column=%s\n",
                     (int)lv_event_get_code(e),
                     target,
                     lv_event_get_target(e),
                     column_name(binding->column));

    if (activate_filter_button(binding, target))
    {
        return;
    }
    if (activate_list_back(binding, target))
    {
        return;
    }
    (void)activate_list_button(binding, target);
}

} // namespace

void init(Binding* binding, const Adapter& adapter)
{
    if (!binding) return;
    if (binding->bound)
    {
        cleanup(binding);
    }

    binding->adapter = adapter;
    binding->group = lv_group_create();
    binding->prev_group = lv_group_get_default();
    binding->encoder = find_encoder_indev();
    binding->column = FocusColumn::Filter;
    binding->bound = true;

    set_default_group(nullptr);
    set_default_group(binding->group);
    lv_group_set_default(nullptr);
    lv_group_set_editing(binding->group, false);
    notify_column_changed(binding);
    rebind_by_column(binding);

    if (lv_obj_t* key_target = get_key_target(binding))
    {
        if (is_valid(key_target))
        {
            lv_obj_remove_event_cb(key_target, root_key_event_cb);
            lv_obj_add_event_cb(key_target, root_key_event_cb, LV_EVENT_KEY, binding);
            lv_obj_remove_event_cb(key_target, root_click_event_cb);
        }
    }
}

void cleanup(Binding* binding)
{
    if (!binding || !binding->bound) return;

    if (binding->group)
    {
        set_default_group(nullptr);
        lv_group_del(binding->group);
        binding->group = nullptr;
    }
    if (binding->prev_group)
    {
        set_default_group(binding->prev_group);
    }

    binding->prev_group = nullptr;
    binding->encoder = nullptr;
    binding->bound = false;
    binding->adapter = Adapter{};
    binding->column = FocusColumn::Filter;
}

void on_ui_refreshed(Binding* binding)
{
    if (!binding_alive(binding) || !binding->group) return;
    lv_group_set_editing(binding->group, false);
    prepare_touch_routing(binding);

    if (binding->column == FocusColumn::List)
    {
        rebind_by_column(binding);
        return;
    }

    lv_obj_t* focused = lv_group_get_focused(binding->group);
    if (is_visible(focused))
    {
        return;
    }

    bind_filter_column(binding);
}

void focus_filter(Binding* binding)
{
    if (!binding_alive(binding) || !binding->group) return;
    set_column(binding, FocusColumn::Filter);
    rebind_by_column(binding);
}

void focus_list(Binding* binding)
{
    if (!binding_alive(binding) || !binding->group) return;
    set_column(binding, FocusColumn::List);
    rebind_by_column(binding);
}

lv_group_t* get_group(const Binding* binding)
{
    return binding ? binding->group : nullptr;
}

} // namespace two_pane_nav
} // namespace components
} // namespace ui
