#pragma once

#include "../../chat/domain/chat_types.h"
#include "../../widgets/top_bar.h"
#include "lvgl.h"
#include <string>
#include <vector>

namespace chat::ui
{

class ChatMessageListScreen
{
  public:
    enum class ActionIntent
    {
        SelectConversation,
        Back
    };

    explicit ChatMessageListScreen(lv_obj_t* parent);
    ~ChatMessageListScreen();

    void setConversations(const std::vector<chat::ConversationMeta>& convs);
    void updateBatteryFromBoard();
    void setSelected(int index);
    void setSelectedConversation(const chat::ConversationId& conv);
    chat::ConversationId getSelectedConversation() const;

    void setActionCallback(void (*cb)(ActionIntent intent,
                                      const chat::ConversationId& conv,
                                      void*),
                           void* user_data);

    bool isAlive() const { return guard_ && guard_->alive; }

    lv_obj_t* getObj() const { return container_; }
    lv_obj_t* getDirectButton() const { return nullptr; }
    lv_obj_t* getBroadcastButton() const { return nullptr; }
    lv_obj_t* getTeamButton() const { return nullptr; }
    lv_obj_t* getBackButton() const { return nullptr; }
    lv_obj_t* getListBackButton() const { return nullptr; }
    size_t getItemCount() const { return list_items_.size(); }
    lv_obj_t* getItemButton(size_t index) const;
    int getSelectedIndex() const { return selected_index_; }

  private:
    struct LifetimeGuard
    {
        bool alive = false;
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

    enum class ViewMode
    {
        Menu,
        ListDirect,
        ListBroadcast
    };

    struct ListItem
    {
        chat::ConversationId conv{};
        lv_obj_t* btn = nullptr;
        lv_obj_t* label = nullptr;
        bool is_back = false;
    };

    lv_obj_t* container_ = nullptr;
    lv_obj_t* menu_panel_ = nullptr;
    lv_obj_t* direct_btn_ = nullptr;
    lv_obj_t* broadcast_btn_ = nullptr;
    lv_obj_t* back_btn_ = nullptr;
    lv_obj_t* direct_label_ = nullptr;
    lv_obj_t* broadcast_label_ = nullptr;
    lv_obj_t* back_label_ = nullptr;

    int selected_index_ = 0;
    std::vector<chat::ConversationMeta> convs_;
    chat::ConversationId direct_conv_{};
    chat::ConversationId broadcast_conv_{};
    int direct_unread_ = 0;
    int broadcast_unread_ = 0;
    bool has_direct_ = false;
    bool has_broadcast_ = false;
    ViewMode view_mode_ = ViewMode::Menu;
    std::vector<ListItem> list_items_;

    LifetimeGuard* guard_ = nullptr;

    void (*action_cb_)(ActionIntent intent,
                       const chat::ConversationId& conv,
                       void*) = nullptr;
    void* action_cb_user_data_ = nullptr;

    void showMenu();
    void showList(ViewMode mode);
    void rebuildMenu();
    void rebuildList();
    void refreshTargets();
    void schedule_action_async(ActionIntent intent, const chat::ConversationId& conv);

    static void menu_event_cb(lv_event_t* e);
    static void async_action_cb(void* user_data);
    static void handle_back(void* user_data);
};

} // namespace chat::ui
