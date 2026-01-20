/**
 * @file chat_message_list_components.cpp
 * @brief Chat message list screen implementation (explicit architecture version)
 */

#include "chat_message_list_components.h"

#include "../ui_common.h"
#include "chat_message_list_layout.h"
#include "chat_message_list_styles.h"
#include "chat_message_list_input.h"

#include <cstddef>
#include <cstring>
#include <ctime>
#include <cstdio>

namespace chat {
namespace ui {

// ---- small helper (logic only, unchanged) ----
static void format_time_hhmm(char out[16], uint32_t ts)
{
    if (ts == 0) {
        snprintf(out, 16, "--:--");
        return;
    }
    time_t t = ui_apply_timezone_offset(static_cast<time_t>(ts));
    struct tm* info = gmtime(&t);
    if (info) {
        strftime(out, 16, "%H:%M", info);
    } else {
        snprintf(out, 16, "--:--");
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
      selected_index_(0),
      filter_mode_(FilterMode::Direct),
      select_cb_(nullptr),
      select_cb_user_data_(nullptr),
      back_cb_(nullptr),
      back_cb_user_data_(nullptr)
{
    lv_obj_t* active = lv_screen_active();
    if (!active) {
        Serial.printf("[ChatMessageList] WARNING: lv_screen_active() is null\n");
    } else {
        Serial.printf("[ChatMessageList] init: active=%p parent=%p\n", active, parent);
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

    // ---------- Styles ----------
    chat::ui::message_list::styles::apply_root_container(container_);
    chat::ui::message_list::styles::apply_filter_panel(filter_panel_);
    chat::ui::message_list::styles::apply_panel(list_panel_);
    if (direct_btn_) chat::ui::message_list::styles::apply_filter_btn(direct_btn_);
    if (broadcast_btn_) chat::ui::message_list::styles::apply_filter_btn(broadcast_btn_);

    // ---------- Top bar (existing widget, unchanged) ----------
    ::ui::widgets::top_bar_init(top_bar_, container_);
    ::ui::widgets::top_bar_set_title(top_bar_, "MESSAGES");
    ::ui::widgets::top_bar_set_right_text(top_bar_, "--:--  --%");
    ::ui::widgets::top_bar_set_back_callback(top_bar_, handle_back, this);
    if (top_bar_.container) {
        lv_obj_move_to_index(top_bar_.container, 0);
    }

    // ---------- Filter events ----------
    if (direct_btn_) {
        lv_obj_add_event_cb(direct_btn_, filter_focus_cb, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(direct_btn_, filter_click_cb, LV_EVENT_CLICKED, this);
    }
    if (broadcast_btn_) {
        lv_obj_add_event_cb(broadcast_btn_, filter_focus_cb, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(broadcast_btn_, filter_click_cb, LV_EVENT_CLICKED, this);
    }
    updateFilterHighlight();

    if (container_ && !lv_obj_is_valid(container_)) {
        Serial.printf("[ChatMessageList] WARNING: container invalid\n");
    }
    if (list_panel_ && !lv_obj_is_valid(list_panel_)) {
        Serial.printf("[ChatMessageList] WARNING: list_panel invalid\n");
    }

    set_default_group(prev_group);

    // ---------- Input layer ----------
    chat::ui::message_list::input::init(this);
}

ChatMessageListScreen::~ChatMessageListScreen()
{
    // Explicit input cleanup (no behavior change)
    chat::ui::message_list::input::cleanup();

    // Same destruction behavior as original code
    for (auto& item : items_) {
        if (item.btn) {
            lv_obj_del(item.btn);
        }
    }

    if (container_) {
        lv_obj_del(container_);
    }
}

void ChatMessageListScreen::setConversations(const std::vector<chat::ConversationMeta>& convs)
{
    convs_ = convs;
    rebuildList();
}

void ChatMessageListScreen::setSelected(int index)
{
    if (index >= 0 && index < static_cast<int>(items_.size()) &&
        items_[index].btn != nullptr) {
        selected_index_ = index;
    }
}

void ChatMessageListScreen::setSelectedConversation(const chat::ConversationId& conv)
{
    for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].conv == conv) {
            setSelected(static_cast<int>(i));
            return;
        }
    }
}

chat::ConversationId ChatMessageListScreen::getSelectedConversation() const
{
    if (selected_index_ >= 0 &&
        selected_index_ < static_cast<int>(items_.size())) {
        return items_[selected_index_].conv;
    }
    return chat::ConversationId();
}

lv_obj_t* ChatMessageListScreen::getItemButton(size_t index) const
{
    if (index >= items_.size()) {
        return nullptr;
    }
    return items_[index].btn;
}

void ChatMessageListScreen::setChannelSelectCallback(
    void (*cb)(chat::ChannelId, void*),
    void* user_data)
{
    select_cb_ = cb;
    select_cb_user_data_ = user_data;
}

void ChatMessageListScreen::updateBatteryFromBoard()
{
    ui_update_top_bar_battery(top_bar_);
}

void ChatMessageListScreen::setBackCallback(void (*cb)(void*), void* user_data)
{
    back_cb_ = cb;
    back_cb_user_data_ = user_data;
}

// ------------------------------------------------
// Core logic: rebuild list (behavior unchanged)
// ------------------------------------------------
void ChatMessageListScreen::rebuildList()
{
    // Same behavior: clear and rebuild
    lv_obj_clean(list_panel_);
    items_.clear();
    list_back_btn_ = nullptr;

    std::vector<chat::ConversationMeta> filtered;
    filtered.reserve(convs_.size());
    for (const auto& conv : convs_) {
        if (filter_mode_ == FilterMode::Direct && conv.id.peer != 0) {
            filtered.push_back(conv);
        } else if (filter_mode_ == FilterMode::Broadcast && conv.id.peer == 0) {
            filtered.push_back(conv);
        }
    }

    for (const auto& conv : filtered) {
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
        chat::ui::message_list::styles::apply_label_name(item.name_label);
        chat::ui::message_list::styles::apply_label_preview(item.preview_label);
        chat::ui::message_list::styles::apply_label_time(item.time_label);
        chat::ui::message_list::styles::apply_label_unread(item.unread_label);
        lv_obj_clear_state(item.btn, (lv_state_t)(LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY));

        // ----- Content -----
        lv_label_set_text(item.name_label, conv.name.c_str());
        lv_label_set_text(item.preview_label, conv.preview.c_str());

        char time_buf[16];
        format_time_hhmm(time_buf, conv.last_timestamp);
        lv_label_set_text(item.time_label, time_buf);

        if (conv.unread > 0) {
            char unread_str[16];
            snprintf(unread_str, sizeof(unread_str), "%d", conv.unread);
            lv_label_set_text(item.unread_label, unread_str);
        } else {
            lv_label_set_text(item.unread_label, "");
        }

        // ----- Events (unchanged) -----
        lv_obj_add_event_cb(item.btn, item_event_cb, LV_EVENT_CLICKED, this);

        items_.push_back(item);
    }

    if (items_.empty()) {
        lv_obj_t* placeholder = chat::ui::layout::create_placeholder(list_panel_);
        chat::ui::message_list::styles::apply_label_placeholder(placeholder);
        lv_label_set_text(placeholder, "No messages");
        selected_index_ = -1;
    }

    // Append back button as the last item in list panel.
    list_back_btn_ = lv_btn_create(list_panel_);
    lv_obj_set_size(list_back_btn_, LV_PCT(100), 36);
    lv_obj_clear_flag(list_back_btn_, LV_OBJ_FLAG_SCROLLABLE);
    chat::ui::message_list::styles::apply_item_btn(list_back_btn_);
    lv_obj_clear_state(list_back_btn_, (lv_state_t)(LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY));
    lv_obj_t* back_label = lv_label_create(list_back_btn_);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(list_back_btn_, list_back_event_cb, LV_EVENT_CLICKED, this);

    if (!items_.empty()) {
        setSelected(0);
    }

    chat::ui::message_list::input::on_ui_refreshed();
}

void ChatMessageListScreen::item_event_cb(lv_event_t* e)
{
    auto* screen =
        static_cast<ChatMessageListScreen*>(lv_event_get_user_data(e));
    if (!screen) {
        return;
    }

    lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
    for (size_t i = 0; i < screen->items_.size(); i++) {
        if (screen->items_[i].btn == btn) {
            screen->setSelected(static_cast<int>(i));
            if (screen->select_cb_ != nullptr) {
                screen->select_cb_(
                    screen->items_[i].conv.channel,
                    screen->select_cb_user_data_);
            }
            break;
        }
    }
}

void ChatMessageListScreen::list_back_event_cb(lv_event_t* e)
{
    auto* screen =
        static_cast<ChatMessageListScreen*>(lv_event_get_user_data(e));
    if (!screen) {
        return;
    }
    chat::ui::message_list::input::focus_filter();
}

void ChatMessageListScreen::filter_focus_cb(lv_event_t* e)
{
    auto* screen =
        static_cast<ChatMessageListScreen*>(lv_event_get_user_data(e));
    if (!screen) {
        return;
    }
    lv_obj_t* tgt = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (tgt == screen->direct_btn_) {
        screen->setFilterMode(FilterMode::Direct);
    } else if (tgt == screen->broadcast_btn_) {
        screen->setFilterMode(FilterMode::Broadcast);
    }
}

void ChatMessageListScreen::filter_click_cb(lv_event_t* e)
{
    auto* screen =
        static_cast<ChatMessageListScreen*>(lv_event_get_user_data(e));
    if (!screen) {
        return;
    }
    lv_obj_t* tgt = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (tgt == screen->direct_btn_) {
        screen->setFilterMode(FilterMode::Direct);
    } else if (tgt == screen->broadcast_btn_) {
        screen->setFilterMode(FilterMode::Broadcast);
    }
    chat::ui::message_list::input::focus_list();
}

void ChatMessageListScreen::updateFilterHighlight()
{
    if (!direct_btn_ || !broadcast_btn_) {
        return;
    }
    lv_obj_clear_state(direct_btn_, LV_STATE_CHECKED);
    lv_obj_clear_state(broadcast_btn_, LV_STATE_CHECKED);
    if (filter_mode_ == FilterMode::Direct) {
        lv_obj_add_state(direct_btn_, LV_STATE_CHECKED);
    } else {
        lv_obj_add_state(broadcast_btn_, LV_STATE_CHECKED);
    }
}

void ChatMessageListScreen::setFilterMode(FilterMode mode)
{
    if (filter_mode_ == mode) {
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
    if (screen && screen->back_cb_ != nullptr) {
        screen->back_cb_(screen->back_cb_user_data_);
    }
}

} // namespace ui
} // namespace chat
