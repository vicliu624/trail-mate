#if !defined(ARDUINO_T_WATCH_S3)
/**
 * @file chat_message_list_components.cpp
 * @brief Chat message list screen implementation (explicit architecture version)
 */

#include "ui/screens/chat/chat_message_list_components.h"

#include "chat/infra/mesh_protocol_utils.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/screens/chat/chat_message_list_input.h"
#include "ui/screens/chat/chat_message_list_layout.h"
#include "ui/screens/chat/chat_message_list_styles.h"
#include "ui/ui_common.h"

#include "sys/clock.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ctime>

#ifndef CHAT_MESSAGE_LIST_LOG_ENABLE
#define CHAT_MESSAGE_LIST_LOG_ENABLE 0
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
constexpr uint32_t kMinValidEpochSeconds = 1577836800U; // 2020-01-01
constexpr uint32_t kSecondsPerDay = 24U * 60U * 60U;
constexpr uint32_t kSecondsPerMonth = 30U * kSecondsPerDay;
constexpr uint32_t kSecondsPerYear = 365U * kSecondsPerDay;

static bool is_valid_epoch_ts(uint32_t ts)
{
    return ts >= kMinValidEpochSeconds;
}
} // namespace

static void format_time_hhmm(char out[16], uint32_t ts)
{
    if (ts == 0)
    {
        snprintf(out, 16, "--:--");
        return;
    }
    if (!is_valid_epoch_ts(ts))
    {
        uint32_t now_epoch = sys::epoch_seconds_now();
        uint32_t now_secs = is_valid_epoch_ts(now_epoch) ? now_epoch : static_cast<uint32_t>(sys::millis_now() / 1000U);
        if (now_secs < ts)
        {
            now_secs = ts;
        }
        uint32_t diff = now_secs - ts;
        if (diff < 60U)
        {
            snprintf(out, 16, "now");
        }
        else if (diff < 3600U)
        {
            snprintf(out, 16, "%um", static_cast<unsigned>(diff / 60U));
        }
        else if (diff < kSecondsPerDay)
        {
            snprintf(out, 16, "%uh", static_cast<unsigned>(diff / 3600U));
        }
        else if (diff < kSecondsPerMonth)
        {
            snprintf(out, 16, "%ud", static_cast<unsigned>(diff / kSecondsPerDay));
        }
        else if (diff < kSecondsPerYear)
        {
            snprintf(out, 16, "%umo", static_cast<unsigned>(diff / kSecondsPerMonth));
        }
        else
        {
            snprintf(out, 16, "%uy", static_cast<unsigned>(diff / kSecondsPerYear));
        }
        return;
    }
    time_t t = ui_apply_timezone_offset(static_cast<time_t>(ts));
    struct tm* info = gmtime(&t);
    if (info)
    {
        strftime(out, 16, "%H:%M", info);
    }
    else
    {
        snprintf(out, 16, "--:--");
    }
}

static std::string truncate_preview(const std::string& text)
{
    static constexpr size_t kMaxPreviewBytes = 18;
    if (text.size() <= kMaxPreviewBytes)
    {
        return text;
    }
    std::string out = text.substr(0, kMaxPreviewBytes);
    out.append("...");
    return out;
}

static bool is_team_conversation(const chat::ConversationId& conv)
{
    constexpr uint8_t kTeamChatChannelRaw = 2;
    constexpr chat::ChannelId kTeamChatChannel =
        static_cast<chat::ChannelId>(kTeamChatChannelRaw);
    return conv.channel == kTeamChatChannel && conv.peer == 0;
}

static const char* protocol_short_label(chat::MeshProtocol protocol)
{
    return chat::infra::meshProtocolShortName(protocol);
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
    default:
        return "OTHER";
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
    ::ui::widgets::top_bar_set_title(top_bar_, "MESSAGES");
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

    // ---------- Filter events ----------
    if (direct_btn_)
    {
        lv_obj_add_event_cb(direct_btn_, filter_focus_cb, LV_EVENT_FOCUSED, this);
    }
    if (broadcast_btn_)
    {
        lv_obj_add_event_cb(broadcast_btn_, filter_focus_cb, LV_EVENT_FOCUSED, this);
    }
    if (team_btn_)
    {
        lv_obj_add_event_cb(team_btn_, filter_focus_cb, LV_EVENT_FOCUSED, this);
    }
    updateFilterHighlight();

    if (container_ && !lv_obj_is_valid(container_))
    {
        CHAT_MESSAGE_LIST_LOG("[ChatMessageList] WARNING: container invalid\n");
    }
    if (list_panel_ && !lv_obj_is_valid(list_panel_))
    {
        CHAT_MESSAGE_LIST_LOG("[ChatMessageList] WARNING: list_panel invalid\n");
    }

    set_default_group(prev_group);

    // ---------- Input layer ----------
    chat::ui::message_list::input::init(this, &input_controller_);
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
    }
}

