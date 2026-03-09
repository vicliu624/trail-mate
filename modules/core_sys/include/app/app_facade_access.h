#pragma once

#include "app/app_facades.h"

namespace app
{

void bindAppFacade(IAppFacade& app_facade);
void unbindAppFacade();
bool hasAppFacade();

IAppFacade& appFacade();
IAppConfigFacade& configFacade();
IAppMessagingFacade& messagingFacade();
IAppTeamFacade& teamFacade();
IAppRuntimeFacade& runtimeFacade();
IAppLifecycleFacade& lifecycleFacade();

} // namespace app
