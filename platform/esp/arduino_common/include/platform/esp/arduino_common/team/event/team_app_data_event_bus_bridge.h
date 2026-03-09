#pragma once

#include "team/usecase/team_service.h"

namespace team::infra
{

class TeamAppDataEventBusBridge : public team::TeamService::UnhandledAppDataObserver
{
  public:
    void onUnhandledAppData(const chat::MeshIncomingData& msg) override;
};

} // namespace team::infra
