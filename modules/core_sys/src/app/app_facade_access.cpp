#include "app/app_facade_access.h"

#include <cstdlib>

namespace app
{
namespace
{
IAppFacade* g_app_facade = nullptr;

IAppFacade& requireAppFacade()
{
    if (!g_app_facade)
    {
        std::abort();
    }
    return *g_app_facade;
}
} // namespace

void bindAppFacade(IAppFacade& app_facade)
{
    g_app_facade = &app_facade;
}

void unbindAppFacade()
{
    g_app_facade = nullptr;
}

bool hasAppFacade()
{
    return g_app_facade != nullptr;
}

IAppFacade& appFacade()
{
    return requireAppFacade();
}

IAppConfigFacade& configFacade()
{
    return requireAppFacade();
}

IAppMessagingFacade& messagingFacade()
{
    return requireAppFacade();
}

IAppTeamFacade& teamFacade()
{
    return requireAppFacade();
}

IAppRuntimeFacade& runtimeFacade()
{
    return requireAppFacade();
}

IAppLifecycleFacade& lifecycleFacade()
{
    return requireAppFacade();
}

} // namespace app
