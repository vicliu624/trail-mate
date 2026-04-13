#include "ui/screens/contacts/contacts_page_runtime.h"

#include "app/app_facade_access.h"
#include "chat/domain/chat_types.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "ui/app_runtime.h"
#include "ui/screens/chat/chat_compose_components.h"
#include "ui/screens/chat/chat_conversation_components.h"
#include "ui/screens/contacts/contacts_page_components.h"
#include "ui/screens/contacts/contacts_page_input.h"
#include "ui/screens/contacts/contacts_page_layout.h"
#include "ui/screens/contacts/contacts_state.h"
#include "ui/ui_common.h"
#include "ui/widgets/ime/ime_widget.h"
#include "ui/widgets/top_bar.h"

#include <cstdio>

#define CONTACTS_DEBUG 0
#if CONTACTS_DEBUG
#define CONTACTS_LOG(...) std::printf(__VA_ARGS__)
#else
#define CONTACTS_LOG(...)
#endif

using namespace contacts::ui;

namespace
{

using contacts::ui::shell::Host;

const Host* s_host = nullptr;

void request_exit()
{
    if (s_host)
    {
        ::ui::page::request_exit(s_host);
        return;
    }
    ui_request_exit_to_menu();
}

void contacts_top_bar_back(void*)
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
    request_exit();
}

void refresh_contacts_data_impl_internal()
{
    chat::contacts::ContactService& contact_service = app::messagingFacade().getContactService();
    g_contacts_state.contacts_list = contact_service.getContacts();
    g_contacts_state.nearby_list = contact_service.getNearby();
    g_contacts_state.ignored_list = contact_service.getIgnoredNodes();

    CONTACTS_LOG("[Contacts] Data refreshed: %zu contacts, %zu nearby, %zu ignored\n",
                 g_contacts_state.contacts_list.size(),
                 g_contacts_state.nearby_list.size(),
                 g_contacts_state.ignored_list.size());
}

} // namespace

void refresh_contacts_data_impl()
{
    refresh_contacts_data_impl_internal();
}

namespace contacts::ui::runtime
{

bool is_available()
{
    return app::hasAppFacade();
}

void enter(const shell::Host* host, lv_obj_t* parent)
{
    if (!is_available())
    {
        return;
    }

    s_host = host;

    CONTACTS_LOG("[Contacts] Entering Contacts page\n");

    if (g_contacts_state.root != nullptr)
    {
        lv_obj_del(g_contacts_state.root);
        g_contacts_state.root = nullptr;
    }

    g_contacts_state.exiting = false;
    g_contacts_state.contact_service = &app::messagingFacade().getContactService();
    g_contacts_state.chat_service = &app::messagingFacade().getChatService();

    lv_group_t* prev_group = lv_group_get_default();
    set_default_group(nullptr);

    g_contacts_state.root = contacts::ui::layout::create_root(parent);
    contacts::ui::layout::create_header(g_contacts_state.root, contacts_top_bar_back, nullptr);

    lv_obj_t* content = contacts::ui::layout::create_content(g_contacts_state.root);
    g_contacts_state.page = content;
    ui_update_top_bar_battery(g_contacts_state.top_bar);

    create_filter_panel(content);
    contacts::ui::layout::create_list_panel(content);

    set_default_group(prev_group);

    g_contacts_state.current_mode = ContactsMode::Contacts;
    g_contacts_state.last_action_mode = ContactsMode::Contacts;
    g_contacts_state.current_page = 0;
    g_contacts_state.selected_index = -1;

    init_contacts_input();
    refresh_contacts_data();
    refresh_ui();

    g_contacts_state.initialized = true;
    CONTACTS_LOG("[Contacts] Contacts page initialized\n");
}

void exit(lv_obj_t* parent)
{
    (void)parent;

    if (!is_available())
    {
        s_host = nullptr;
        return;
    }

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
    s_host = nullptr;
}

} // namespace contacts::ui::runtime
