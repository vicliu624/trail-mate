#include "fake/fake_chat_action_sink.h"
#include "fake/fake_chat_presentation_source.h"

#include "product_composition/app_services_bundle.h"
#include "product_composition/composition_status.h"
#include "product_composition/presentation_bundle.h"
#include "product_composition/target_app_shell.h"
#include "ui_presentation/chat/chat_workspace_model.h"
#include "ui_presentation/menu/menu_model.h"
#include "ui_presentation/screen/screen_binding_registry.h"

#include <cassert>

namespace chat
{
class ChatService
{
};

namespace contacts
{
class ContactService
{
};
} // namespace contacts
} // namespace chat

namespace
{

class FakeShell final : public product_composition::ITargetAppShell
{
  public:
    bool initialize() override
    {
        initialized = true;
        return true;
    }

    void tick() override
    {
        ++ticks;
    }

    void shutdown() override
    {
        shutdown_called = true;
    }

    bool initialized = false;
    bool shutdown_called = false;
    int ticks = 0;
};

void test_app_services_bundle_is_explicit()
{
    product_composition::AppServicesBundle empty;
    assert(!product_composition::hasChatServices(empty));

    chat::ChatService chat_service;
    chat::contacts::ContactService contacts;

    product_composition::AppServicesBundle bundle;
    bundle.chat = &chat_service;
    assert(!product_composition::hasChatServices(bundle));

    bundle.contacts = &contacts;
    assert(product_composition::hasChatServices(bundle));
}

void test_presentation_bundle_exports_workspace_graph()
{
    product_composition::PresentationBundle bundle;
    assert(!product_composition::hasInteractivePresentation(bundle));
    assert(bundle.ux_menu == nullptr);
    assert(!product_composition::hasUxMenu(bundle));
    assert(bundle.screen_bindings == nullptr);
    assert(!product_composition::hasScreenBindings(bundle));

    ui::tests::FakeChatPresentationSource chat_source;
    ui::tests::FakeChatActionSink chat_sink;
    ui::chat::ChatWorkspaceModel chat_model(chat_source, chat_sink);

    bundle.workspace.chat = &chat_model;
    assert(product_composition::hasInteractivePresentation(bundle));
    assert(bundle.workspace.hasChat());

    ui::menu::MenuModel ux_menu;
    assert(ux_menu.add({ui::menu::MenuScreenId::Chat, "Chat", true}));
    bundle.ux_menu = &ux_menu;
    assert(bundle.ux_menu != nullptr);
    assert(bundle.ux_menu->size() == 1);
    assert(product_composition::hasUxMenu(bundle));

    ui::screen::ScreenBindingRegistry screen_bindings;
    assert(screen_bindings.add({ui::menu::MenuScreenId::Chat, "chat", true}));
    bundle.screen_bindings = &screen_bindings;
    assert(bundle.screen_bindings != nullptr);
    assert(product_composition::hasScreenBindings(bundle));
}

void test_target_app_shell_lifecycle_contract()
{
    FakeShell shell;
    assert(shell.initialize());
    shell.tick();
    shell.tick();
    shell.shutdown();

    assert(shell.initialized);
    assert(shell.ticks == 2);
    assert(shell.shutdown_called);
}

void test_composition_status_is_explicit()
{
    auto status = product_composition::CompositionStatus::Ok;
    assert(status == product_composition::CompositionStatus::Ok);

    status = product_composition::CompositionStatus::MissingRequiredService;
    assert(status != product_composition::CompositionStatus::Ok);
}

} // namespace

int main()
{
    test_app_services_bundle_is_explicit();
    test_presentation_bundle_exports_workspace_graph();
    test_target_app_shell_lifecycle_contract();
    test_composition_status_is_explicit();
    return 0;
}
