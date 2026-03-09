#pragma once

#include <memory>

#include "platform/esp/arduino_common/team/pairing/team_pairing_service_espnow.h"
#include "platform/esp/arduino_common/team/pairing/team_pairing_transport_espnow.h"
#include "platform/esp/arduino_common/team/runtime/team_runtime_arduino.h"
#include "platform/esp/arduino_common/team/runtime/team_track_source_gps.h"
#include "team/ports/i_team_pairing_event_sink.h"
#include "team/ports/i_team_pairing_transport.h"
#include "team/ports/i_team_runtime.h"
#include "team/ports/i_team_track_source.h"
#include "team/usecase/team_pairing_service.h"

namespace platform::esp::arduino_common
{

struct TeamPlatformBundle
{
    std::unique_ptr<team::ITeamRuntime> runtime;
    std::unique_ptr<team::ITeamTrackSource> track_source;
    std::unique_ptr<team::ITeamPairingTransport> pairing_transport;
    std::unique_ptr<team::TeamPairingService> pairing_service;
};

inline TeamPlatformBundle createTeamPlatformBundle(team::ITeamPairingEventSink& pairing_event_sink)
{
    TeamPlatformBundle bundle;
    bundle.runtime = std::unique_ptr<team::ITeamRuntime>(new team::infra::TeamRuntimeArduino());
    bundle.track_source = std::unique_ptr<team::ITeamTrackSource>(new team::infra::TeamTrackSourceGps());
    bundle.pairing_transport = std::unique_ptr<team::ITeamPairingTransport>(new team::infra::EspNowTeamPairingTransport());
    bundle.pairing_service = std::unique_ptr<team::TeamPairingService>(
        new team::EspNowTeamPairingService(*bundle.runtime, pairing_event_sink, *bundle.pairing_transport));
    return bundle;
}

} // namespace platform::esp::arduino_common
