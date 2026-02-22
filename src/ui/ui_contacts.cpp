/**
 * @file ui_contacts.cpp
 * @brief Contacts page implementation
 */

#include "ui_contacts.h"
#include "../../app/app_context.h"
#include "../../chat/domain/chat_types.h"
#include "screens/chat/chat_compose_components.h"
#include "screens/contacts/contacts_page_components.h"
#include "screens/contacts/contacts_page_input.h"
#include "screens/contacts/contacts_page_layout.h"
#include "screens/contacts/contacts_state.h"
#include "ui_common.h"
#include "widgets/top_bar.h"
#include <Arduino.h>
#include <algorithm>

#define CONTACTS_DEBUG 0
#if CONTACTS_DEBUG
#define CONTACTS_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define CONTACTS_LOG(...)
#endif

using namespace contacts::ui;

static void contacts_top_bar_back(void* /*user_data*/)
{
    if (g_contacts_state.exiting)
    {
        return;
    }
    g_contacts_state.exiting = true;
    if (g_contacts_state.refresh_timer != nullptr)
    {
        lv_timer_del(g_contacts_state.refresh_timer);
        g_contacts_state.refresh_timer = nullptr;
    }
    if (g_contacts_state.conversation_timer != nullptr)
    {
        lv_timer_del(g_contacts_state.conversation_timer);
        g_contacts_state.conversation_timer = nullptr;
    }
    if (g_contacts_state.discover_scan_timer != nullptr)
    {
        lv_timer_del(g_contacts_state.discover_scan_timer);
        g_contacts_state.discover_scan_timer = nullptr;
    }
    if (g_contacts_state.root)
    {
        lv_obj_add_flag(g_contacts_state.root, LV_OBJ_FLAG_HIDDEN);
    }
    ui_request_exit_to_menu();
}

void ui_contacts_enter(lv_obj_t* parent)
{
    CONTACTS_LOG("[Contacts] Entering Contacts page\n");

    // 清理旧状态
    if (g_contacts_state.root != nullptr)
    {
        lv_obj_del(g_contacts_state.root);
        g_contacts_state.root = nullptr;
    }

    // Bind services for Contacts screen usage (avoid heavy includes in components)
    app::AppContext& app_ctx = app::AppContext::getInstance();
    g_contacts_state.exiting = false;
    g_contacts_state.contact_service = &app_ctx.getContactService();
    g_contacts_state.chat_service = &app_ctx.getChatService();

    // Avoid auto-adding new widgets to the current default group during creation.
    lv_group_t* prev_group = lv_group_get_default();
    set_default_group(nullptr);

    // ✅ 所有布局代码都在layout模块中
    g_contacts_state.root = contacts::ui::layout::create_root(parent);

    lv_obj_t* header = contacts::ui::layout::create_header(
        g_contacts_state.root,
        contacts_top_bar_back,
        nullptr);

    lv_obj_t* content = contacts::ui::layout::create_content(g_contacts_state.root);
    g_contacts_state.page = content;

    // 更新电池显示
    ui_update_top_bar_battery(g_contacts_state.top_bar);

    // 仅创建两列：Filter + List（移除常驻 Action Panel）
    create_filter_panel(content);
    create_list_panel(content);
    g_contacts_state.action_panel = nullptr;
    g_contacts_state.chat_btn = nullptr;
    g_contacts_state.position_btn = nullptr;
    g_contacts_state.edit_btn = nullptr;
    g_contacts_state.del_btn = nullptr;
    g_contacts_state.add_btn = nullptr;
    g_contacts_state.info_btn = nullptr;
    g_contacts_state.action_back_btn = nullptr;

    // Restore previous default group before initializing input.
    set_default_group(prev_group);

    // Reset mode/focus state on every enter
    g_contacts_state.current_mode = ContactsMode::Contacts;
    g_contacts_state.last_action_mode = ContactsMode::Contacts;
    g_contacts_state.current_page = 0;
    g_contacts_state.selected_index = -1;

    // Initialize input handling
    init_contacts_input();

    // Load data and refresh UI
    refresh_contacts_data();
    refresh_ui();

    g_contacts_state.initialized = true;
    CONTACTS_LOG("[Contacts] Contacts page initialized\n");
}

void ui_contacts_exit(lv_obj_t* parent)
{
    (void)parent;
    CONTACTS_LOG("[Contacts] Exiting Contacts page\n");

    if (g_contacts_state.compose_screen)
    {
        if (g_contacts_state.compose_ime)
        {
            g_contacts_state.compose_ime->detach();
            delete g_contacts_state.compose_ime;
            g_contacts_state.compose_ime = nullptr;
        }
        delete g_contacts_state.compose_screen;
        g_contacts_state.compose_screen = nullptr;
    }
    if (g_contacts_state.conversation_screen)
    {
        delete g_contacts_state.conversation_screen;
        g_contacts_state.conversation_screen = nullptr;
    }
    if (g_contacts_state.conversation_timer != nullptr)
    {
        lv_timer_del(g_contacts_state.conversation_timer);
        g_contacts_state.conversation_timer = nullptr;
    }
    if (g_contacts_state.discover_scan_timer != nullptr)
    {
        lv_timer_del(g_contacts_state.discover_scan_timer);
        g_contacts_state.discover_scan_timer = nullptr;
    }

    if (g_contacts_state.refresh_timer != nullptr)
    {
        lv_timer_del(g_contacts_state.refresh_timer);
        g_contacts_state.refresh_timer = nullptr;
    }

    cleanup_contacts_input();
    cleanup_modals();

    if (g_contacts_state.root != nullptr)
    {
        lv_obj_del(g_contacts_state.root);
        g_contacts_state.root = nullptr;
    }

    g_contacts_state = ContactsPageState{};
    CONTACTS_LOG("[Contacts] Contacts page cleaned up\n");
}

// Implementation of refresh_contacts_data moved here to avoid library compilation issues
void refresh_contacts_data_impl()
{
    app::AppContext& app_ctx = app::AppContext::getInstance();
    chat::contacts::ContactService& contact_service = app_ctx.getContactService();
    g_contacts_state.contacts_list = contact_service.getContacts();
    g_contacts_state.nearby_list = contact_service.getNearby();

    CONTACTS_LOG("[Contacts] Data refreshed: %zu contacts, %zu nearby\n",
                 g_contacts_state.contacts_list.size(),
                 g_contacts_state.nearby_list.size());
}
