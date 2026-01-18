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
    time_t t = static_cast<time_t>(ts);
    struct tm* info = localtime(&t);
    if (info) {
        strftime(out, 16, "%H:%M", info);
    } else {
        snprintf(out, 16, "--:--");
    }
}

// ------------------------------------------------

ChatMessageListScreen::ChatMessageListScreen(lv_obj_t* parent)
    : container_(nullptr),
      panel_(nullptr),
      selected_index_(0),
      select_cb_(nullptr),
      select_cb_user_data_(nullptr),
      back_cb_(nullptr),
      back_cb_user_data_(nullptr)
{
    // ---------- Layout ----------
    container_ = chat::ui::layout::create_root(parent);

    // ---------- Styles ----------
    chat::ui::message_list::styles::apply_root_container(container_);

    // ---------- Top bar (existing widget, unchanged) ----------
    ::ui::widgets::top_bar_init(top_bar_, container_);
    ::ui::widgets::top_bar_set_title(top_bar_, "MESSAGES");
    ::ui::widgets::top_bar_set_right_text(top_bar_, "--:--  --%");
    ::ui::widgets::top_bar_set_back_callback(top_bar_, handle_back, this);

    // ---------- Content panel ----------
    panel_ = chat::ui::layout::create_panel(container_);
    chat::ui::message_list::styles::apply_panel(panel_);

    // ---------- Input layer (explicit, no-op for now) ----------
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
    rebuildList(convs);
}

void ChatMessageListScreen::setSelected(int index)
{
    if (index >= 0 && index < static_cast<int>(items_.size()) &&
        items_[index].btn != nullptr) {
        selected_index_ = index;
        lv_group_focus_obj(items_[index].btn);
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
void ChatMessageListScreen::rebuildList(const std::vector<chat::ConversationMeta>& convs)
{
    // Same behavior: clear and rebuild
    lv_obj_clean(panel_);
    items_.clear();

    for (const auto& conv : convs) {
        MessageItem item{};
        item.conv = conv.id;
        item.unread_count = conv.unread;

        // ----- Layout -----
        auto w = chat::ui::layout::create_message_item(panel_);
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
        lv_obj_t* placeholder = chat::ui::layout::create_placeholder(panel_);
        chat::ui::message_list::styles::apply_label_placeholder(placeholder);
        lv_label_set_text(placeholder, "No messages");
        selected_index_ = -1;
    } else {
        setSelected(0);
    }
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

void ChatMessageListScreen::handle_back(void* user_data)
{
    auto* screen = static_cast<ChatMessageListScreen*>(user_data);
    if (screen && screen->back_cb_ != nullptr) {
        screen->back_cb_(screen->back_cb_user_data_);
    }
}

} // namespace ui
} // namespace chat
