#include "platform/esp/arduino_common/mesh/esp_meshtastic_adapter_bridge.h"

#include "platform/esp/arduino_common/app_tasks.h"

#include <Arduino.h>
#include <RadioLib.h>
#include <cstring>

namespace platform::esp::arduino_common::mesh
{

uint32_t EspArduinoMeshClock::nowMs() const
{
    return millis();
}

::mesh::CryptoResult EspArduinoMeshCryptoProvider::random(uint8_t* out, size_t len)
{
    if (!out && len > 0)
    {
        return ::mesh::CryptoResult::fail(::mesh::CryptoFailure::InvalidInput);
    }
    for (size_t index = 0; index < len; ++index)
    {
        out[index] = static_cast<uint8_t>(::random(0, 256));
    }
    return ::mesh::CryptoResult::success();
}

::mesh::CryptoResult EspArduinoMeshCryptoProvider::sha256(::mesh::ByteView input, uint8_t out[32])
{
    (void)input;
    (void)out;
    return ::mesh::CryptoResult::fail(::mesh::CryptoFailure::Unsupported);
}

::mesh::CryptoResult EspArduinoMeshCryptoProvider::aesCcmEncrypt(::mesh::ByteView key,
                                                                 ::mesh::ByteView nonce,
                                                                 ::mesh::ByteView aad,
                                                                 ::mesh::ByteView plaintext,
                                                                 uint8_t* ciphertext,
                                                                 size_t ciphertext_capacity,
                                                                 size_t& ciphertext_size)
{
    (void)key;
    (void)nonce;
    (void)aad;
    (void)plaintext;
    (void)ciphertext;
    (void)ciphertext_capacity;
    ciphertext_size = 0;
    return ::mesh::CryptoResult::fail(::mesh::CryptoFailure::Unsupported);
}

::mesh::CryptoResult EspArduinoMeshCryptoProvider::aesCcmDecrypt(::mesh::ByteView key,
                                                                 ::mesh::ByteView nonce,
                                                                 ::mesh::ByteView aad,
                                                                 ::mesh::ByteView ciphertext,
                                                                 uint8_t* plaintext,
                                                                 size_t plaintext_capacity,
                                                                 size_t& plaintext_size)
{
    (void)key;
    (void)nonce;
    (void)aad;
    (void)ciphertext;
    (void)plaintext;
    (void)plaintext_capacity;
    plaintext_size = 0;
    return ::mesh::CryptoResult::fail(::mesh::CryptoFailure::Unsupported);
}

EspMeshtasticPacketRadio::EspMeshtasticPacketRadio(LoraBoard& board)
    : board_(board)
{
}

::mesh::RadioResult EspMeshtasticPacketRadio::configure(const ::mesh::RadioConfig& config)
{
    (void)config;
    return board_.isRadioOnline()
               ? ::mesh::RadioResult::success()
               : ::mesh::RadioResult::fail(::mesh::RadioFailure::NotReady);
}

::mesh::RadioResult EspMeshtasticPacketRadio::send(::mesh::ByteView packet)
{
    if (packet.empty())
    {
        return ::mesh::RadioResult::fail(::mesh::RadioFailure::InvalidConfig);
    }
    if (packet.size > 255)
    {
        return ::mesh::RadioResult::fail(::mesh::RadioFailure::InvalidConfig);
    }
    if (!board_.isRadioOnline())
    {
        return ::mesh::RadioResult::fail(::mesh::RadioFailure::NotReady);
    }

    app::AppTasks::requestRadioReceiveRestart();
#if defined(ARDUINO_LILYGO_LORA_SX1262) || defined(ARDUINO_LILYGO_LORA_SX1280)
    const int state = board_.transmitRadio(packet.data, packet.size);
#else
    const int state = RADIOLIB_ERR_UNSUPPORTED;
#endif
    if (state == RADIOLIB_ERR_NONE)
    {
        last_sent_size_ = packet.size;
        std::memcpy(last_sent_, packet.data, packet.size);
        board_.startRadioReceive();
        return ::mesh::RadioResult::success();
    }
    app::AppTasks::requestRadioReceiveRestart();
    return ::mesh::RadioResult::fail(::mesh::RadioFailure::TxFailed);
}

bool EspMeshtasticPacketRadio::poll(::mesh::RadioRxPacket& out)
{
    (void)out;
    return false;
}

void EspMeshEventBridge::emit(const ::mesh::MeshEvent& event)
{
    last_event = event;
    ++emitted_count;
}

EspMeshtasticAdapterBridge::EspMeshtasticAdapterBridge(LoraBoard& board)
    : radio_(board),
      identity_(local_store_, peer_store_, crypto_, clock_),
      receive_(protocol_, identity_, events_, clock_, &receive_dedup_),
      direct_(protocol_, identity_, radio_, clock_, events_),
      session_(radio_, protocol_, direct_, receive_, clock_)
{
    ::mesh::MeshRuntimeConfig config{};
    config.protocol = ::mesh::MeshProtocolKind::Meshtastic;
    config.radio.frequency_hz = 1;
    (void)session_.start(config);
}

::mesh::SendResult EspMeshtasticAdapterBridge::sendDirect(const ::mesh::DirectMessageCommand& command)
{
    if (session_.state() != ::mesh::MeshSessionState::Ready)
    {
        ::mesh::MeshRuntimeConfig config{};
        config.protocol = ::mesh::MeshProtocolKind::Meshtastic;
        config.radio.frequency_hz = 1;
        if (!session_.start(config))
        {
            return ::mesh::SendResult::fail(::mesh::SendFailure::NotReady);
        }
    }
    return session_.sendDirect(command);
}

bool EspMeshtasticAdapterBridge::copyLastSentPacket(uint8_t* out, size_t capacity, size_t& out_size) const
{
    if (!out || radio_.last_sent_size_ == 0 || capacity < radio_.last_sent_size_)
    {
        return false;
    }
    std::memcpy(out, radio_.last_sent_, radio_.last_sent_size_);
    out_size = radio_.last_sent_size_;
    return true;
}

void EspMeshtasticAdapterBridge::onRadioPacket(const uint8_t* data,
                                               size_t size,
                                               int16_t rssi,
                                               int8_t snr)
{
    if (!data || size == 0 || size > sizeof(::mesh::RadioRxPacket::bytes))
    {
        return;
    }

    ::mesh::RadioRxPacket packet{};
    std::memcpy(packet.bytes, data, size);
    packet.size = size;
    packet.rssi = rssi;
    packet.snr = snr;
    packet.received_at_ms = clock_.nowMs();
    receive_.onRadioPacket(packet);
}

void EspMeshtasticAdapterBridge::tick()
{
    session_.tick();
}

} // namespace platform::esp::arduino_common::mesh
