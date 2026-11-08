#if !defined(ARDUINO_T_WATCH_S3)
/**
 * @file chat_message_list_components.cpp
 * @brief Chat message list screen implementation (explicit architecture version)
 */

#include "ui/screens/chat/chat_message_list_components.h"

#include "ui/app_runtime.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/components/info_card.h"
#include "ui/components/two_pane_styles.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/screens/chat/chat_message_list_input.h"
#include "ui/screens/chat/chat_message_list_layout.h"
#include "ui/screens/chat/chat_message_list_styles.h"
#include "ui/screens/chat/chat_protocol_support.h"
#include "ui/ui_common.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>

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
static bool show_second_column_back()
{
    return !::ui::components::info_card::use_tdeck_layout();
}

static bool use_group_navigation()
{
    // The shared controller routes both pointer and hardware input. All targets
    // must create the bottom menu, including touchpad-only products.
    return true;
}
} // namespace

static bool is_team_conversation(const chat::ConversationId& conv)
{
    constexpr uint8_t kTeamChatChannelRaw = static_cast<uint8_t>(chat::ChannelId::TEAM);
    constexpr chat::ChannelId kTeamChatChannel =
        static_cast<chat::ChannelId>(kTeamChatChannelRaw);
    return conv.channel == kTeamChatChannel && conv.peer == 0;
}

static bool has_reticulum_destination(const chat::ConversationId& conv)
{
    return conv.protocol == chat::MeshProtocol::Reticulum &&
           chat::hasReticulumDestinationIdentity(conv.reticulum_identity);
}

static bool is_direct_conversation(const chat::ConversationMeta& conv)
{
    return !is_team_conversation(conv.id) &&
           (conv.id.peer != 0 || has_reticulum_destination(conv.id));
}

