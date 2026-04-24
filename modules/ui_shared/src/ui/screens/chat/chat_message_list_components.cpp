#if !defined(ARDUINO_T_WATCH_S3)
/**
 * @file chat_message_list_components.cpp
 * @brief Chat message list screen implementation (explicit architecture version)
 */

#include "ui/screens/chat/chat_message_list_components.h"

#include "ui/app_runtime.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/components/info_card.h"
#include "ui/localization.h"
#include "ui/screens/chat/chat_message_list_input.h"
#include "ui/screens/chat/chat_message_list_layout.h"
#include "ui/screens/chat/chat_message_list_styles.h"
#include "ui/ui_common.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>

#ifndef CHAT_MESSAGE_LIST_LOG_ENABLE
#define CHAT_MESSAGE_LIST_LOG_ENABLE 1
#endif

#if CHAT_MESSAGE_LIST_LOG_ENABLE
#define CHAT_MESSAGE_LIST_LOG(...) std::printf(__VA_ARGS__)
#else
#define CHAT_MESSAGE_LIST_LOG(...)
#endif

namespace chat
{
namespace ui
{

// ---- small helper (logic only, unchanged) ----
namespace
{
static bool show_second_column_back()
{
    return !::ui::components::info_card::use_tdeck_layout();
}

static bool use_group_navigation()
{
#if defined(USING_INPUT_DEV_TOUCHPAD)
    return false;
#else
    return true;
#endif
}
} // namespace

static bool is_team_conversation(const chat::ConversationId& conv)
{
    constexpr uint8_t kTeamChatChannelRaw = 2;
    constexpr chat::ChannelId kTeamChatChannel =
        static_cast<chat::ChannelId>(kTeamChatChannelRaw);
    return conv.channel == kTeamChatChannel && conv.peer == 0;
}

static bool conversation_meta_equal(const chat::ConversationMeta& lhs,
                                    const chat::ConversationMeta& rhs)
{
    return lhs.id == rhs.id &&
           lhs.name == rhs.name &&
           lhs.preview == rhs.preview &&
           lhs.last_timestamp == rhs.last_timestamp &&
           lhs.unread == rhs.unread;
}

static bool conversation_list_equal(const std::vector<chat::ConversationMeta>& lhs,
                                    const std::vector<chat::ConversationMeta>& rhs)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }
    for (size_t index = 0; index < lhs.size(); ++index)
    {
        if (!conversation_meta_equal(lhs[index], rhs[index]))
        {
            return false;
        }
    }
    return true;
}

static bool conversation_identity_list_equal(const std::vector<chat::ConversationMeta>& lhs,
                                             const std::vector<chat::ConversationMeta>& rhs)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }
    for (size_t index = 0; index < lhs.size(); ++index)
    {
        if (!(lhs[index].id == rhs[index].id))
        {
            return false;
        }
    }
    return true;
}

static const char* touch_event_name(lv_event_code_t code)
{
    switch (code)
    {
    case LV_EVENT_PRESSED:
        return "PRESSED";
    case LV_EVENT_PRESSING:
        return "PRESSING";
    case LV_EVENT_RELEASED:
        return "RELEASED";
    case LV_EVENT_CLICKED:
        return "CLICKED";
    case LV_EVENT_FOCUSED:
        return "FOCUSED";
    case LV_EVENT_DEFOCUSED:
        return "DEFOCUSED";
    default:
        return "OTHER";
    }
}

static bool event_input_is_pointer(lv_event_t* e)
{
    lv_indev_t* indev = e ? lv_event_get_indev(e) : nullptr;
    if (!indev)
    {
        indev = lv_indev_get_act();
    }
    return indev && lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER;
}

static void disable_touch_click_focus_recursive(lv_obj_t* obj)
{
    if (!obj || use_group_navigation())
    {
        return;
    }
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_clear_state(obj, static_cast<lv_state_t>(LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY));
    const uint32_t child_count = lv_obj_get_child_count(obj);
    for (uint32_t child_index = 0; child_index < child_count; ++child_index)
    {
        disable_touch_click_focus_recursive(lv_obj_get_child(obj, child_index));
    }
}

static void normalize_touch_focus_tree(lv_obj_t* root)
{
    disable_touch_click_focus_recursive(root);
}

