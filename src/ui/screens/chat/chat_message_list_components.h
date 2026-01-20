/**
 * @file chat_message_list_components.h
 * @brief Chat message list screen (explicit architecture version)
 */

#pragma once

#include "lvgl.h"
#include "../../chat/domain/chat_types.h"
#include "../../widgets/top_bar.h"
#include <vector>
#include <string>

namespace chat {
namespace ui {

class ChatMessageListScreen {
public:
    explicit ChatMessageListScreen(lv_obj_t* parent);
    ~ChatMessageListScreen();

    // Populate list with conversations
    void setConversations(const std::vector<chat::ConversationMeta>& convs);

    // Update status text (battery) from board state
    void updateBatteryFromBoard();

    // Set back button callback
    void setBackCallback(void (*cb)(void*), void* user_data);

    // Set selected item index
    void setSelected(int index);
    void setSelectedConversation(const chat::ConversationId& conv);

    // Get selected conversation
    chat::ConversationId getSelectedConversation() const;

    // Set callback for selecting a channel (conversation)
    void setChannelSelectCallback(void (*cb)(chat::ChannelId, void*), void* user_data);

    // Get LVGL root object
    lv_obj_t* getObj() const { return container_; }
    lv_obj_t* getDirectButton() const { return direct_btn_; }
    lv_obj_t* getBroadcastButton() const { return broadcast_btn_; }
    lv_obj_t* getBackButton() const { return top_bar_.back_btn; }
    lv_obj_t* getListBackButton() const { return list_back_btn_; }
    size_t getItemCount() const { return items_.size(); }
    lv_obj_t* getItemButton(size_t index) const;
    int getSelectedIndex() const { return selected_index_; }

private:
    enum class FilterMode {
        Direct,
        Broadcast
    };

    lv_obj_t* container_ = nullptr;
    ::ui::widgets::TopBar top_bar_{};
    lv_obj_t* filter_panel_ = nullptr;
    lv_obj_t* list_panel_ = nullptr;
    lv_obj_t* direct_btn_ = nullptr;
    lv_obj_t* broadcast_btn_ = nullptr;
    lv_obj_t* list_back_btn_ = nullptr;

    int selected_index_ = 0;
    FilterMode filter_mode_ = FilterMode::Direct;

    void (*select_cb_)(chat::ChannelId, void*) = nullptr;
    void* select_cb_user_data_ = nullptr;

    void (*back_cb_)(void*) = nullptr;
    void* back_cb_user_data_ = nullptr;

    struct MessageItem {
        chat::ConversationId conv{};
        lv_obj_t* btn = nullptr;
        lv_obj_t* name_label = nullptr;
        lv_obj_t* preview_label = nullptr;
        lv_obj_t* time_label = nullptr;
        lv_obj_t* unread_label = nullptr;
        int unread_count = 0;
    };

    std::vector<MessageItem> items_;
    std::vector<chat::ConversationMeta> convs_;

    void rebuildList();
    void updateFilterHighlight();
    void setFilterMode(FilterMode mode);

    static void item_event_cb(lv_event_t* e);
    static void list_back_event_cb(lv_event_t* e);
    static void filter_focus_cb(lv_event_t* e);
    static void filter_click_cb(lv_event_t* e);
    static void handle_back(void* user_data);
};

} // namespace ui
} // namespace chat
