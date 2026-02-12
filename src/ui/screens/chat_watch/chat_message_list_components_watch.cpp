#if defined(ARDUINO_T_WATCH_S3)

#include "chat_message_list_components_watch.h"

#include "../../ui_common.h"
#include "../../ui_theme.h"
#include <cstdio>

namespace
{
constexpr lv_coord_t kMenuButtonHeight = 46;
} // namespace

namespace chat::ui
{

ChatMessageListScreen::ChatMessageListScreen(lv_obj_t* parent)
{
    guard_ = new LifetimeGuard();
    guard_->alive = true;

    container_ = lv_obj_create(parent);
    lv_obj_set_size(container_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(container_, ::ui::theme::page_bg(), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    menu_panel_ = lv_obj_create(container_);
    lv_obj_set_size(menu_panel_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(menu_panel_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(menu_panel_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(menu_panel_, 0, 0);
    lv_obj_set_style_pad_left(menu_panel_, 16, 0);
    lv_obj_set_style_pad_right(menu_panel_, 16, 0);
    lv_obj_set_style_pad_top(menu_panel_, 14, 0);
    lv_obj_set_style_pad_bottom(menu_panel_, 14, 0);
    lv_obj_set_style_pad_row(menu_panel_, 12, 0);
    lv_obj_set_flex_flow(menu_panel_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(menu_panel_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(menu_panel_, LV_OBJ_FLAG_SCROLLABLE);

    showMenu();
}

ChatMessageListScreen::~ChatMessageListScreen()
{
    if (guard_)
    {
        guard_->alive = false;
    }
    if (container_)
    {
        lv_obj_del(container_);
        container_ = nullptr;
    }
    delete guard_;
    guard_ = nullptr;
}

void ChatMessageListScreen::setConversations(const std::vector<chat::ConversationMeta>& convs)
{
    convs_ = convs;
    refreshTargets();
    if (view_mode_ == ViewMode::Menu)
    {
        rebuildMenu();
    }
    else
    {
        rebuildList();
    }
}

void ChatMessageListScreen::updateBatteryFromBoard()
{
    (void)0;
}

void ChatMessageListScreen::setSelected(int index)
{
    selected_index_ = index;
}

void ChatMessageListScreen::setSelectedConversation(const chat::ConversationId& conv)
{
    for (size_t i = 0; i < convs_.size(); ++i)
    {
        if (convs_[i].id == conv)
        {
            selected_index_ = static_cast<int>(i);
            return;
        }
    }
    selected_index_ = 0;
}

chat::ConversationId ChatMessageListScreen::getSelectedConversation() const
{
    if (selected_index_ < 0 || selected_index_ >= static_cast<int>(convs_.size()))
    {
        return chat::ConversationId();
    }
    return convs_[selected_index_].id;
}

void ChatMessageListScreen::setActionCallback(void (*cb)(ActionIntent intent,
                                                        const chat::ConversationId& conv,
                                                        void*),
                                              void* user_data)
{
    action_cb_ = cb;
    action_cb_user_data_ = user_data;
}

lv_obj_t* ChatMessageListScreen::getItemButton(size_t index) const
{
    if (index >= list_items_.size())
    {
        return nullptr;
    }
    return list_items_[index].btn;
}

void ChatMessageListScreen::showMenu()
{
    view_mode_ = ViewMode::Menu;
    if (!menu_panel_)
    {
        return;
    }
    lv_obj_clear_flag(menu_panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(menu_panel_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clean(menu_panel_);
    list_items_.clear();

    auto make_btn = [&](lv_obj_t** btn_out, lv_obj_t** label_out, const char* text) {
        lv_obj_t* btn = lv_btn_create(menu_panel_);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, kMenuButtonHeight);
        lv_obj_set_style_bg_color(btn, ::ui::theme::surface(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn, ::ui::theme::border(), LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_left(btn, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_right(btn, 12, LV_PART_MAIN);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(btn, menu_event_cb, LV_EVENT_CLICKED, this);
        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, text);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_text_color(label, ::ui::theme::text(), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
        if (btn_out) *btn_out = btn;
        if (label_out) *label_out = label;
    };

    make_btn(&direct_btn_, &direct_label_, "Direct");
    make_btn(&broadcast_btn_, &broadcast_label_, "Broadcast");
    make_btn(&back_btn_, &back_label_, "Back");
    rebuildMenu();
}

void ChatMessageListScreen::showList(ViewMode mode)
{
    view_mode_ = mode;
    if (!menu_panel_)
    {
        return;
    }
    lv_obj_add_flag(menu_panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(menu_panel_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(menu_panel_, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_clean(menu_panel_);
    list_items_.clear();
    rebuildList();
}

void ChatMessageListScreen::refreshTargets()
{
    has_direct_ = false;
    has_broadcast_ = true;
    direct_unread_ = 0;
    broadcast_unread_ = 0;
    broadcast_conv_ = chat::ConversationId(chat::ChannelId::PRIMARY, 0);

    for (const auto& conv : convs_)
    {
        if (conv.id.peer == 0)
        {
            broadcast_unread_ += conv.unread;
            continue;
        }

        if (!has_direct_)
        {
            direct_conv_ = conv.id;
            has_direct_ = true;
        }
        direct_unread_ += conv.unread;
    }
}

void ChatMessageListScreen::rebuildMenu()
{
    if (!direct_label_ || !broadcast_label_ || !back_label_)
    {
        return;
    }

    char buf[32];
    if (direct_unread_ > 0)
    {
        snprintf(buf, sizeof(buf), "Direct (%d)", direct_unread_);
        lv_label_set_text(direct_label_, buf);
    }
    else
    {
        lv_label_set_text(direct_label_, "Direct");
    }

    if (broadcast_unread_ > 0)
    {
        snprintf(buf, sizeof(buf), "Broadcast (%d)", broadcast_unread_);
        lv_label_set_text(broadcast_label_, buf);
    }
    else
    {
        lv_label_set_text(broadcast_label_, "Broadcast");
    }

    lv_label_set_text(back_label_, "Back");

    if (!has_direct_)
    {
        lv_obj_add_state(direct_btn_, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_clear_state(direct_btn_, LV_STATE_DISABLED);
    }

    if (!has_broadcast_)
    {
        lv_obj_add_state(broadcast_btn_, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_clear_state(broadcast_btn_, LV_STATE_DISABLED);
    }
}

void ChatMessageListScreen::rebuildList()
{
    if (!menu_panel_)
    {
        return;
    }
    list_items_.clear();

    bool show_direct = (view_mode_ == ViewMode::ListDirect);
    bool show_broadcast = (view_mode_ == ViewMode::ListBroadcast);
    int item_count = 0;

    for (const auto& conv : convs_)
    {
        if (show_direct && conv.id.peer == 0)
        {
            continue;
        }
        if (show_broadcast && conv.id.peer != 0)
        {
            continue;
        }

        ListItem item{};
        item.conv = conv.id;
        item.is_back = false;

        lv_obj_t* btn = lv_btn_create(menu_panel_);
        item.btn = btn;
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, kMenuButtonHeight);
        lv_obj_set_style_bg_color(btn, ::ui::theme::surface(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn, ::ui::theme::border(), LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_left(btn, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_right(btn, 12, LV_PART_MAIN);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(btn, menu_event_cb, LV_EVENT_CLICKED, this);

        lv_obj_t* label = lv_label_create(btn);
        item.label = label;
        std::string name = conv.name.empty() ? std::string("Chat") : conv.name;
        if (conv.unread > 0)
        {
            char unread_buf[16];
            snprintf(unread_buf, sizeof(unread_buf), " (%d)", conv.unread);
            name += unread_buf;
        }
        lv_label_set_text(label, name.c_str());
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_text_color(label, ::ui::theme::text(), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);

        list_items_.push_back(item);
        item_count++;
    }

    if (show_broadcast && item_count == 0)
    {
        ListItem new_item{};
        new_item.is_back = false;
        new_item.conv = broadcast_conv_;
        new_item.btn = lv_btn_create(menu_panel_);
        lv_obj_set_width(new_item.btn, LV_PCT(100));
        lv_obj_set_height(new_item.btn, kMenuButtonHeight);
        lv_obj_set_style_bg_color(new_item.btn, ::ui::theme::accent(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(new_item.btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(new_item.btn, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(new_item.btn, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_left(new_item.btn, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_right(new_item.btn, 12, LV_PART_MAIN);
        lv_obj_clear_flag(new_item.btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(new_item.btn, menu_event_cb, LV_EVENT_CLICKED, this);
        new_item.label = lv_label_create(new_item.btn);
        lv_label_set_text(new_item.label, "New");
        lv_obj_align(new_item.label, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_text_color(new_item.label, ::ui::theme::white(), 0);
        lv_obj_set_style_text_font(new_item.label, &lv_font_montserrat_18, 0);
        list_items_.push_back(new_item);
    }

    ListItem back_item{};
    back_item.is_back = true;
    back_item.conv = chat::ConversationId();
    back_item.btn = lv_btn_create(menu_panel_);
    lv_obj_set_width(back_item.btn, LV_PCT(100));
    lv_obj_set_height(back_item.btn, kMenuButtonHeight);
    lv_obj_set_style_bg_color(back_item.btn, ::ui::theme::surface_alt(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(back_item.btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(back_item.btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(back_item.btn, ::ui::theme::border(), LV_PART_MAIN);
    lv_obj_set_style_radius(back_item.btn, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_left(back_item.btn, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_right(back_item.btn, 12, LV_PART_MAIN);
    lv_obj_clear_flag(back_item.btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(back_item.btn, menu_event_cb, LV_EVENT_CLICKED, this);
    back_item.label = lv_label_create(back_item.btn);
    lv_label_set_text(back_item.label, "Back");
    lv_obj_align(back_item.label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_text_color(back_item.label, ::ui::theme::text(), 0);
    lv_obj_set_style_text_font(back_item.label, &lv_font_montserrat_18, 0);
    list_items_.push_back(back_item);
}

void ChatMessageListScreen::schedule_action_async(ActionIntent intent, const chat::ConversationId& conv)
{
    if (!action_cb_)
    {
        return;
    }

    auto* payload = new ActionPayload();
    payload->guard = guard_;
    payload->action_cb = action_cb_;
    payload->user_data = action_cb_user_data_;
    payload->intent = intent;
    payload->conv = conv;

    lv_async_call(async_action_cb, payload);
}

void ChatMessageListScreen::menu_event_cb(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }
    auto* screen = static_cast<ChatMessageListScreen*>(lv_event_get_user_data(e));
    if (!screen)
    {
        return;
    }
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (screen->view_mode_ == ViewMode::Menu)
    {
        if (target == screen->direct_btn_)
        {
            if (screen->has_direct_)
            {
                screen->showList(ViewMode::ListDirect);
            }
            return;
        }
        if (target == screen->broadcast_btn_)
        {
            if (screen->has_broadcast_)
            {
                screen->showList(ViewMode::ListBroadcast);
            }
            return;
        }
        if (target == screen->back_btn_)
        {
            screen->schedule_action_async(ActionIntent::Back, chat::ConversationId());
        }
        return;
    }

    for (const auto& item : screen->list_items_)
    {
        if (item.btn != target)
        {
            continue;
        }
        if (item.is_back)
        {
            screen->showMenu();
            return;
        }
        screen->schedule_action_async(ActionIntent::SelectConversation, item.conv);
        return;
    }
}

void ChatMessageListScreen::async_action_cb(void* user_data)
{
    auto* payload = static_cast<ActionPayload*>(user_data);
    if (!payload)
    {
        return;
    }
    if (payload->guard && payload->guard->alive && payload->action_cb)
    {
        payload->action_cb(payload->intent, payload->conv, payload->user_data);
    }
    delete payload;
}

void ChatMessageListScreen::handle_back(void* user_data)
{
    auto* screen = static_cast<ChatMessageListScreen*>(user_data);
    if (!screen)
    {
        return;
    }
    screen->schedule_action_async(ActionIntent::Back, chat::ConversationId());
}

} // namespace chat::ui

#endif