static void log_obj_snapshot(const char* tag, lv_obj_t* obj)
{
    if (!obj)
    {
        CHAT_MESSAGE_LIST_LOG("[ChatMessageList][State] %s obj=null\n", tag);
        return;
    }
    if (!lv_obj_is_valid(obj))
    {
        CHAT_MESSAGE_LIST_LOG("[ChatMessageList][State] %s obj=%p invalid\n", tag, obj);
        return;
    }

    const lv_state_t state = lv_obj_get_state(obj);
    CHAT_MESSAGE_LIST_LOG(
        "[ChatMessageList][State] %s obj=%p state=0x%X focused=%d focus_key=%d pressed=%d "
        "checked=%d clickable=%d click_focusable=%d checkable=%d press_lock=%d group=%p "
        "parent=%p child_count=%u\n",
        tag,
        obj,
        static_cast<unsigned>(state),
        lv_obj_has_state(obj, LV_STATE_FOCUSED) ? 1 : 0,
        lv_obj_has_state(obj, LV_STATE_FOCUS_KEY) ? 1 : 0,
        lv_obj_has_state(obj, LV_STATE_PRESSED) ? 1 : 0,
        lv_obj_has_state(obj, LV_STATE_CHECKED) ? 1 : 0,
        lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE) ? 1 : 0,
        lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICK_FOCUSABLE) ? 1 : 0,
        lv_obj_has_flag(obj, LV_OBJ_FLAG_CHECKABLE) ? 1 : 0,
        lv_obj_has_flag(obj, LV_OBJ_FLAG_PRESS_LOCK) ? 1 : 0,
        lv_obj_get_group(obj),
        lv_obj_get_parent(obj),
        static_cast<unsigned>(lv_obj_get_child_count(obj)));
}

static void log_item_tree(const char* tag, lv_obj_t* item)
{
    log_obj_snapshot(tag, item);
    if (!item || !lv_obj_is_valid(item))
    {
        return;
    }

    for (uint32_t child_index = 0; child_index < lv_obj_get_child_count(item); ++child_index)
    {
        lv_obj_t* child = lv_obj_get_child(item, child_index);
        char child_tag[64];
        std::snprintf(child_tag,
                      sizeof(child_tag),
                      "%s.child[%u]",
                      tag,
                      static_cast<unsigned>(child_index));
        log_obj_snapshot(child_tag, child);
    }
}

// ------------------------------------------------

