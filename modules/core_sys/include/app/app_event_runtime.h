#pragma once

namespace sys
{
struct Event;
}

namespace app
{

class IAppFacade;

struct AppEventRuntimeHooks
{
    void (*update_core_services)(IAppFacade&) = nullptr;
    void (*tick)(IAppFacade&) = nullptr;
    bool (*dispatch_event)(IAppFacade&, sys::Event*) = nullptr;
    bool (*handle_event)(IAppFacade&, sys::Event*) = nullptr;
};

} // namespace app
