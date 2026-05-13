#pragma once

#include "board/LoraBoard.h"
#include "mesh/ports/i_clock.h"
#include "mesh/ports/i_crypto_provider.h"
#include "mesh/ports/i_mesh_event_sink.h"
#include "mesh/ports/i_packet_radio.h"
#include "mesh/protocol/meshtastic/meshtastic_protocol_strategy.h"
#include "mesh/usecase/direct_message_service.h"
#include "mesh/usecase/mesh_dedup_service.h"
#include "mesh/usecase/mesh_session.h"
#include "mesh/usecase/peer_identity_service.h"
#include "mesh/usecase/receive_packet_service.h"
#include "platform/esp/arduino_common/mesh/esp_preferences_mesh_identity_store.h"

namespace platform::esp::arduino_common::mesh
{

class EspArduinoMeshClock final : public ::mesh::IClock
{
  public:
    uint32_t nowMs() const override;
};

class EspArduinoMeshCryptoProvider final : public ::mesh::ICryptoProvider
{
  public:
    ::mesh::CryptoResult random(uint8_t* out, size_t len) override;
    ::mesh::CryptoResult sha256(::mesh::ByteView input, uint8_t out[32]) override;
    ::mesh::CryptoResult aesCcmEncrypt(::mesh::ByteView key,
                                       ::mesh::ByteView nonce,
                                       ::mesh::ByteView aad,
                                       ::mesh::ByteView plaintext,
                                       uint8_t* ciphertext,
                                       size_t ciphertext_capacity,
                                       size_t& ciphertext_size) override;
    ::mesh::CryptoResult aesCcmDecrypt(::mesh::ByteView key,
                                       ::mesh::ByteView nonce,
                                       ::mesh::ByteView aad,
                                       ::mesh::ByteView ciphertext,
                                       uint8_t* plaintext,
                                       size_t plaintext_capacity,
                                       size_t& plaintext_size) override;
};

class EspMeshtasticPacketRadio final : public ::mesh::IPacketRadio
{
  public:
    explicit EspMeshtasticPacketRadio(LoraBoard& board);

    ::mesh::RadioResult configure(const ::mesh::RadioConfig& config) override;
    ::mesh::RadioResult send(::mesh::ByteView packet) override;
    bool poll(::mesh::RadioRxPacket& out) override;

  private:
    LoraBoard& board_;
    uint8_t last_sent_[256]{};
    size_t last_sent_size_ = 0;

    friend class EspMeshtasticAdapterBridge;
};

class EspMeshEventBridge final : public ::mesh::IMeshEventSink
{
  public:
    void emit(const ::mesh::MeshEvent& event) override;

    ::mesh::MeshEvent last_event{};
    uint32_t emitted_count = 0;
};

class EspMeshtasticAdapterBridge final
{
  public:
    explicit EspMeshtasticAdapterBridge(LoraBoard& board);

    ::mesh::SendResult sendDirect(const ::mesh::DirectMessageCommand& command);
    bool copyLastSentPacket(uint8_t* out, size_t capacity, size_t& out_size) const;
    void onRadioPacket(const uint8_t* data, size_t size, int16_t rssi, int8_t snr);
    void tick();

  private:
    EspMeshtasticPacketRadio radio_;
    ::mesh::meshtastic::MeshtasticProtocolStrategy protocol_;
    EspPreferencesLocalIdentityStore local_store_;
    EspPreferencesPeerKeyStore peer_store_;
    EspArduinoMeshCryptoProvider crypto_;
    EspArduinoMeshClock clock_;
    EspMeshEventBridge events_;
    ::mesh::PeerIdentityService identity_;
    ::mesh::MeshDedupService receive_dedup_;
    ::mesh::ReceivePacketService receive_;
    ::mesh::DirectMessageService direct_;
    ::mesh::MeshSession session_;
};

} // namespace platform::esp::arduino_common::mesh