ChatMessageListScreen::ChatMessageListScreen(lv_obj_t* parent)
    : container_(nullptr),
      filter_panel_(nullptr),
      list_panel_(nullptr),
      direct_btn_(nullptr),
      broadcast_btn_(nullptr),
      list_back_btn_(nullptr),
      selected_index_(-1),
      filter_mode_(FilterMode::Direct),
      action_cb_(nullptr),
      action_cb_user_data_(nullptr)
{
    guard_ = new LifetimeGuard();
    guard_->alive = true;
    guard_->pending_async = 0;

    lv_obj_t* active = lv_screen_active();
    if (!active)
    {
        CHAT_MESSAGE_LIST_LOG("[ChatMessageList] WARNING: lv_screen_active() is null\n");
    }
    else
    {
        CHAT_MESSAGE_LIST_LOG("[ChatMessageList] init: active=%p parent=%p\n", active, parent);
    }

    lv_group_t* prev_group = lv_group_get_default();
    set_default_group(nullptr);

    // ---------- Layout ----------
    auto w = chat::ui::layout::create_layout(parent);
    container_ = w.root;
    filter_panel_ = w.filter_panel;
    list_panel_ = w.list_panel;
    direct_btn_ = w.direct_btn;
    broadcast_btn_ = w.broadcast_btn;
    team_btn_ = w.team_btn;
    air_status_footer_ = w.air_status_footer;

    // ---------- Styles ----------
    chat::ui::message_list::styles::apply_root_container(container_);
    chat::ui::message_list::styles::apply_filter_panel(filter_panel_);
    chat::ui::message_list::styles::apply_panel(list_panel_);
    if (direct_btn_) chat::ui::message_list::styles::apply_filter_btn(direct_btn_);
    if (broadcast_btn_) chat::ui::message_list::styles::apply_filter_btn(broadcast_btn_);
    if (team_btn_) chat::ui::message_list::styles::apply_filter_btn(team_btn_);
    auto apply_filter_label = [](lv_obj_t* btn)
    {
        if (!btn) return;
        lv_obj_t* label = lv_obj_get_child(btn, 0);
        if (label)
        {
            chat::ui::message_list::styles::apply_filter_label(label);
        }
    };
    apply_filter_label(direct_btn_);
    apply_filter_label(broadcast_btn_);
    apply_filter_label(team_btn_);

    // ---------- Top bar (existing widget, unchanged) ----------
    ::ui::widgets::top_bar_init(top_bar_, container_);
    ::ui::widgets::top_bar_set_title(top_bar_, ::ui::i18n::tr("MESSAGES"));
    ::ui::widgets::top_bar_set_right_text(top_bar_, "--:--  --%");
    ::ui::widgets::top_bar_set_back_callback(top_bar_, handle_back, this);
    if (top_bar_.container)
    {
        lv_obj_move_to_index(top_bar_.container, 0);
    }

    if (container_)
    {
        lv_obj_add_event_cb(container_, on_root_deleted, LV_EVENT_DELETE, this);
        lv_obj_add_event_cb(container_, debug_touch_event_cb, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(container_, debug_touch_event_cb, LV_EVENT_CLICKED, this);
        if (list_panel_)
        {
            lv_obj_add_event_cb(list_panel_, debug_touch_event_cb, LV_EVENT_PRESSED, this);
            lv_obj_add_event_cb(list_panel_, debug_touch_event_cb, LV_EVENT_CLICKED, this);
        }
    }
    disable_touch_click_focus_recursive(container_);

    // ---------- Filter events ----------
    if (direct_btn_)
    {
        if (use_group_navigation())
        {
            lv_obj_add_event_cb(direct_btn_, filter_focus_cb, LV_EVENT_FOCUSED, this);
        }
        lv_obj_add_event_cb(direct_btn_, filter_click_cb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(direct_btn_, debug_touch_event_cb, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(direct_btn_, debug_touch_event_cb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(direct_btn_, debug_touch_event_cb, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(direct_btn_, debug_touch_event_cb, LV_EVENT_DEFOCUSED, this);
    }
    if (broadcast_btn_)
    {
        if (use_group_navigation())
        {
            lv_obj_add_event_cb(broadcast_btn_, filter_focus_cb, LV_EVENT_FOCUSED, this);
        }
        lv_obj_add_event_cb(broadcast_btn_, filter_click_cb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(broadcast_btn_, debug_touch_event_cb, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(broadcast_btn_, debug_touch_event_cb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(broadcast_btn_, debug_touch_event_cb, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(broadcast_btn_, debug_touch_event_cb, LV_EVENT_DEFOCUSED, this);
    }
    if (team_btn_)
    {
        if (use_group_navigation())
        {
            lv_obj_add_event_cb(team_btn_, filter_focus_cb, LV_EVENT_FOCUSED, this);
        }
        lv_obj_add_event_cb(team_btn_, filter_click_cb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(team_btn_, debug_touch_event_cb, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(team_btn_, debug_touch_event_cb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(team_btn_, debug_touch_event_cb, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(team_btn_, debug_touch_event_cb, LV_EVENT_DEFOCUSED, this);
    }
    updateFilterHighlight();
    disable_touch_click_focus_recursive(container_);

    if (container_ && !lv_obj_is_valid(container_))
    {
        CHAT_MESSAGE_LIST_LOG("[ChatMessageList] WARNING: container invalid\n");
    }
    if (list_panel_ && !lv_obj_is_valid(list_panel_))
    {
        CHAT_MESSAGE_LIST_LOG("[ChatMessageList] WARNING: list_panel invalid\n");
    }

    set_default_group(prev_group);
    CHAT_MESSAGE_LIST_LOG(
        "[ChatMessageList] ctor use_group_navigation=%d prev_group=%p restored_group=%p root=%p list=%p "
        "filter=%p\n",
        use_group_navigation() ? 1 : 0,
        prev_group,
        lv_group_get_default(),
        container_,
        list_panel_,
        filter_panel_);
    log_obj_snapshot("ctor.root", container_);
    log_obj_snapshot("ctor.list_panel", list_panel_);
    log_obj_snapshot("ctor.filter_panel", filter_panel_);
    log_obj_snapshot("ctor.direct_btn", direct_btn_);
    log_obj_snapshot("ctor.broadcast_btn", broadcast_btn_);
    log_obj_snapshot("ctor.team_btn", team_btn_);

    // ---------- Input layer ----------
    if (use_group_navigation())
    {
        chat::ui::message_list::input::init(this, &input_controller_);
    }
    CHAT_MESSAGE_LIST_LOG("[ChatMessageList] ctor input_group=%p\n", input_controller_.group());
}

ChatMessageListScreen::~ChatMessageListScreen()
{
    if (container_ && lv_obj_is_valid(container_))
    {
        lv_obj_del(container_);
    }

    if (guard_)
    {
        guard_->alive = false;
        guard_->owner_dead = true;
        if (guard_->pending_async == 0)
        {
            delete guard_;
        }
        guard_ = nullptr;
    }
}

void ChatMessageListScreen::setConversations(const std::vector<chat::ConversationMeta>& convs)
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }

    bool has_team = false;
    for (const auto& conv : convs)
    {
        if (is_team_conversation(conv.id))
        {
            has_team = true;
            break;
        }
    }

    const bool team_visibility_changed = team_btn_ &&
                                         (has_team == lv_obj_has_flag(team_btn_, LV_OBJ_FLAG_HIDDEN));
    const bool conversations_changed = !conversation_list_equal(convs_, convs);

    if (!conversations_changed && !team_visibility_changed)
    {
        return;
    }

    CHAT_MESSAGE_LIST_LOG("[ChatMessageList] setConversations changed=%d team_visibility_changed=%d size=%u\n",
                          conversations_changed ? 1 : 0,
                          team_visibility_changed ? 1 : 0,
                          (unsigned)convs.size());

    const std::vector<chat::ConversationMeta> previous_convs = convs_;
    convs_ = convs;
    if (team_btn_)
    {
        if (has_team)
        {
            lv_obj_clear_flag(team_btn_, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(team_btn_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (!has_team && filter_mode_ == FilterMode::Team)
    {
        filter_mode_ = FilterMode::Broadcast;
    }
    updateFilterHighlight();
    if (!conversation_identity_list_equal(previous_convs, convs_) ||
        !updateListInPlace(convs_))
    {
        rebuildList();
    }
}

void ChatMessageListScreen::setSelected(int index)
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }
    if (index >= 0 && index < static_cast<int>(items_.size()) &&
        items_[index].btn != nullptr)
    {
        selected_index_ = index;
        return;
    }
    selected_index_ = -1;
}

void ChatMessageListScreen::setSelectedConversation(const chat::ConversationId& conv)
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }
    selected_index_ = -1;
    for (size_t i = 0; i < items_.size(); ++i)
    {
        if (items_[i].conv == conv)
        {
            setSelected(static_cast<int>(i));
            return;
        }
    }
}

bool ChatMessageListScreen::tryGetSelectedConversation(chat::ConversationId* conv) const
{
    if (!guard_ || !guard_->alive || !conv)
    {
        return false;
    }
    if (selected_index_ >= 0 &&
        selected_index_ < static_cast<int>(items_.size()))
    {
        *conv = items_[selected_index_].conv;
        return true;
    }

    if (use_group_navigation())
    {
        lv_group_t* group = input_controller_.group();
        lv_obj_t* focused = group ? lv_group_get_focused(group) : nullptr;
        if (focused && lv_obj_is_valid(focused))
        {
            for (const auto& item : items_)
            {
                if (item.btn == focused)
                {
                    *conv = item.conv;
                    return true;
                }
            }
        }
    }

    return false;
}

chat::ConversationId ChatMessageListScreen::getSelectedConversation() const
{
    chat::ConversationId conv;
    (void)tryGetSelectedConversation(&conv);
    return conv;
}

lv_obj_t* ChatMessageListScreen::getItemButton(size_t index) const
{
    if (!guard_ || !guard_->alive)
    {
        return nullptr;
    }
    if (index >= items_.size())
    {
        return nullptr;
    }
    return items_[index].btn;
}

void ChatMessageListScreen::setActionCallback(
    void (*cb)(ActionIntent intent, const chat::ConversationId& conv, void*),
    void* user_data)
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }
    action_cb_ = cb;
    action_cb_user_data_ = user_data;
}

void ChatMessageListScreen::updateBatteryFromBoard()
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }
    ::ui::components::air_status_footer::refresh(air_status_footer_);
    ui_update_top_bar_battery(top_bar_);
}

// ------------------------------------------------
// Core logic: rebuild list (behavior unchanged)
// ------------------------------------------------
void ChatMessageListScreen::rebuildList()
{
    if (!guard_ || !guard_->alive || !list_panel_ || !lv_obj_is_valid(list_panel_))
    {
        return;
    }
    chat::ConversationId previous_selected{};
    const bool had_previous_selected = tryGetSelectedConversation(&previous_selected);
    CHAT_MESSAGE_LIST_LOG("[ChatMessageList] rebuildList begin convs=%u selected_before=%d had_previous_selected=%d prev_selected_peer=%lu\n",
                          static_cast<unsigned>(convs_.size()),
                          selected_index_,
                          had_previous_selected ? 1 : 0,
                          static_cast<unsigned long>(previous_selected.peer));
    log_obj_snapshot("rebuild.list_panel.before", list_panel_);

    // Same behavior: clear and rebuild
    lv_obj_clean(list_panel_);
    items_.clear();
    list_back_btn_ = nullptr;
    selected_index_ = -1;

    std::vector<chat::ConversationMeta> filtered;
    filtered.reserve(convs_.size());
    for (const auto& conv : convs_)
    {
        if (is_team_conversation(conv.id))
        {
            if (filter_mode_ == FilterMode::Team)
            {
                filtered.push_back(conv);
            }
            continue;
        }
        if (filter_mode_ == FilterMode::Direct && conv.id.peer != 0)
        {
            filtered.push_back(conv);
        }
        else if (filter_mode_ == FilterMode::Broadcast && conv.id.peer == 0)
        {
            filtered.push_back(conv);
        }
    }

    for (const auto& conv : filtered)
    {
        MessageItem item{};
        item.conv = conv.id;
        item.unread_count = conv.unread;

        // ----- Layout -----
        auto w = chat::ui::layout::create_message_item(list_panel_);
        item.btn = w.btn;
        item.name_label = w.name_label;
        item.preview_label = w.preview_label;
        item.time_label = w.time_label;
        item.unread_label = w.unread_label;

        // ----- Styles -----
        chat::ui::message_list::styles::apply_item_btn(item.btn);
        lv_obj_add_flag(item.btn, LV_OBJ_FLAG_EVENT_BUBBLE);
        chat::ui::message_list::styles::apply_label_name(item.name_label);
        chat::ui::message_list::styles::apply_label_preview(item.preview_label);
        chat::ui::message_list::styles::apply_label_time(item.time_label);
        chat::ui::message_list::styles::apply_label_unread(item.unread_label);
        disable_touch_click_focus_recursive(item.btn);
        lv_obj_clear_state(item.btn, (lv_state_t)(LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY));

        // ----- Content -----
        chat::ui::layout::MessageItemWidgets widgets{
            item.btn,
            item.name_label,
            item.preview_label,
            item.time_label,
            item.unread_label,
        };
        chat::ui::layout::populate_message_item(widgets, conv);

        // ----- Events (unchanged) -----
        lv_obj_add_event_cb(item.btn, item_event_cb, LV_EVENT_CLICKED, this);
        if (use_group_navigation())
        {
            lv_obj_add_event_cb(item.btn, item_focused_cb, LV_EVENT_FOCUSED, this);
        }
        lv_obj_add_event_cb(item.btn, debug_touch_event_cb, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(item.btn, debug_touch_event_cb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(item.btn, debug_touch_event_cb, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(item.btn, debug_touch_event_cb, LV_EVENT_DEFOCUSED, this);

        items_.push_back(item);
        CHAT_MESSAGE_LIST_LOG("[ChatMessageList] rebuildList item index=%u btn=%p\n",
                              (unsigned)(items_.size() - 1),
                              item.btn);
        char item_tag[48];
        std::snprintf(item_tag,
                      sizeof(item_tag),
                      "rebuild.item[%u]",
                      static_cast<unsigned>(items_.size() - 1));
        log_item_tree(item_tag, item.btn);
    }

    if (items_.empty())
    {
        lv_obj_t* placeholder = chat::ui::layout::create_placeholder(list_panel_);
        chat::ui::message_list::styles::apply_label_placeholder(placeholder);
        ::ui::i18n::set_label_text(placeholder, "No messages");
        ::ui::fonts::apply_localized_font(placeholder, lv_label_get_text(placeholder), ::ui::fonts::ui_chrome_font());
    }

    if (show_second_column_back())
    {
        list_back_btn_ = lv_obj_create(list_panel_);
        lv_obj_add_flag(list_back_btn_, LV_OBJ_FLAG_CLICKABLE);
        if (::ui::components::info_card::use_tdeck_layout())
        {
            ::ui::components::info_card::configure_item(list_back_btn_, 28);
            lv_obj_set_style_pad_row(list_back_btn_, 0, LV_PART_MAIN);
            lv_obj_set_flex_flow(list_back_btn_, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(list_back_btn_, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        }
        else
        {
            lv_obj_set_size(list_back_btn_, LV_PCT(100), 28);
            lv_obj_clear_flag(list_back_btn_, LV_OBJ_FLAG_SCROLLABLE);
        }
        chat::ui::message_list::styles::apply_item_btn(list_back_btn_);
        lv_obj_add_flag(list_back_btn_, LV_OBJ_FLAG_EVENT_BUBBLE);
        disable_touch_click_focus_recursive(list_back_btn_);
        lv_obj_clear_state(list_back_btn_, (lv_state_t)(LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY));
        lv_obj_t* back_label = lv_label_create(list_back_btn_);
        lv_obj_add_flag(back_label, LV_OBJ_FLAG_EVENT_BUBBLE);
        ::ui::i18n::set_label_text(back_label, "Back");
        ::ui::fonts::apply_localized_font(back_label, lv_label_get_text(back_label), ::ui::fonts::ui_chrome_font());
        chat::ui::message_list::styles::apply_label_name(back_label);
        lv_obj_center(back_label);
        lv_obj_add_event_cb(list_back_btn_, list_back_event_cb, LV_EVENT_CLICKED, this);
        if (use_group_navigation())
        {
            lv_obj_add_event_cb(list_back_btn_, item_focused_cb, LV_EVENT_FOCUSED, this);
        }
        lv_obj_add_event_cb(list_back_btn_, debug_touch_event_cb, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(list_back_btn_, debug_touch_event_cb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(list_back_btn_, debug_touch_event_cb, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(list_back_btn_, debug_touch_event_cb, LV_EVENT_DEFOCUSED, this);
        CHAT_MESSAGE_LIST_LOG("[ChatMessageList] rebuildList back_btn=%p\n", list_back_btn_);
        log_item_tree("rebuild.back_btn", list_back_btn_);
    }

    if (had_previous_selected && !items_.empty())
    {
        setSelectedConversation(previous_selected);
    }

    if (use_group_navigation())
    {
        chat::ui::message_list::input::on_ui_refreshed(&input_controller_);
    }
    disable_touch_click_focus_recursive(container_);
    CHAT_MESSAGE_LIST_LOG("[ChatMessageList] rebuildList done items=%u selected_after=%d input_group=%p\n",
                          static_cast<unsigned>(items_.size()),
                          selected_index_,
                          input_controller_.group());
}

bool ChatMessageListScreen::updateListInPlace(const std::vector<chat::ConversationMeta>& convs)
{
    if (!guard_ || !guard_->alive || !list_panel_ || !lv_obj_is_valid(list_panel_))
    {
        return false;
    }
    if (!conversation_identity_list_equal(convs_, convs))
    {
        return false;
    }

    std::vector<chat::ConversationMeta> filtered;
    filtered.reserve(convs.size());
    for (const auto& conv : convs)
    {
        if (is_team_conversation(conv.id))
        {
            if (filter_mode_ == FilterMode::Team)
            {
                filtered.push_back(conv);
            }
            continue;
        }
        if (filter_mode_ == FilterMode::Direct && conv.id.peer != 0)
        {
            filtered.push_back(conv);
        }
        else if (filter_mode_ == FilterMode::Broadcast && conv.id.peer == 0)
        {
            filtered.push_back(conv);
        }
    }

    if (filtered.size() != items_.size())
    {
        return false;
    }

    for (size_t index = 0; index < filtered.size(); ++index)
    {
        if (!(items_[index].conv == filtered[index].id))
        {
            return false;
        }
    }

    for (size_t index = 0; index < filtered.size(); ++index)
    {
        updateListItem(index, filtered[index]);
    }
    return true;
}

void ChatMessageListScreen::updateListItem(const size_t index,
                                           const chat::ConversationMeta& conv)
{
    if (index >= items_.size())
    {
        return;
    }

    MessageItem& item = items_[index];
    chat::ui::layout::MessageItemWidgets widgets{
        item.btn,
        item.name_label,
        item.preview_label,
        item.time_label,
        item.unread_label,
    };
    chat::ui::layout::populate_message_item(widgets, conv);
    item.unread_count = conv.unread;
}

void ChatMessageListScreen::item_event_cb(lv_event_t* e)
{
    auto* screen =
        static_cast<ChatMessageListScreen*>(lv_event_get_user_data(e));
    if (!screen || !screen->guard_ || !screen->guard_->alive)
    {
        return;
    }

    lv_obj_t* item = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    CHAT_MESSAGE_LIST_LOG("[ChatMessageList] item_click current=%p target=%p selected_before=%d\n",
                          item,
                          lv_event_get_target(e),
                          screen->selected_index_);
    log_item_tree("item_click.current.before", item);
    for (size_t i = 0; i < screen->items_.size(); i++)
    {
        if (screen->items_[i].btn == item)
        {
            screen->setSelected(static_cast<int>(i));
            normalize_touch_focus_tree(screen->container_);
            CHAT_MESSAGE_LIST_LOG("[ChatMessageList] item_click matched index=%u selected_after=%d peer=%lu\n",
                                  static_cast<unsigned>(i),
                                  screen->selected_index_,
                                  static_cast<unsigned long>(screen->items_[i].conv.peer));
            log_item_tree("item_click.current.after", item);
            screen->schedule_action_async(ActionIntent::SelectConversation,
                                          screen->items_[i].conv);
            break;
        }
    }
}

void ChatMessageListScreen::list_back_event_cb(lv_event_t* e)
{
    auto* screen =
        static_cast<ChatMessageListScreen*>(lv_event_get_user_data(e));
    if (!screen || !screen->guard_ || !screen->guard_->alive)
    {
        return;
    }
    (void)lv_event_get_current_target(e);
    if (use_group_navigation() && !event_input_is_pointer(e))
    {
        chat::ui::message_list::input::focus_filter(&screen->input_controller_);
    }
}

void ChatMessageListScreen::item_focused_cb(lv_event_t* e)
{
    auto* screen =
        static_cast<ChatMessageListScreen*>(lv_event_get_user_data(e));
    if (!screen || !screen->guard_ || !screen->guard_->alive)
    {
        return;
    }
    lv_obj_t* item = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    if (item && lv_obj_is_valid(item))
    {
        lv_obj_scroll_to_view(item, LV_ANIM_OFF);
    }
}

void ChatMessageListScreen::filter_focus_cb(lv_event_t* e)
{
    auto* screen =
        static_cast<ChatMessageListScreen*>(lv_event_get_user_data(e));
    if (!screen || !screen->guard_ || !screen->guard_->alive)
    {
        return;
    }
    lv_obj_t* tgt = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    if (tgt == screen->direct_btn_)
    {
        screen->setFilterMode(FilterMode::Direct);
    }
    else if (tgt == screen->broadcast_btn_)
    {
        screen->setFilterMode(FilterMode::Broadcast);
    }
    else if (tgt == screen->team_btn_)
    {
        screen->setFilterMode(FilterMode::Team);
    }
}

void ChatMessageListScreen::filter_click_cb(lv_event_t* e)
{
    auto* screen =
        static_cast<ChatMessageListScreen*>(lv_event_get_user_data(e));
    if (!screen || !screen->guard_ || !screen->guard_->alive)
    {
        return;
    }
    lv_obj_t* tgt = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    if (tgt == screen->direct_btn_)
    {
        screen->setFilterMode(FilterMode::Direct);
    }
    else if (tgt == screen->broadcast_btn_)
    {
        screen->setFilterMode(FilterMode::Broadcast);
    }
    else if (tgt == screen->team_btn_)
    {
        screen->setFilterMode(FilterMode::Team);
    }
    CHAT_MESSAGE_LIST_LOG("[ChatMessageList] filter_click target=%p pointer=%d mode=%d\n",
                          tgt,
                          event_input_is_pointer(e) ? 1 : 0,
                          static_cast<int>(screen->filter_mode_));
    normalize_touch_focus_tree(screen->container_);
    log_obj_snapshot("filter_click.target", tgt);
    if (use_group_navigation() && !event_input_is_pointer(e))
    {
        chat::ui::message_list::input::focus_list(&screen->input_controller_);
    }
}

void ChatMessageListScreen::debug_touch_event_cb(lv_event_t* e)
{
    auto* screen = static_cast<ChatMessageListScreen*>(lv_event_get_user_data(e));
    if (!screen || !screen->guard_ || !screen->guard_->alive)
    {
        return;
    }

    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* current = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    const char* role = "unknown";
    int index = -1;

    if (current == screen->list_panel_)
    {
        role = "list_panel";
    }
    else if (current == screen->container_)
    {
        role = "screen_root";
    }
    else if (current == screen->list_back_btn_)
    {
        role = "list_back";
    }
    else if (current == screen->direct_btn_)
    {
        role = "direct_filter";
    }
    else if (current == screen->broadcast_btn_)
    {
        role = "broadcast_filter";
    }
    else if (current == screen->team_btn_)
    {
        role = "team_filter";
    }
    else
    {
        for (size_t i = 0; i < screen->items_.size(); ++i)
        {
            if (screen->items_[i].btn == current)
            {
                role = "list_item";
                index = static_cast<int>(i);
                break;
            }
        }
    }

    CHAT_MESSAGE_LIST_LOG("[ChatMessageList][Touch] event=%s code=%d role=%s index=%d current=%p target=%p selected=%d items=%u\n",
                          touch_event_name(code),
                          (int)code,
                          role,
                          index,
                          current,
                          target,
                          screen->selected_index_,
                          (unsigned)screen->items_.size());
    log_obj_snapshot("touch.current", current);
    if (index >= 0)
    {
        log_item_tree("touch.item", current);
    }
}

void ChatMessageListScreen::updateFilterHighlight()
{
    if (!direct_btn_ || !broadcast_btn_)
    {
        return;
    }
    lv_obj_clear_state(direct_btn_, LV_STATE_CHECKED);
    lv_obj_clear_state(broadcast_btn_, LV_STATE_CHECKED);
    if (team_btn_)
    {
        lv_obj_clear_state(team_btn_, LV_STATE_CHECKED);
    }
    if (filter_mode_ == FilterMode::Direct)
    {
        lv_obj_add_state(direct_btn_, LV_STATE_CHECKED);
    }
    else if (filter_mode_ == FilterMode::Broadcast)
    {
        lv_obj_add_state(broadcast_btn_, LV_STATE_CHECKED);
    }
    else if (team_btn_)
    {
        lv_obj_add_state(team_btn_, LV_STATE_CHECKED);
    }
}

void ChatMessageListScreen::setFilterMode(FilterMode mode)
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }
    if (filter_mode_ == mode)
    {
        return;
    }
    filter_mode_ = mode;
    selected_index_ = -1;
    updateFilterHighlight();
    rebuildList();
}

void ChatMessageListScreen::handle_back(void* user_data)
{
    auto* screen = static_cast<ChatMessageListScreen*>(user_data);
    if (!screen || !screen->guard_ || !screen->guard_->alive)
    {
        return;
    }
    screen->schedule_action_async(ActionIntent::Back, chat::ConversationId());
}

void ChatMessageListScreen::async_action_cb(void* user_data)
{
    auto* payload = static_cast<ActionPayload*>(user_data);
    if (!payload)
    {
        return;
    }
    LifetimeGuard* guard = payload->guard;
    if (guard && guard->alive && payload->action_cb)
    {
        payload->action_cb(payload->intent, payload->conv, payload->user_data);
    }
    if (guard && guard->pending_async > 0)
    {
        guard->pending_async--;
        if (guard->owner_dead && !guard->alive && guard->pending_async == 0)
        {
            delete guard;
        }
    }
    delete payload;
}

void ChatMessageListScreen::on_root_deleted(lv_event_t* e)
{
    auto* screen = static_cast<ChatMessageListScreen*>(lv_event_get_user_data(e));
    if (!screen)
    {
        return;
    }
    screen->handle_root_deleted();
}

void ChatMessageListScreen::handle_root_deleted()
{
    if ((!guard_ || !guard_->alive) && container_ == nullptr)
    {
        return;
    }

    if (guard_)
    {
        guard_->alive = false;
    }
    action_cb_ = nullptr;
    action_cb_user_data_ = nullptr;

    if (use_group_navigation())
    {
        chat::ui::message_list::input::cleanup(&input_controller_);
    }
    clear_all_timers();

    if (top_bar_.back_btn)
    {
        ::ui::widgets::top_bar_set_back_callback(top_bar_, nullptr, nullptr);
    }

    items_.clear();
    convs_.clear();

    container_ = nullptr;
    filter_panel_ = nullptr;
    list_panel_ = nullptr;
    direct_btn_ = nullptr;
    broadcast_btn_ = nullptr;
    list_back_btn_ = nullptr;
}

void ChatMessageListScreen::schedule_action_async(ActionIntent intent,
                                                  const chat::ConversationId& conv)
{
    if (!guard_ || !guard_->alive || !action_cb_)
    {
        return;
    }
    auto* payload = new ActionPayload();
    payload->guard = guard_;
    payload->action_cb = action_cb_;
    payload->user_data = action_cb_user_data_;
    payload->intent = intent;
    payload->conv = conv;
    guard_->pending_async++;
    lv_async_call(async_action_cb, payload);
}

lv_timer_t* ChatMessageListScreen::add_timer(lv_timer_cb_t cb,
                                             uint32_t period_ms,
                                             void* user_data,
                                             TimerDomain domain)
{
    if (!guard_ || !guard_->alive)
    {
        return nullptr;
    }
    lv_timer_t* timer = lv_timer_create(cb, period_ms, user_data);
    if (timer)
    {
        TimerEntry entry;
        entry.timer = timer;
        entry.domain = domain;
        timers_.push_back(entry);
    }
    return timer;
}

void ChatMessageListScreen::clear_timers(TimerDomain domain)
{
    if (timers_.empty())
    {
        return;
    }
    for (auto& entry : timers_)
    {
        if (entry.timer && entry.domain == domain)
        {
            lv_timer_del(entry.timer);
            entry.timer = nullptr;
        }
    }
    timers_.erase(
        std::remove_if(timers_.begin(), timers_.end(),
                       [](const TimerEntry& entry)
                       { return entry.timer == nullptr; }),
        timers_.end());
}

void ChatMessageListScreen::clear_all_timers()
{
    for (auto& entry : timers_)
    {
        if (entry.timer)
        {
            lv_timer_del(entry.timer);
            entry.timer = nullptr;
        }
    }
    timers_.clear();
}

} // namespace ui
} // namespace chat

#endif
