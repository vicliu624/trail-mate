/**
 * @file chat_message_list_components.h
 * @brief Chat message list screen (explicit architecture version)
 */

#pragma once

#include "lvgl.h"
#include "../../chat/domain/chat_types.h"
#include "../../widgets/top_bar.h"
#include "chat_message_list_input.h"
#include <vector>
#include <string>

namespace chat {
namespace ui {

class ChatMessageListScreen {
public:
    enum class ActionIntent {
        SelectConversation,
        Back
    };

    explicit ChatMessageListScreen(lv_obj_t* parent);
    ~ChatMessageListScreen();

    // Populate list with conversations
    void setConversations(const std::vector<chat::ConversationMeta>& convs);

    // Update status text (battery) from board state
    void updateBatteryFromBoard();

    // Set selected item index
    void setSelected(int index);
    void setSelectedConversation(const chat::ConversationId& conv);

    // Get selected conversation
    chat::ConversationId getSelectedConversation() const;

    // Set callback for user actions (select/back)
    void setActionCallback(void (*cb)(ActionIntent intent,
                                      const chat::ConversationId& conv,
                                      void*),
                           void* user_data);

    bool isAlive() const { return guard_ && guard_->alive; }

    // Get LVGL root object
    lv_obj_t* getObj() const { return container_; }
    lv_obj_t* getDirectButton() const { return direct_btn_; }
    lv_obj_t* getBroadcastButton() const { return broadcast_btn_; }
    lv_obj_t* getTeamButton() const { return team_btn_; }
    lv_obj_t* getBackButton() const { return top_bar_.back_btn; }
    lv_obj_t* getListBackButton() const { return list_back_btn_; }
    size_t getItemCount() const { return items_.size(); }
    lv_obj_t* getItemButton(size_t index) const;
    int getSelectedIndex() const { return selected_index_; }

private:
    enum class TimerDomain {
        ScreenGeneral,
        Input
    };

    struct TimerEntry {
        lv_timer_t* timer = nullptr;
        TimerDomain domain = TimerDomain::ScreenGeneral;
    };

    struct LifetimeGuard {
        bool alive = false;
        int pending_async = 0;
        bool owner_dead = false;
    };

    struct ActionPayload {
        LifetimeGuard* guard = nullptr;
        void (*action_cb)(ActionIntent intent,
                          const chat::ConversationId& conv,
                          void*) = nullptr;
        void* user_data = nullptr;
        ActionIntent intent = ActionIntent::SelectConversation;
        chat::ConversationId conv{};
    };

    enum class FilterMode {
        Direct,
        Broadcast,
        Team
    };

    lv_obj_t* container_ = nullptr;
    ::ui::widgets::TopBar top_bar_{};
    lv_obj_t* filter_panel_ = nullptr;
    lv_obj_t* list_panel_ = nullptr;
    lv_obj_t* direct_btn_ = nullptr;
    lv_obj_t* broadcast_btn_ = nullptr;
    lv_obj_t* team_btn_ = nullptr;
    lv_obj_t* list_back_btn_ = nullptr;

    int selected_index_ = 0;
    FilterMode filter_mode_ = FilterMode::Direct;

    void (*action_cb_)(ActionIntent intent,
                       const chat::ConversationId& conv,
                       void*) = nullptr;
    void* action_cb_user_data_ = nullptr;

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

    LifetimeGuard* guard_ = nullptr;
    std::vector<TimerEntry> timers_;
    message_list::input::Binding input_binding_{};

    void rebuildList();
    void updateFilterHighlight();
    void setFilterMode(FilterMode mode);

    static void item_event_cb(lv_event_t* e);
    static void list_back_event_cb(lv_event_t* e);
    static void filter_focus_cb(lv_event_t* e);
    static void filter_click_cb(lv_event_t* e);
    static void async_action_cb(void* user_data);
    static void on_root_deleted(lv_event_t* e);
    static void handle_back(void* user_data);

    void handle_root_deleted();
    void schedule_action_async(ActionIntent intent, const chat::ConversationId& conv);
    lv_timer_t* add_timer(lv_timer_cb_t cb, uint32_t period_ms, void* user_data, TimerDomain domain);
    void clear_timers(TimerDomain domain);
    void clear_all_timers();
};

} // namespace ui
} // namespace chat
