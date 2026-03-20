#include "platform/esp/common/walkie_runtime.h"
#include "boards/tab5/tab5_board.h"

#include <cstdio>
#include <cstring>

#include "boards/tab5/codec_compat.h"
#include "platform/esp/idf_common/sx126x_radio.h"

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
namespace
{
constexpr float kFskBitRateKbps = 9.6f;
constexpr float kFskFreqDevKHz = 5.0f;
constexpr float kFskRxBwKHz = 156.2f;
constexpr uint16_t kFskPreambleLen = 16;
constexpr uint8_t kFskSyncWord[] = {0x2D, 0x01};

struct BackendState
{
    ::boards::tab5::CodecCompat codec;
};

BackendState s_backend_state;

bool board_supports_walkie()
{
    return ::boards::tab5::Tab5Board::hasAudio() &&
           (::boards::tab5::Tab5Board::hasLora() ||
            ::boards::tab5::Tab5Board::hasM5BusLoraRouting());
}

platform::esp::idf_common::Sx126xRadio& radio()
{
    return platform::esp::idf_common::Sx126xRadio::instance();
}

BackendState* resolve_state(const ::platform::esp::common::walkie_runtime::Session* session)
{
    return (session && session->impl == &s_backend_state) ? &s_backend_state : nullptr;
}

void write_error(char* error_buffer, size_t error_buffer_size, const char* message)
{
    if (!error_buffer || error_buffer_size == 0)
    {
        return;
    }
    if (!message)
    {
        error_buffer[0] = '\0';
        return;
    }
    std::snprintf(error_buffer, error_buffer_size, "%s", message);
}
} // namespace
#endif

