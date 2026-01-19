/**
 * @file ui_contacts.cpp
 * @brief Contacts page implementation
 */

#include "ui_contacts.h"
#include "screens/contacts/contacts_state.h"
#include "screens/contacts/contacts_page_components.h"
#include "screens/contacts/contacts_page_layout.h"
#include "screens/contacts/contacts_page_input.h"
#include "screens/chat/chat_compose_components.h"
#include "widgets/top_bar.h"
#include "ui_common.h"
#include "../../app/app_context.h"
#include "../../chat/domain/chat_types.h"
#include <Arduino.h>
#include <algorithm>

#define CONTACTS_DEBUG 1
#if CONTACTS_DEBUG
#define CONTACTS_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define CONTACTS_LOG(...)
#endif

using namespace contacts::ui;

static void contacts_top_bar_back(void* /*user_data*/)
{
    ui_contacts_exit(nullptr);
    menu_show();
}

void ui_contacts_enter(lv_obj_t* parent)
{
    CONTACTS_LOG("[Contacts] Entering Contacts page\n");

    // 清理旧状态
    if (g_contacts_state.root != nullptr) {
        lv_obj_del(g_contacts_state.root);
        g_contacts_state.root = nullptr;
    }

    // Bind services for Contacts screen usage (avoid heavy includes in components)
    app::AppContext& app_ctx = app::AppContext::getInstance();
    g_contacts_state.contact_service = &app_ctx.getContactService();
    g_contacts_state.chat_service = &app_ctx.getChatService();

    // ✅ 所有布局代码都在layout模块中
    g_contacts_state.root = contacts::ui::layout::create_root(parent);

    lv_obj_t* header = contacts::ui::layout::create_header(
        g_contacts_state.root,
        contacts_top_bar_back,
        nullptr
    );

    lv_obj_t* content = contacts::ui::layout::create_content(g_contacts_state.root);
    g_contacts_state.page = content;

    // 更新电池显示
    ui_update_top_bar_battery(g_contacts_state.top_bar);

    // 创建三个面板
    create_filter_panel(content);
    create_list_panel(content);
    create_action_panel(content);
    
    // Initialize input handling
    init_contacts_input();
    
    // Load data and refresh UI
    refresh_contacts_data();
    refresh_ui();
    
    // Create refresh timer (update every 5 seconds)
    g_contacts_state.refresh_timer = lv_timer_create([](lv_timer_t* timer) {
        (void)timer;
        refresh_contacts_data();
        refresh_ui();
    }, 5000, nullptr);
    lv_timer_set_repeat_count(g_contacts_state.refresh_timer, -1);
    
    g_contacts_state.initialized = true;
    CONTACTS_LOG("[Contacts] Contacts page initialized\n");
}

void ui_contacts_exit(lv_obj_t* parent)
{
    (void)parent;
    CONTACTS_LOG("[Contacts] Exiting Contacts page\n");
    
    if (g_contacts_state.compose_screen) {
        delete g_contacts_state.compose_screen;
        g_contacts_state.compose_screen = nullptr;
    }

    if (g_contacts_state.refresh_timer != nullptr) {
        lv_timer_del(g_contacts_state.refresh_timer);
        g_contacts_state.refresh_timer = nullptr;
    }
    
    cleanup_contacts_input();
    cleanup_modals();
    
    if (g_contacts_state.root != nullptr) {
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

    auto should_keep = [](const chat::contacts::NodeInfo& node, chat::MeshProtocol protocol) {
        if (protocol == chat::MeshProtocol::Meshtastic) {
            return node.protocol != chat::contacts::NodeProtocolType::MeshCore;
        }
        if (protocol == chat::MeshProtocol::MeshCore) {
            return node.protocol != chat::contacts::NodeProtocolType::Meshtastic;
        }
        return true;
    };

    chat::MeshProtocol protocol = app_ctx.getConfig().mesh_protocol;

    g_contacts_state.contacts_list = contact_service.getContacts();
    g_contacts_state.contacts_list.erase(
        std::remove_if(
            g_contacts_state.contacts_list.begin(),
            g_contacts_state.contacts_list.end(),
            [protocol, &should_keep](const chat::contacts::NodeInfo& node) {
                return !should_keep(node, protocol);
            }),
        g_contacts_state.contacts_list.end());

    g_contacts_state.nearby_list = contact_service.getNearby();
    g_contacts_state.nearby_list.erase(
        std::remove_if(
            g_contacts_state.nearby_list.begin(),
            g_contacts_state.nearby_list.end(),
            [protocol, &should_keep](const chat::contacts::NodeInfo& node) {
                return !should_keep(node, protocol);
            }),
        g_contacts_state.nearby_list.end());

    CONTACTS_LOG("[Contacts] Data refreshed: %zu contacts, %zu nearby\n",
                 g_contacts_state.contacts_list.size(),
                 g_contacts_state.nearby_list.size());
}
