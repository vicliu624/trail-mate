/**
 * @file chat_message_list_components.h
 * @brief Chat message list screen (explicit architecture version)
 */

#pragma once

#if defined(ARDUINO_T_WATCH_S3)
#include "../chat_watch/chat_message_list_components_watch.h"
#else

#include "chat/domain/chat_types.h"
#include "chat_message_list_input.h"
#include "lvgl.h"
#include "ui/components/floating_search_box.h"
#include "ui/widgets/top_bar.h"
#include <string>
#include <vector>

namespace chat
{
namespace ui
{

class ChatMessageListScreen
{
  public:
    enum class ActionIntent
    {
        SelectConversation,
        ShowInfo,
        PingDestination,
        DeleteConversation,
        Back
    };

    explicit ChatMessageListScreen(lv_obj_t* parent);
    ~ChatMessageListScreen();

    void setConversations(const std::vector<chat::ConversationMeta>& convs);
    // A false-ready chat snapshot is not an empty inbox. Keep that transient
    // projection distinct from a successfully loaded list with zero rows.
    void setDataLoading(bool loading);
    void updateBatteryFromBoard();

    void setSelected(int index);
    void setSelectedConversation(const chat::ConversationId& conv);
    bool tryGetSelectedConversation(chat::ConversationId* conv) const;
    chat::ConversationId getSelectedConversation() const;

    void setActionCallback(void (*cb)(ActionIntent intent,
                                      const chat::ConversationId& conv,
                                      void*),
                           void* user_data);

    bool isAlive() const { return guard_ && guard_->alive; }

    lv_obj_t* getObj() const { return container_; }
    lv_obj_t* getDirectButton() const { return direct_btn_; }
    lv_obj_t* getBroadcastButton() const { return broadcast_btn_; }
    lv_obj_t* getTeamButton() const { return team_btn_; }
    lv_obj_t* getBackButton() const { return top_bar_.back_btn; }
    lv_obj_t* getListBackButton() const { return list_back_btn_; }
    size_t getItemCount() const { return items_.size(); }
    lv_obj_t* getItemButton(size_t index) const;
    int getSelectedIndex() const { return selected_index_; }
    bool isFilterPanelVisible() const { return filter_panel_visible_; }
    bool openSelectedActionMenu();

  private:
    enum class TimerDomain
    {
        ScreenGeneral,
        Input
    };

    struct TimerEntry
    {
        lv_timer_t* timer = nullptr;
        TimerDomain domain = TimerDomain::ScreenGeneral;
    };

    struct LifetimeGuard
    {
        bool alive = false;
        int pending_async = 0;
        bool owner_dead = false;
    };

    struct ActionPayload
    {
        LifetimeGuard* guard = nullptr;
        void (*action_cb)(ActionIntent intent,
                          const chat::ConversationId& conv,
                          void*) = nullptr;
        void* user_data = nullptr;
        ActionIntent intent = ActionIntent::SelectConversation;
        chat::ConversationId conv{};
    };

    enum class FilterMode
    {
        Direct,
        Broadcast,
        Team,
        All
    };

    enum class ModalCommand
    {
        Chat,
        Info,
        PingDestination,
        Delete,
        Cancel
    };

    struct ModalButtonContext
    {
        ChatMessageListScreen* screen = nullptr;
        ModalCommand command = ModalCommand::Cancel;
    };

    lv_obj_t* container_ = nullptr;
    ::ui::widgets::TopBar top_bar_{};
    lv_obj_t* filter_panel_ = nullptr;
    lv_obj_t* list_panel_ = nullptr;
    lv_obj_t* direct_btn_ = nullptr;
    lv_obj_t* broadcast_btn_ = nullptr;
    lv_obj_t* team_btn_ = nullptr;
    lv_obj_t* list_back_btn_ = nullptr;
    lv_obj_t* action_menu_modal_ = nullptr;
    lv_obj_t* delete_confirm_modal_ = nullptr;
    lv_group_t* modal_group_ = nullptr;
    lv_group_t* modal_prev_group_ = nullptr;
    chat::ConversationId modal_conv_{};
    ModalButtonContext modal_button_contexts_[5]{};

    int selected_index_ = -1;
    FilterMode filter_mode_ =
#if defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
        FilterMode::All;
#else
        FilterMode::Direct;
#endif
    bool filter_panel_visible_ = true;
    bool data_loading_ = false;
    char search_query_[32] = {};
    ::ui::components::floating_search_box::State search_box_{};

    void (*action_cb_)(ActionIntent intent,
                       const chat::ConversationId& conv,
                       void*) = nullptr;
    void* action_cb_user_data_ = nullptr;

    struct MessageItem
    {
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
    message_list::input::Controller input_controller_{};

    void rebuildList();
    bool updateListInPlace(const std::vector<chat::ConversationMeta>& convs);
    void updateListItem(size_t index, const chat::ConversationMeta& conv);
    void updateFilterHighlight();
    void setFilterMode(FilterMode mode);
    bool searchActive() const;
    bool conversationMatchesSearch(const chat::ConversationMeta& conv) const;
    void buildFilteredConversations(std::vector<chat::ConversationMeta>& out) const;
    void openSearchModal();
    void toggleFilterPanel();
    void applyFilterPanelVisibility();

    static void item_event_cb(lv_event_t* e);
    static void list_back_event_cb(lv_event_t* e);
    static void item_focused_cb(lv_event_t* e);
    static void filter_click_cb(lv_event_t* e);
    static void page_shortcut_cb(lv_event_t* e);
    static void search_apply_cb(const char* text, void* user_data);
    static void search_clear_cb(void* user_data);
    static void search_cancel_cb(void* user_data);
    static void action_menu_button_cb(lv_event_t* e);
    static void action_menu_key_cb(lv_event_t* e);
    static void delete_confirm_cb(lv_event_t* e);
    static void delete_cancel_cb(lv_event_t* e);
    static void modal_bg_key_cb(lv_event_t* e);
    static void async_action_cb(void* user_data);
    static void on_root_deleted(lv_event_t* e);
    static void handle_back(void* user_data);

    void handle_root_deleted();
    void openActionMenu(const chat::ConversationId& conv);
    void openDeleteConfirm(const chat::ConversationId& conv);
    void closeActionMenu();
    void closeDeleteConfirm();
    void closeModal(lv_obj_t*& modal);
    void prepareModalGroup();
    void restoreModalGroup();
    bool isModalOpen() const;
    void schedule_action_async(ActionIntent intent, const chat::ConversationId& conv);
    lv_timer_t* add_timer(lv_timer_cb_t cb, uint32_t period_ms, void* user_data, TimerDomain domain);
    void clear_timers(TimerDomain domain);
    void clear_all_timers();
};

} // namespace ui
} // namespace chat

#endif
