#include "platform/esp/arduino_common/team/pairing/team_pairing_service_espnow.h"

#include <cstring>

namespace team
{

EspNowTeamPairingService::EspNowTeamPairingService(team::ITeamRuntime& runtime,
                                                   team::ITeamPairingEventSink& event_sink,
                                                   team::ITeamPairingTransport& transport)
    : transport_(transport), core_(runtime, event_sink, transport)
{
}

bool EspNowTeamPairingService::ensureTransport()
{
    if (transport_ready_)
    {
        return true;
    }
    if (!transport_.begin(*this))
    {
        return false;
    }
    transport_ready_ = true;
    return true;
}

void EspNowTeamPairingService::shutdownTransport()
{
    if (!transport_ready_)
    {
        return;
    }
    transport_.end();
    transport_ready_ = false;
    rx_pending_ = false;
}

bool EspNowTeamPairingService::startLeader(const TeamId& team_id,
                                           uint32_t key_id,
                                           const uint8_t* psk,
                                           size_t psk_len,
                                           uint32_t leader_id,
                                           const char* team_name)
{
    if (!ensureTransport())
    {
        return false;
    }
    if (!core_.startLeader(team_id, key_id, psk, psk_len, leader_id, team_name))
    {
        shutdownTransport();
        return false;
    }
    return true;
}

bool EspNowTeamPairingService::startMember(uint32_t self_id)
{
    if (!ensureTransport())
    {
        return false;
    }
    if (!core_.startMember(self_id))
    {
        shutdownTransport();
        return false;
    }
    return true;
}

void EspNowTeamPairingService::stop()
{
    core_.stop();
    shutdownTransport();
}

TeamPairingStatus EspNowTeamPairingService::getStatus() const
{
    return core_.getStatus();
}

void EspNowTeamPairingService::onPairingReceive(const uint8_t* mac, const uint8_t* data, size_t len)
{
    if (!mac || !data || len == 0 || len > sizeof(rx_packet_.data))
    {
        return;
    }
    portENTER_CRITICAL(&rx_mux_);
    if (!rx_pending_)
    {
        memcpy(rx_packet_.mac, mac, 6);
        memcpy(rx_packet_.data, data, len);
        rx_packet_.len = len;
        rx_pending_ = true;
    }
    portEXIT_CRITICAL(&rx_mux_);
}

void EspNowTeamPairingService::update()
{
    if (!transport_ready_)
    {
        return;
    }

    RxPacket rx{};
    bool has_rx = false;
    portENTER_CRITICAL(&rx_mux_);
    if (rx_pending_)
    {
        rx = rx_packet_;
        rx_pending_ = false;
        has_rx = true;
    }
    portEXIT_CRITICAL(&rx_mux_);

    if (has_rx)
    {
        core_.handleIncomingPacket(rx.mac, rx.data, rx.len);
    }

    core_.update();
    if (transport_ready_ && core_.getStatus().state == TeamPairingState::Idle)
    {
        shutdownTransport();
    }
}

} // namespace team
