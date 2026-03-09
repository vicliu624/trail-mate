#pragma once

#include <cstddef>
#include <cstdint>

namespace platform::esp::common::walkie_runtime
{

struct Session
{
    void* impl = nullptr;
    bool paused_radio_tasks = false;
    bool lora_mode_configured = false;
    bool codec_open = false;
};

bool isSupported();
bool tryAcquire(Session* out_session);
Session acquire();
bool isValid(const Session& session);
bool isRadioReady(const Session& session);
bool isCodecReady(const Session& session);

bool configureFsk(Session* session, float freq_mhz, int8_t tx_power, char* error_buffer = nullptr,
                  size_t error_buffer_size = 0);
bool restoreLora(Session* session, char* error_buffer = nullptr, size_t error_buffer_size = 0);

int codecOpen(Session* session, uint8_t bits_per_sample, uint8_t channels, uint32_t sample_rate);
void codecClose(Session* session);
int codecRead(Session* session, uint8_t* buffer, size_t size);
int codecWrite(Session* session, uint8_t* buffer, size_t size);
void codecSetVolume(Session* session, uint8_t level);
void codecSetGain(Session* session, float db_value);
void codecSetMute(Session* session, bool enabled);

void standby(Session* session);
int startTransmit(Session* session, const uint8_t* data, size_t size);
int startReceive(Session* session);
uint32_t getRadioIrqFlags(Session* session);
void clearRadioIrqFlags(Session* session, uint32_t flags);
int getPacketLength(Session* session, bool update);
int readRadioData(Session* session, uint8_t* buffer, size_t size);

void release(Session* session);

} // namespace platform::esp::common::walkie_runtime