static bool is_channel_conversation(const chat::ConversationMeta& conv)
{
    return !is_team_conversation(conv.id) &&
           conv.id.peer == 0 && !has_reticulum_destination(conv.id);
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

static bool contains_ci(const char* text, const char* query)
{
    if (!query || query[0] == '\0')
    {
        return true;
    }
    if (!text)
    {
        return false;
    }
    const std::size_t query_len = std::strlen(query);
    const std::size_t text_len = std::strlen(text);
    if (query_len == 0)
    {
        return true;
    }
    if (query_len > text_len)
    {
        return false;
    }
    for (std::size_t start = 0; start + query_len <= text_len; ++start)
    {
        bool match = true;
        for (std::size_t offset = 0; offset < query_len; ++offset)
        {
            const auto lhs = static_cast<unsigned char>(text[start + offset]);
            const auto rhs = static_cast<unsigned char>(query[offset]);
            if (std::tolower(lhs) != std::tolower(rhs))
            {
                match = false;
                break;
            }
        }
        if (match)
        {
            return true;
        }
    }
    return false;
}

static const chat::ReticulumPeerIdentity& conversation_reticulum_identity(
    const chat::ConversationMeta& conv)
{
    if (chat::hasReticulumDestinationIdentity(conv.reticulum_identity))
    {
        return conv.reticulum_identity;
    }
    return conv.id.reticulum_identity;
}

static chat::ConversationId conversation_id_for_item(
    const chat::ConversationMeta& conv)
{
    chat::ConversationId id = conv.id;
    if (!chat::hasReticulumDestinationIdentity(id.reticulum_identity) &&
        chat::hasReticulumDestinationIdentity(conv.reticulum_identity))
    {
        id.reticulum_identity = conv.reticulum_identity;
    }
    return id;
}

static bool conversation_supports_reticulum_ping(const chat::ConversationId& conv)
{
    return conv.protocol == chat::MeshProtocol::Reticulum &&
           support::active_mesh_protocol() == chat::MeshProtocol::Reticulum &&
           support::supports_reticulum_destination_ping() &&
           chat::hasReticulumDestinationIdentity(conv.reticulum_identity);
}

static void format_hash_text(const uint8_t* hash, char* out, size_t out_len)
{
    if (!hash || !out || out_len == 0)
    {
        return;
    }

    size_t used = 0;
    for (size_t index = 0;
         index < chat::kReticulumPeerHashSize && used + 2U < out_len;
         ++index)
    {
        const int written = std::snprintf(out + used,
                                          out_len - used,
                                          "%02X",
                                          static_cast<unsigned>(hash[index]));
        if (written != 2)
        {
            break;
        }
        used += 2U;
    }
    out[used < out_len ? used : out_len - 1U] = '\0';
}

static bool is_search_shortcut_key(uint32_t key)
{
    return key == '/' || key == 's' || key == 'S';
}

static bool is_filter_toggle_shortcut_key(uint32_t key)
{
    return key == 'f' || key == 'F';
}

static lv_style_selector_t selector_for_state(lv_state_t state)
{
    return static_cast<lv_style_selector_t>(LV_PART_MAIN | state);
}

static uint32_t color_panel_bg()
{
    return ::ui::components::two_pane_styles::kMainPanelBg;
}

static uint32_t color_line()
{
    return ::ui::components::two_pane_styles::kBorder;
}

static uint32_t color_accent()
{
    return ::ui::components::two_pane_styles::kAccent;
}

static uint32_t color_text()
{
    return ::ui::components::two_pane_styles::kTextPrimary;
}

static lv_coord_t action_button_height()
{
    return ::ui::page_profile::resolve_control_button_height();
}

static void apply_modal_label(lv_obj_t* label)
{
    if (!label)
    {
        return;
    }
    chat::ui::message_list::styles::apply_label_name(label);
    lv_obj_set_style_text_color(label, lv_color_hex(color_text()), LV_PART_MAIN);
}

static lv_obj_t* create_modal_root(lv_obj_t* parent, int width, int height)
{
    lv_obj_t* root_parent = parent ? parent : lv_screen_active();
    if (!root_parent)
    {
        return nullptr;
    }

    lv_coord_t screen_w = lv_obj_get_width(root_parent);
    lv_coord_t screen_h = lv_obj_get_height(root_parent);
    if (screen_w <= 0 || screen_h <= 0)
    {
        root_parent = lv_screen_active();
        screen_w = root_parent ? lv_obj_get_width(root_parent) : 0;
        screen_h = root_parent ? lv_obj_get_height(root_parent) : 0;
    }

    lv_obj_t* bg = lv_obj_create(root_parent);
    lv_obj_set_size(bg, screen_w, screen_h);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_style_bg_color(bg, lv_color_hex(color_text()), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bg, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(bg, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bg, 0, LV_PART_MAIN);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_move_foreground(bg);

    const auto resolved = ::ui::page_profile::resolve_modal_size(width, height);
    lv_obj_t* win = lv_obj_create(bg);
    lv_obj_set_size(win, resolved.width, resolved.height);
    lv_obj_center(win);
    lv_obj_set_style_bg_color(win, lv_color_hex(color_panel_bg()), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(win, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(win, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(win, lv_color_hex(color_line()), LV_PART_MAIN);
    lv_obj_set_style_radius(win, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(win, ::ui::page_profile::resolve_modal_pad(), LV_PART_MAIN);
    lv_obj_clear_flag(win, LV_OBJ_FLAG_SCROLLABLE);

    return bg;
}

static lv_obj_t* create_action_button(lv_obj_t* parent, const char* text)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, LV_PCT(100), action_button_height());
    ::ui::components::two_pane_styles::apply_btn_basic(btn);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color_panel_bg()), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color_accent()), selector_for_state(LV_STATE_FOCUSED));
    lv_obj_set_style_bg_color(btn, lv_color_hex(color_accent()), selector_for_state(LV_STATE_FOCUS_KEY));
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xC98118), selector_for_state(LV_STATE_PRESSED));
    lv_obj_set_style_border_color(btn, lv_color_hex(color_line()), LV_PART_MAIN);

    lv_obj_t* label = lv_label_create(btn);
    ::ui::i18n::set_label_text(label, text);
    apply_modal_label(label);
    lv_obj_center(label);
    return btn;
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

// ------------------------------------------------

ChatMessageListScreen::ChatMessageListScreen(lv_obj_t* parent)
    : container_(nullptr),
      filter_panel_(nullptr),
      list_panel_(nullptr),
      direct_btn_(nullptr),
      broadcast_btn_(nullptr),
      list_back_btn_(nullptr),
      selected_index_(-1),
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
#if defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
    if (direct_btn_)
        ::ui::i18n::set_label_text(lv_obj_get_child(direct_btn_, 0), "All");