namespace platform::esp::common::walkie_runtime
{

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
bool isSupported()
{
    return board_supports_walkie();
}

bool tryAcquire(Session* out_session)
{
    if (!out_session)
    {
        return false;
    }

    *out_session = {};
    if (!isSupported())
    {
        return false;
    }
    if (!radio().acquire())
    {
        return false;
    }
    out_session->impl = &s_backend_state;
    return true;
}

Session acquire()
{
    Session session{};
    tryAcquire(&session);
    return session;
}

bool isValid(const Session& session)
{
    return resolve_state(&session) != nullptr;
}

bool isRadioReady(const Session& session)
{
    return isValid(session) && radio().isOnline();
}

bool isCodecReady(const Session& session)
{
    return isValid(session);
}

bool configureFsk(Session* session, float freq_mhz, int8_t tx_power, char* error_buffer,
                  size_t error_buffer_size)
{
    if (!resolve_state(session) || !radio().isOnline())
    {
        write_error(error_buffer, error_buffer_size, "Radio offline");
        return false;
    }

    if (!radio().configureFsk(freq_mhz,
                              tx_power,
                              kFskBitRateKbps,
                              kFskFreqDevKHz,
                              kFskRxBwKHz,
                              kFskPreambleLen,
                              kFskSyncWord,
                              sizeof(kFskSyncWord),
                              2))
    {
        write_error(error_buffer, error_buffer_size, radio().lastError());
        return false;
    }

    session->lora_mode_configured = true;
    write_error(error_buffer, error_buffer_size, nullptr);
    return true;
}

bool restoreLora(Session* session, char* error_buffer, size_t error_buffer_size)
{
    if (!resolve_state(session))
    {
        return false;
    }
    if (session->lora_mode_configured)
    {
        radio().standby();
        session->lora_mode_configured = false;
    }
    write_error(error_buffer, error_buffer_size, nullptr);
    return true;
}

int codecOpen(Session* session, uint8_t bits_per_sample, uint8_t channels, uint32_t sample_rate)
{
    BackendState* state = resolve_state(session);
    if (!state)
    {
        return -1;
    }
    const int rc = state->codec.open(bits_per_sample, channels, sample_rate);
    if (rc == 0)
    {
        session->codec_open = true;
    }
    return rc;
}

void codecClose(Session* session)
{
    BackendState* state = resolve_state(session);
    if (!state || !session->codec_open)
    {
        return;
    }
    state->codec.close();
    session->codec_open = false;
}

int codecRead(Session* session, uint8_t* buffer, size_t size)
{
    BackendState* state = resolve_state(session);
    return state ? state->codec.read(buffer, size) : -1;
}

int codecWrite(Session* session, uint8_t* buffer, size_t size)
{
    BackendState* state = resolve_state(session);
    return state ? state->codec.write(buffer, size) : -1;
}

void codecSetVolume(Session* session, uint8_t level)
{
    BackendState* state = resolve_state(session);
    if (state)
    {
        state->codec.setVolume(level);
    }
}

void codecSetGain(Session* session, float db_value)
{
    BackendState* state = resolve_state(session);
    if (state)
    {
        state->codec.setGain(db_value);
    }
}

void codecSetMute(Session* session, bool enabled)
{
    BackendState* state = resolve_state(session);
    if (state)
    {
        state->codec.setMute(enabled);
        state->codec.setOutMute(enabled);
    }
}

void standby(Session* session)
{
    if (resolve_state(session))
    {
        radio().standby();
    }
}

int startTransmit(Session* session, const uint8_t* data, size_t size)
{
    return resolve_state(session) ? radio().startTransmit(data, size) : -1;
}

int startReceive(Session* session)
{
    return (resolve_state(session) && radio().startReceive()) ? 0 : -1;
}

uint32_t getRadioIrqFlags(Session* session)
{
    return resolve_state(session) ? radio().getIrqFlags() : 0;
}

void clearRadioIrqFlags(Session* session, uint32_t flags)
{
    if (resolve_state(session))
    {
        radio().clearIrqFlags(flags);
    }
}

int getPacketLength(Session* session, bool update)
{
    return resolve_state(session) ? radio().getPacketLength(update) : -1;
}

int readRadioData(Session* session, uint8_t* buffer, size_t size)
{
    return resolve_state(session) ? radio().readPacket(buffer, size) : -1;
}

void release(Session* session)
{
    if (!session)
    {
        return;
    }

    const bool acquired = resolve_state(session) != nullptr;
    codecClose(session);
    restoreLora(session);
    if (acquired)
    {
        radio().release();
    }
    *session = {};
}
#else
bool isSupported()
{
    return false;
}

bool tryAcquire(Session* out_session)
{
    if (out_session)
    {
        *out_session = {};
    }
    return false;
}

Session acquire()
{
    return {};
}

bool isValid(const Session& session)
{
    (void)session;
    return false;
}

bool isRadioReady(const Session& session)
{
    (void)session;
    return false;
}

bool isCodecReady(const Session& session)
{
    (void)session;
    return false;
}

bool configureFsk(Session* session, float freq_mhz, int8_t tx_power, char* error_buffer,
                  size_t error_buffer_size)
{
    (void)session;
    (void)freq_mhz;
    (void)tx_power;
    (void)error_buffer;
    (void)error_buffer_size;
    return false;
}

bool restoreLora(Session* session, char* error_buffer, size_t error_buffer_size)
{
    (void)session;
    (void)error_buffer;
    (void)error_buffer_size;
    return false;
}

int codecOpen(Session* session, uint8_t bits_per_sample, uint8_t channels, uint32_t sample_rate)
{
    (void)session;
    (void)bits_per_sample;
    (void)channels;
    (void)sample_rate;
    return -1;
}

void codecClose(Session* session)
{
    (void)session;
}

int codecRead(Session* session, uint8_t* buffer, size_t size)
{
    (void)session;
    (void)buffer;
    (void)size;
    return -1;
}

int codecWrite(Session* session, uint8_t* buffer, size_t size)
{
    (void)session;
    (void)buffer;
    (void)size;
    return -1;
}

void codecSetVolume(Session* session, uint8_t level)
{
    (void)session;
    (void)level;
}

void codecSetGain(Session* session, float db_value)
{
    (void)session;
    (void)db_value;
}

void codecSetMute(Session* session, bool enabled)
{
    (void)session;
    (void)enabled;
}

void standby(Session* session)
{
    (void)session;
}

int startTransmit(Session* session, const uint8_t* data, size_t size)
{
    (void)session;
    (void)data;
    (void)size;
    return -1;
}

int startReceive(Session* session)
{
    (void)session;
    return -1;
}

uint32_t getRadioIrqFlags(Session* session)
{
    (void)session;
    return 0;
}

void clearRadioIrqFlags(Session* session, uint32_t flags)
{
    (void)session;
    (void)flags;
}

int getPacketLength(Session* session, bool update)
{
    (void)session;
    (void)update;
    return -1;
}

int readRadioData(Session* session, uint8_t* buffer, size_t size)
{
    (void)session;
    (void)buffer;
    (void)size;
    return -1;
}

void release(Session* session)
{
    if (session)
    {
        *session = {};
    }
}
#endif

} // namespace platform::esp::common::walkie_runtime