void ChatMessageListScreen::setSelectedConversation(const chat::ConversationId& conv)
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }
    for (size_t i = 0; i < items_.size(); ++i)
    {
        if (items_[i].conv == conv)
        {
            setSelected(static_cast<int>(i));
            return;
        }
    }
}

chat::ConversationId ChatMessageListScreen::getSelectedConversation() const
{
    if (!guard_ || !guard_->alive)
    {
        return chat::ConversationId();
    }
    if (selected_index_ >= 0 &&
        selected_index_ < static_cast<int>(items_.size()))
    {
        return items_[selected_index_].conv;
    }
    return chat::ConversationId();
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

bool ChatMessageListScreen::activateFilterButton(lv_obj_t* button)
{
    if (!guard_ || !guard_->alive || !button)
    {
        return false;
    }
    if (button == direct_btn_)
    {
        CHAT_MESSAGE_LIST_LOG("[ChatMessageList] activateFilterButton direct button=%p\n", button);
        setFilterMode(FilterMode::Direct);
        return true;
    }
    if (button == broadcast_btn_)
    {
        CHAT_MESSAGE_LIST_LOG("[ChatMessageList] activateFilterButton broadcast button=%p\n", button);
        setFilterMode(FilterMode::Broadcast);
        return true;
    }
    if (button == team_btn_)
    {
        CHAT_MESSAGE_LIST_LOG("[ChatMessageList] activateFilterButton team button=%p\n", button);
        setFilterMode(FilterMode::Team);
        return true;
    }
    return false;
}

bool ChatMessageListScreen::activateListButton(lv_obj_t* button)
{
    if (!guard_ || !guard_->alive || !button)
    {
        return false;
    }
    for (size_t i = 0; i < items_.size(); ++i)
    {
        if (items_[i].btn == button)
        {
            CHAT_MESSAGE_LIST_LOG("[ChatMessageList] activateListButton button=%p index=%u\n",
                                  button,
                                  (unsigned)i);
            setSelected(static_cast<int>(i));
            schedule_action_async(ActionIntent::SelectConversation, items_[i].conv);
            return true;
        }
    }
    CHAT_MESSAGE_LIST_LOG("[ChatMessageList] activateListButton miss button=%p items=%u\n",
                          button,
                          (unsigned)items_.size());
    return false;
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
    // Same behavior: clear and rebuild
    lv_obj_clean(list_panel_);
    items_.clear();
    list_back_btn_ = nullptr;

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
        lv_obj_clear_state(item.btn, (lv_state_t)(LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY));

        // ----- Content -----
        std::string title = "[" + std::string(protocol_short_label(conv.id.protocol)) + "] " + conv.name;
        lv_label_set_text(item.name_label, title.c_str());
        ::ui::fonts::apply_chat_content_font(item.name_label, title.c_str());
        std::string preview = truncate_preview(conv.preview);
        lv_label_set_text(item.preview_label, preview.c_str());
        ::ui::fonts::apply_chat_content_font(item.preview_label, preview.c_str());

        char time_buf[16];
        format_time_hhmm(time_buf, conv.last_timestamp);
        lv_label_set_text(item.time_label, time_buf);
        ::ui::fonts::apply_ui_chrome_font(item.time_label);

        if (conv.unread > 0)
        {
            char unread_str[16];
            snprintf(unread_str, sizeof(unread_str), "%d", conv.unread);
            lv_label_set_text(item.unread_label, unread_str);
            ::ui::fonts::apply_ui_chrome_font(item.unread_label);
        }
        else
        {
            lv_label_set_text(item.unread_label, "");
            ::ui::fonts::apply_ui_chrome_font(item.unread_label);
        }

        // ----- Events (unchanged) -----
        lv_obj_add_event_cb(item.btn, item_focused_cb, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(item.btn, debug_touch_event_cb, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(item.btn, debug_touch_event_cb, LV_EVENT_CLICKED, this);

        items_.push_back(item);
        CHAT_MESSAGE_LIST_LOG("[ChatMessageList] rebuildList item index=%u btn=%p\n",
                              (unsigned)(items_.size() - 1),
                              item.btn);
    }

    if (items_.empty())
    {
        lv_obj_t* placeholder = chat::ui::layout::create_placeholder(list_panel_);
        chat::ui::message_list::styles::apply_label_placeholder(placeholder);
        lv_label_set_text(placeholder, "No messages");
        ::ui::fonts::apply_ui_chrome_font(placeholder);
        selected_index_ = -1;
    }

    // Append back button as the last item in list panel.
    list_back_btn_ = lv_btn_create(list_panel_);
    lv_obj_set_size(list_back_btn_, LV_PCT(100), 28);
    lv_obj_add_flag(list_back_btn_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(list_back_btn_, LV_OBJ_FLAG_SCROLLABLE);
    chat::ui::message_list::styles::apply_item_btn(list_back_btn_);
    lv_obj_add_flag(list_back_btn_, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_state(list_back_btn_, (lv_state_t)(LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY));
    lv_obj_t* back_label = lv_label_create(list_back_btn_);
    lv_obj_add_flag(back_label, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_label_set_text(back_label, "Back");
    ::ui::fonts::apply_ui_chrome_font(back_label);
    chat::ui::message_list::styles::apply_label_name(back_label);
    lv_obj_center(back_label);
    lv_obj_add_event_cb(list_back_btn_, item_focused_cb, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(list_back_btn_, debug_touch_event_cb, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(list_back_btn_, debug_touch_event_cb, LV_EVENT_CLICKED, this);
    CHAT_MESSAGE_LIST_LOG("[ChatMessageList] rebuildList back_btn=%p\n", list_back_btn_);

    if (!items_.empty())
    {
        setSelected(0);
    }

    chat::ui::message_list::input::on_ui_refreshed(&input_controller_);
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
    std::string title = "[" + std::string(protocol_short_label(conv.id.protocol)) + "] " + conv.name;
    lv_label_set_text(item.name_label, title.c_str());
    ::ui::fonts::apply_chat_content_font(item.name_label, title.c_str());

    std::string preview = truncate_preview(conv.preview);
    lv_label_set_text(item.preview_label, preview.c_str());
    ::ui::fonts::apply_chat_content_font(item.preview_label, preview.c_str());

    char time_buf[16];
    format_time_hhmm(time_buf, conv.last_timestamp);
    lv_label_set_text(item.time_label, time_buf);
    ::ui::fonts::apply_ui_chrome_font(item.time_label);

    if (conv.unread > 0)
    {
        char unread_str[16];
        snprintf(unread_str, sizeof(unread_str), "%d", conv.unread);
        lv_label_set_text(item.unread_label, unread_str);
    }
    else
    {
        lv_label_set_text(item.unread_label, "");
    }
    ::ui::fonts::apply_ui_chrome_font(item.unread_label);
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
    for (size_t i = 0; i < screen->items_.size(); i++)
    {
        if (screen->items_[i].btn == item)
        {
            screen->setSelected(static_cast<int>(i));
            screen->schedule_action_async(ActionIntent::SelectConversation,
                                          screen->items_[i].conv);
            break;
        }
    }
}

void ChatMessageListScreen::list_panel_click_cb(lv_event_t* e)
{
    auto* screen =
        static_cast<ChatMessageListScreen*>(lv_event_get_user_data(e));
    if (!screen || !screen->guard_ || !screen->guard_->alive ||
        !screen->list_panel_ || !lv_obj_is_valid(screen->list_panel_))
    {
        return;
    }

    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    while (target && target != screen->list_panel_)
    {
        if (target == screen->list_back_btn_)
        {
            screen->schedule_focus_change_async(false);
            return;
        }
        for (size_t i = 0; i < screen->items_.size(); ++i)
        {
            if (screen->items_[i].btn == target)
            {
                screen->setSelected(static_cast<int>(i));
                screen->schedule_action_async(ActionIntent::SelectConversation,
                                              screen->items_[i].conv);
                return;
            }
        }
        target = lv_obj_get_parent(target);
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
    screen->schedule_focus_change_async(false);
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
    screen->schedule_focus_change_async(true);
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

void ChatMessageListScreen::async_focus_change_cb(void* user_data)
{
    auto* payload = static_cast<FocusPayload*>(user_data);
    if (!payload)
    {
        return;
    }
    LifetimeGuard* guard = payload->guard;
    if (guard && guard->alive && payload->screen)
    {
        if (payload->focus_list)
        {
            chat::ui::message_list::input::focus_list(&payload->screen->input_controller_);
        }
        else
        {
            chat::ui::message_list::input::focus_filter(&payload->screen->input_controller_);
        }
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

    chat::ui::message_list::input::cleanup(&input_controller_);
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

void ChatMessageListScreen::schedule_focus_change_async(bool focus_list)
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }
    auto* payload = new FocusPayload();
    payload->guard = guard_;
    payload->screen = this;
    payload->focus_list = focus_list;
    guard_->pending_async++;
    lv_async_call(async_focus_change_cb, payload);
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