#endif

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
    ::ui::widgets::top_bar_set_back_callback(top_bar_, handle_back, this);
    if (top_bar_.container)
    {
        lv_obj_move_to_index(top_bar_.container, 0);
    }

    if (container_)
    {
        lv_obj_add_event_cb(container_, on_root_deleted, LV_EVENT_DELETE, this);
        lv_obj_add_event_cb(container_, page_shortcut_cb, LV_EVENT_KEY, this);
        if (list_panel_)
        {
            lv_obj_add_event_cb(list_panel_, page_shortcut_cb, LV_EVENT_KEY, this);
        }
    }
    disable_touch_click_focus_recursive(container_);

    // ---------- Filter events ----------
    if (direct_btn_)
    {
        lv_obj_add_event_cb(direct_btn_, filter_click_cb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(direct_btn_, page_shortcut_cb, LV_EVENT_KEY, this);
    }
    if (broadcast_btn_)
    {
        lv_obj_add_event_cb(broadcast_btn_, filter_click_cb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(broadcast_btn_, page_shortcut_cb, LV_EVENT_KEY, this);
    }
    if (team_btn_)
    {
        lv_obj_add_event_cb(team_btn_, filter_click_cb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(team_btn_, page_shortcut_cb, LV_EVENT_KEY, this);
    }
    updateFilterHighlight();
    applyFilterPanelVisibility();
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
    // ---------- Input layer ----------
    if (use_group_navigation())
    {
        chat::ui::message_list::input::init(this, &input_controller_);
    }
    CHAT_MESSAGE_LIST_LOG("[ChatMessageList] ctor input_group=%p\n", input_controller_.group());
}

ChatMessageListScreen::~ChatMessageListScreen()
{
    ::ui::components::floating_search_box::close(search_box_);
    closeActionMenu();
    closeDeleteConfirm();
    restoreModalGroup();
    if (modal_group_)
    {
        lv_group_del(modal_group_);
        modal_group_ = nullptr;
    }
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

void ChatMessageListScreen::setDataLoading(const bool loading)
{
    if (!guard_ || !guard_->alive || data_loading_ == loading)
    {
        return;
    }

    data_loading_ = loading;
    rebuildList();
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

bool ChatMessageListScreen::openSelectedActionMenu()
{
    if (!guard_ || !guard_->alive)
    {
        return false;
    }

    chat::ConversationId conv{};
    if (!tryGetSelectedConversation(&conv))
    {
        return false;
    }
    openActionMenu(conv);
    return true;
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

bool ChatMessageListScreen::searchActive() const
{
    return search_query_[0] != '\0';
}

bool ChatMessageListScreen::conversationMatchesSearch(
    const chat::ConversationMeta& conv) const
{
    if (!searchActive())
    {
        return true;
    }

    char peer_hex[16] = {};
    std::snprintf(peer_hex,
                  sizeof(peer_hex),
                  "%08lX",
                  static_cast<unsigned long>(conv.id.peer));
    char destination_hash[chat::kReticulumPeerHashSize * 2U + 1U] = {};
    char identity_hash[chat::kReticulumPeerHashSize * 2U + 1U] = {};
    const chat::ReticulumPeerIdentity& identity =
        conversation_reticulum_identity(conv);
    if (chat::hasReticulumDestinationIdentity(identity))
    {
        format_hash_text(identity.destination_hash,
                         destination_hash,
                         sizeof(destination_hash));
        format_hash_text(identity.identity_hash,
                         identity_hash,
                         sizeof(identity_hash));
    }
    return contains_ci(conv.name.c_str(), search_query_) ||
           contains_ci(peer_hex, search_query_) ||
           contains_ci(destination_hash, search_query_) ||
           contains_ci(identity_hash, search_query_);
}

void ChatMessageListScreen::buildFilteredConversations(
    std::vector<chat::ConversationMeta>& out) const
{
    out.clear();
    out.reserve(convs_.size());
    for (const auto& conv : convs_)
    {
        bool mode_match = false;
        if (filter_mode_ == FilterMode::All)
        {
            mode_match = true;
        }
        else if (is_team_conversation(conv.id))
        {
            mode_match = filter_mode_ == FilterMode::Team;
        }
        else if (filter_mode_ == FilterMode::Direct &&
                 is_direct_conversation(conv))
        {
            mode_match = true;
        }
        else if (filter_mode_ == FilterMode::Broadcast &&
                 is_channel_conversation(conv))
        {
            mode_match = true;
        }

        if (mode_match && conversationMatchesSearch(conv))
        {
            out.push_back(conv);
        }
    }
    std::stable_sort(out.begin(),
                     out.end(),
                     [](const chat::ConversationMeta& lhs,
                        const chat::ConversationMeta& rhs)
                     {
                         const bool lhs_unread = lhs.unread > 0;
                         const bool rhs_unread = rhs.unread > 0;
                         if (lhs_unread != rhs_unread)
                         {
                             return lhs_unread;
                         }
                         return lhs.last_timestamp > rhs.last_timestamp;
                     });
}

void ChatMessageListScreen::applyFilterPanelVisibility()
{
    if (!filter_panel_)
    {
        return;
    }
    if (lv_obj_has_flag(filter_panel_, LV_OBJ_FLAG_IGNORE_LAYOUT))
    {
        chat::ui::message_list::input::on_ui_refreshed(&input_controller_);
        return;
    }
    if (filter_panel_visible_)
    {
        lv_obj_clear_flag(filter_panel_, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(filter_panel_, LV_OBJ_FLAG_HIDDEN);
    }
    if (use_group_navigation())
    {
        chat::ui::message_list::input::on_ui_refreshed(&input_controller_);
    }
}

void ChatMessageListScreen::toggleFilterPanel()
{
    if (!guard_ || !guard_->alive)
    {
        return;
    }
    if (use_group_navigation())
    {
        if (filter_panel_ && lv_obj_has_flag(filter_panel_, LV_OBJ_FLAG_HIDDEN))
            chat::ui::message_list::input::focus_filter(&input_controller_);
        else chat::ui::message_list::input::focus_list(&input_controller_);
    }
}

void ChatMessageListScreen::search_apply_cb(const char* text, void* user_data)
{
    auto* screen = static_cast<ChatMessageListScreen*>(user_data);
    if (!screen || !screen->guard_ || !screen->guard_->alive)
    {
        return;
    }
    std::snprintf(screen->search_query_,
                  sizeof(screen->search_query_),
                  "%s",
                  text ? text : "");
    screen->selected_index_ = -1;
    screen->rebuildList();
    if (use_group_navigation())
    {
        chat::ui::message_list::input::focus_list(&screen->input_controller_);
    }
}

void ChatMessageListScreen::search_clear_cb(void* user_data)
{
    auto* screen = static_cast<ChatMessageListScreen*>(user_data);
    if (!screen || !screen->guard_ || !screen->guard_->alive)
    {
        return;
    }
    screen->search_query_[0] = '\0';
    screen->selected_index_ = -1;
    screen->rebuildList();
    if (use_group_navigation())
    {
        chat::ui::message_list::input::focus_list(&screen->input_controller_);
    }
}

void ChatMessageListScreen::search_cancel_cb(void* user_data)
{
    auto* screen = static_cast<ChatMessageListScreen*>(user_data);
    if (!screen || !screen->guard_ || !screen->guard_->alive)
    {
        return;
    }
    if (use_group_navigation())
    {
        chat::ui::message_list::input::focus_list(&screen->input_controller_);
    }
}

void ChatMessageListScreen::openSearchModal()
{
    if (::ui::components::floating_search_box::is_open(search_box_))
    {
        ::ui::components::floating_search_box::focus(search_box_);
        return;
    }

    ::ui::components::floating_search_box::Config config{};
    config.title = "Search chats";
    config.initial_text = search_query_;
    config.max_length = sizeof(search_query_) - 1U;
    config.restore_group = input_controller_.group();
    config.callbacks.apply = search_apply_cb;
    config.callbacks.clear = search_clear_cb;
    config.callbacks.cancel = search_cancel_cb;
    config.callbacks.user_data = this;
    (void)::ui::components::floating_search_box::open(
        search_box_,
        container_ ? container_ : lv_screen_active(),
        config);
}

void ChatMessageListScreen::prepareModalGroup()
{
    if (!modal_group_)
    {
        modal_group_ = lv_group_create();
    }
    lv_group_remove_all_objs(modal_group_);
    modal_prev_group_ = lv_group_get_default();
    if (!modal_prev_group_)
    {
        modal_prev_group_ = input_controller_.group();
    }
    set_default_group(modal_group_);
}

void ChatMessageListScreen::restoreModalGroup()
{
    lv_group_t* restore = modal_prev_group_;
    if (!restore)
    {
        restore = input_controller_.group();
    }
    if (restore)
    {
        set_default_group(restore);
    }
    modal_prev_group_ = nullptr;
    if (use_group_navigation())
    {
        chat::ui::message_list::input::on_ui_refreshed(&input_controller_);
        chat::ui::message_list::input::focus_list(&input_controller_);
    }
}

void ChatMessageListScreen::closeModal(lv_obj_t*& modal)
{
    if (!modal)
    {
        return;
    }
    lv_obj_del(modal);
    modal = nullptr;
    restoreModalGroup();
}

void ChatMessageListScreen::closeActionMenu()
{
    closeModal(action_menu_modal_);
}

void ChatMessageListScreen::closeDeleteConfirm()
{
    closeModal(delete_confirm_modal_);
}

bool ChatMessageListScreen::isModalOpen() const
{
    return action_menu_modal_ != nullptr || delete_confirm_modal_ != nullptr;
}

void ChatMessageListScreen::openActionMenu(const chat::ConversationId& conv)
{
    if (!guard_ || !guard_->alive || isModalOpen())
    {
        return;
    }

    modal_conv_ = conv;
    prepareModalGroup();
    const int row_gap = ::ui::page_profile::current().large_touch_hitbox ? 8 : 4;
    const bool allow_ping = conversation_supports_reticulum_ping(conv);
    const int action_count = allow_ping ? 5 : 4;
    const int modal_h = 58 + action_count * (action_button_height() + row_gap);
    action_menu_modal_ = create_modal_root(container_, 190, modal_h);
    lv_obj_t* win = action_menu_modal_ ? lv_obj_get_child(action_menu_modal_, 0) : nullptr;
    if (!win)
    {
        closeActionMenu();
        return;
    }
    lv_obj_add_event_cb(action_menu_modal_, modal_bg_key_cb, LV_EVENT_KEY, this);
    lv_obj_set_flex_flow(win, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(win,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(win, row_gap, LV_PART_MAIN);

    std::string title = ::ui::i18n::tr("Conversation");
    for (const auto& conv_meta : convs_)
    {
        if (conv_meta.id == conv && !conv_meta.name.empty())
        {
            title = conv_meta.name;
            break;
        }
    }

    lv_obj_t* title_label = lv_label_create(win);
    ::ui::i18n::set_label_text_raw(title_label, title.c_str());
    apply_modal_label(title_label);
    lv_obj_set_width(title_label, LV_PCT(100));
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t* list = lv_obj_create(win);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_height(list, 0);
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, 2, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    auto add_action = [&](ModalCommand command, const char* text)
    {
        lv_obj_t* btn = create_action_button(list, text);
        const size_t context_index = static_cast<size_t>(command);
        modal_button_contexts_[context_index].screen = this;
        modal_button_contexts_[context_index].command = command;
        lv_obj_add_event_cb(
            btn,
            action_menu_button_cb,
            LV_EVENT_CLICKED,
            &modal_button_contexts_[context_index]);
        lv_obj_add_event_cb(btn, action_menu_key_cb, LV_EVENT_KEY, this);
        lv_group_add_obj(modal_group_, btn);
        return btn;
    };

    lv_obj_t* first = add_action(ModalCommand::Chat, "Chat");
    add_action(ModalCommand::Info, "Info");
    if (allow_ping)
    {
        add_action(ModalCommand::PingDestination, "Ping");
    }
    add_action(ModalCommand::Delete, "Delete");
    add_action(ModalCommand::Cancel, "Cancel");
    if (first)
    {
        lv_group_focus_obj(first);
    }
}

void ChatMessageListScreen::openDeleteConfirm(const chat::ConversationId& conv)
{
    if (!guard_ || !guard_->alive || delete_confirm_modal_)
    {
        return;
    }

    modal_conv_ = conv;
    closeActionMenu();
    prepareModalGroup();
    delete_confirm_modal_ = create_modal_root(container_, 260, 138);
    lv_obj_t* win = delete_confirm_modal_ ? lv_obj_get_child(delete_confirm_modal_, 0) : nullptr;
    if (!win)
    {
        closeDeleteConfirm();
        return;
    }
    lv_obj_add_event_cb(delete_confirm_modal_, modal_bg_key_cb, LV_EVENT_KEY, this);

    lv_obj_t* label = lv_label_create(win);
    apply_modal_label(label);
    ::ui::i18n::set_label_text(label, "Delete this chat?");
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t* btn_row = lv_obj_create(win);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* confirm_btn = create_action_button(btn_row, "Delete");
    lv_obj_set_width(confirm_btn, ::ui::page_profile::resolve_compact_button_min_width());
    lv_obj_add_event_cb(confirm_btn, delete_confirm_cb, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(confirm_btn, action_menu_key_cb, LV_EVENT_KEY, this);

    lv_obj_t* cancel_btn = create_action_button(btn_row, "Cancel");
    lv_obj_set_width(cancel_btn, ::ui::page_profile::resolve_compact_button_min_width());
    lv_obj_add_event_cb(cancel_btn, delete_cancel_cb, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(cancel_btn, action_menu_key_cb, LV_EVENT_KEY, this);

    lv_group_add_obj(modal_group_, confirm_btn);
    lv_group_add_obj(modal_group_, cancel_btn);
    lv_group_focus_obj(cancel_btn);
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
    // Same behavior: clear and rebuild
    lv_obj_clean(list_panel_);
    items_.clear();
    list_back_btn_ = nullptr;
    selected_index_ = -1;

    std::vector<chat::ConversationMeta> filtered;
    buildFilteredConversations(filtered);

    for (const auto& conv : filtered)
    {
        MessageItem item{};
        item.conv = conversation_id_for_item(conv);
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
        lv_obj_add_event_cb(item.btn, page_shortcut_cb, LV_EVENT_KEY, this);
        if (use_group_navigation())
        {
            lv_obj_add_event_cb(item.btn, item_focused_cb, LV_EVENT_FOCUSED, this);
        }
        items_.push_back(item);
        CHAT_MESSAGE_LIST_LOG("[ChatMessageList] rebuildList item index=%u btn=%p\n",
                              (unsigned)(items_.size() - 1),
                              item.btn);
    }

    if (items_.empty())
    {
        lv_obj_t* placeholder = chat::ui::layout::create_placeholder(list_panel_);
        chat::ui::message_list::styles::apply_label_placeholder(placeholder);
        const char* placeholder_text = searchActive()
                                           ? "No matches"
                                           : (data_loading_ ? "Loading messages..." : "No messages");
        ::ui::i18n::set_label_text(placeholder, placeholder_text);
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
        lv_obj_add_event_cb(list_back_btn_, page_shortcut_cb, LV_EVENT_KEY, this);
        if (use_group_navigation())
        {
            lv_obj_add_event_cb(list_back_btn_, item_focused_cb, LV_EVENT_FOCUSED, this);
        }
        CHAT_MESSAGE_LIST_LOG("[ChatMessageList] rebuildList back_btn=%p\n", list_back_btn_);
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
    buildFilteredConversations(filtered);

    if (filtered.size() != items_.size())
    {
        return false;
    }

    for (size_t index = 0; index < filtered.size(); ++index)
    {
        if (!(items_[index].conv == conversation_id_for_item(filtered[index])))
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
    item.conv = conversation_id_for_item(conv);
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
            screen->openActionMenu(screen->items_[i].conv);
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
        screen->setFilterMode(
#if defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
            FilterMode::All
#else
            FilterMode::Direct
#endif
        );
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
    if (use_group_navigation() && !event_input_is_pointer(e))
    {
        chat::ui::message_list::input::focus_list(&screen->input_controller_);
    }
}

void ChatMessageListScreen::action_menu_button_cb(lv_event_t* e)
{
    auto* ctx = static_cast<ModalButtonContext*>(lv_event_get_user_data(e));
    ChatMessageListScreen* screen = ctx ? ctx->screen : nullptr;
    if (!screen || !screen->guard_ || !screen->guard_->alive)
    {
        return;
    }

    const chat::ConversationId conv = screen->modal_conv_;
    switch (ctx->command)
    {
    case ModalCommand::Chat:
        screen->closeActionMenu();
        screen->schedule_action_async(ActionIntent::SelectConversation, conv);
        break;
    case ModalCommand::Info:
        screen->closeActionMenu();
        screen->schedule_action_async(ActionIntent::ShowInfo, conv);
        break;
    case ModalCommand::PingDestination:
        screen->closeActionMenu();
        screen->schedule_action_async(ActionIntent::PingDestination, conv);
        break;
    case ModalCommand::Delete:
        screen->openDeleteConfirm(conv);
        break;
    case ModalCommand::Cancel:
    default:
        screen->closeActionMenu();
        break;
    }
}

void ChatMessageListScreen::action_menu_key_cb(lv_event_t* e)
{
    auto* screen = static_cast<ChatMessageListScreen*>(lv_event_get_user_data(e));
    if (!screen || !screen->guard_ || !screen->guard_->alive ||
        lv_event_get_code(e) != LV_EVENT_KEY)
    {
        return;
    }
    const uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE)
    {
        screen->closeActionMenu();
        screen->closeDeleteConfirm();
        lv_event_stop_processing(e);
    }
}

void ChatMessageListScreen::modal_bg_key_cb(lv_event_t* e)
{
    action_menu_key_cb(e);
}

void ChatMessageListScreen::delete_confirm_cb(lv_event_t* e)
{
    auto* screen = static_cast<ChatMessageListScreen*>(lv_event_get_user_data(e));
    if (!screen || !screen->guard_ || !screen->guard_->alive)
    {
        return;
    }
    const chat::ConversationId conv = screen->modal_conv_;
    screen->closeDeleteConfirm();
    screen->schedule_action_async(ActionIntent::DeleteConversation, conv);
}

void ChatMessageListScreen::delete_cancel_cb(lv_event_t* e)
{
    auto* screen = static_cast<ChatMessageListScreen*>(lv_event_get_user_data(e));
    if (!screen || !screen->guard_ || !screen->guard_->alive)
    {
        return;
    }
    screen->closeDeleteConfirm();
}

void ChatMessageListScreen::page_shortcut_cb(lv_event_t* e)
{
    auto* screen = static_cast<ChatMessageListScreen*>(lv_event_get_user_data(e));
    if (!screen || !screen->guard_ || !screen->guard_->alive ||
        lv_event_get_code(e) != LV_EVENT_KEY)
    {
        return;
    }
    if (::ui::components::floating_search_box::is_open(screen->search_box_))
    {
        return;
    }
    if (screen->isModalOpen())
    {
        return;
    }

    const uint32_t key = lv_event_get_key(e);
    chat::ConversationId conv{};
    const bool has_selected = screen->tryGetSelectedConversation(&conv);
    if ((key == LV_KEY_ENTER || key == ' ') && has_selected)
    {
        screen->openActionMenu(conv);
        lv_event_stop_processing(e);
        return;
    }
    if ((key == 'c' || key == 'C') && has_selected)
    {
        screen->schedule_action_async(ActionIntent::SelectConversation, conv);
        lv_event_stop_processing(e);
        return;
    }
    if ((key == 'i' || key == 'I') && has_selected)
    {
        screen->schedule_action_async(ActionIntent::ShowInfo, conv);
        lv_event_stop_processing(e);
        return;
    }
    if ((key == 'p' || key == 'P') && has_selected &&
        conversation_supports_reticulum_ping(conv))
    {
        screen->schedule_action_async(ActionIntent::PingDestination, conv);
        lv_event_stop_processing(e);
        return;
    }
    if ((key == 'd' || key == 'D') && has_selected)
    {
        screen->openDeleteConfirm(conv);
        lv_event_stop_processing(e);
        return;
    }
    if (is_search_shortcut_key(key))
    {
        screen->openSearchModal();
        lv_event_stop_processing(e);
        return;
    }
    if (is_filter_toggle_shortcut_key(key))
    {
        screen->toggleFilterPanel();
        lv_event_stop_processing(e);
        return;
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
    if (filter_mode_ == FilterMode::Direct || filter_mode_ == FilterMode::All)
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

    ::ui::components::floating_search_box::close(search_box_);
    closeActionMenu();
    closeDeleteConfirm();
    restoreModalGroup();
    if (modal_group_)
    {
        lv_group_del(modal_group_);
        modal_group_ = nullptr;
    }
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
