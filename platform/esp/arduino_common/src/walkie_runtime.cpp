#include "platform/esp/common/walkie_runtime.h"

#if defined(ARDUINO_T_LORA_PAGER) && defined(ARDUINO_LILYGO_LORA_SX1262) && defined(USING_AUDIO_CODEC)
#include <Arduino.h>
#include <RadioLib.h>
#include <cstring>

#include "board/TLoRaPagerBoard.h"
#include "platform/esp/arduino_common/app_tasks.h"
#include "platform/esp/boards/board_runtime.h"

namespace platform::esp::common::walkie_runtime
{
namespace
{
constexpr float kFskBitRateKbps = 9.6f;
constexpr float kFskFreqDevKHz = 5.0f;
constexpr float kFskRxBwKHz = 156.2f;
constexpr uint16_t kFskPreambleLen = 16;
constexpr uint8_t kFskSyncWord[] = {0x2D, 0x01};

TLoRaPagerBoard* resolveBoard(const Session* session)
{
    return session ? static_cast<TLoRaPagerBoard*>(session->impl) : nullptr;
}

void writeError(char* error_buffer, size_t error_buffer_size, const char* message)
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
    snprintf(error_buffer, error_buffer_size, "%s", message);
}

void writeStateError(char* error_buffer, size_t error_buffer_size, const char* prefix, int state)
{
    if (!error_buffer || error_buffer_size == 0)
    {
        return;
    }
    snprintf(error_buffer, error_buffer_size, "%s %d", prefix, state);
}
} // namespace

bool isSupported()
{
    return true;
}

bool tryAcquire(Session* out_session)
{
    if (!out_session)
    {
        return false;
    }

    *out_session = {};

    platform::esp::boards::AppContextInitHandles handles;
    if (!platform::esp::boards::tryResolveAppContextInitHandles(&handles) || !handles.board)
    {
        return false;
    }

    out_session->impl = handles.board;
    if (!app::AppTasks::areRadioTasksPaused())
    {
        app::AppTasks::pauseRadioTasks();
        out_session->paused_radio_tasks = true;
    }

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
    return resolveBoard(&session) != nullptr;
}

bool isRadioReady(const Session& session)
{
    TLoRaPagerBoard* board = resolveBoard(&session);
    return board && board->isRadioOnline();
}

bool isCodecReady(const Session& session)
{
    TLoRaPagerBoard* board = resolveBoard(&session);
    return board && ((board->getDevicesProbe() & HW_CODEC_ONLINE) != 0);
}

bool configureFsk(Session* session, float freq_mhz, int8_t tx_power, char* error_buffer,
                  size_t error_buffer_size)
{
    TLoRaPagerBoard* board = resolveBoard(session);
    if (!board || !board->isRadioOnline())
    {
        writeError(error_buffer, error_buffer_size, "Radio offline");
        return false;
    }

    if (!board->lock(pdMS_TO_TICKS(200)))
    {
        writeError(error_buffer, error_buffer_size, "Radio lock failed");
        return false;
    }

    board->radio_.standby();
    int state = board->radio_.beginFSK(freq_mhz,
                                       kFskBitRateKbps,
                                       kFskFreqDevKHz,
                                       kFskRxBwKHz,
                                       tx_power,
                                       kFskPreambleLen,
                                       1.6f);
    if (state != RADIOLIB_ERR_NONE)
    {
        Serial.printf("[WALKIE] beginFSK failed state=%d\n", state);
        writeStateError(error_buffer, error_buffer_size, "beginFSK fail", state);
        board->unlock();
        return false;
    }

    uint8_t sync[sizeof(kFskSyncWord)];
    memcpy(sync, kFskSyncWord, sizeof(sync));
    state = board->radio_.setSyncWord(sync, sizeof(sync));
    if (state != RADIOLIB_ERR_NONE)
    {
        Serial.printf("[WALKIE] setSyncWord failed state=%d\n", state);
        writeStateError(error_buffer, error_buffer_size, "setSync fail", state);
        board->unlock();
        return false;
    }

    state = board->radio_.setCRC(2);
    if (state != RADIOLIB_ERR_NONE)
    {
        Serial.printf("[WALKIE] setCRC failed state=%d\n", state);
        writeStateError(error_buffer, error_buffer_size, "setCRC fail", state);
        board->unlock();
        return false;
    }

    state = board->radio_.setPreambleLength(kFskPreambleLen);
    if (state != RADIOLIB_ERR_NONE)
    {
        Serial.printf("[WALKIE] setPreamble failed state=%d\n", state);
        writeStateError(error_buffer, error_buffer_size, "setPre fail", state);
        board->unlock();
        return false;
    }

    board->unlock();
    session->lora_mode_configured = true;
    return true;
}

bool restoreLora(Session* session, char* error_buffer, size_t error_buffer_size)
{
    TLoRaPagerBoard* board = resolveBoard(session);
    if (!board)
    {
        return false;
    }
    if (!session->lora_mode_configured)
    {
        return true;
    }
    if (!board->lock(pdMS_TO_TICKS(200)))
    {
        writeError(error_buffer, error_buffer_size, "Radio lock failed");
        return false;
    }

    int state = board->radio_.begin();
    board->unlock();
    if (state != RADIOLIB_ERR_NONE)
    {
        Serial.printf("[WALKIE] restore LoRa failed state=%d\n", state);
        writeStateError(error_buffer, error_buffer_size, "restore LoRa fail", state);
        return false;
    }

    session->lora_mode_configured = false;
    return true;
}

int codecOpen(Session* session, uint8_t bits_per_sample, uint8_t channels, uint32_t sample_rate)
{
    TLoRaPagerBoard* board = resolveBoard(session);
    if (!board)
    {
        return -1;
    }
    int state = board->codec.open(bits_per_sample, channels, sample_rate);
    if (state == 0)
    {
        session->codec_open = true;
    }
    return state;
}

void codecClose(Session* session)
{
    TLoRaPagerBoard* board = resolveBoard(session);
    if (!board || !session->codec_open)
    {
        return;
    }
    board->codec.close();
    session->codec_open = false;
}

int codecRead(Session* session, uint8_t* buffer, size_t size)
{
    TLoRaPagerBoard* board = resolveBoard(session);
    return board ? board->codec.read(buffer, size) : -1;
}

int codecWrite(Session* session, uint8_t* buffer, size_t size)
{
    TLoRaPagerBoard* board = resolveBoard(session);
    return board ? board->codec.write(buffer, size) : -1;
}

void codecSetVolume(Session* session, uint8_t level)
{
    TLoRaPagerBoard* board = resolveBoard(session);
    if (board)
    {
        board->codec.setVolume(level);
    }
}

void codecSetGain(Session* session, float db_value)
{
    TLoRaPagerBoard* board = resolveBoard(session);
    if (board)
    {
        board->codec.setGain(db_value);
    }
}

void codecSetMute(Session* session, bool enabled)
{
    TLoRaPagerBoard* board = resolveBoard(session);
    if (board)
    {
        board->codec.setMute(enabled);
    }
}

void standby(Session* session)
{
    TLoRaPagerBoard* board = resolveBoard(session);
    if (board)
    {
        board->radio_.standby();
    }
}

int startTransmit(Session* session, const uint8_t* data, size_t size)
{
    TLoRaPagerBoard* board = resolveBoard(session);
    if (!board)
    {
        return -1;
    }
    if (!board->lock(pdMS_TO_TICKS(50)))
    {
        return -1;
    }
    int state = board->radio_.startTransmit(const_cast<uint8_t*>(data), size);
    board->unlock();
    return state;
}

int startReceive(Session* session)
{
    TLoRaPagerBoard* board = resolveBoard(session);
    return board ? board->startRadioReceive() : -1;
}

uint32_t getRadioIrqFlags(Session* session)
{
    TLoRaPagerBoard* board = resolveBoard(session);
    return board ? board->getRadioIrqFlags() : 0;
}

void clearRadioIrqFlags(Session* session, uint32_t flags)
{
    TLoRaPagerBoard* board = resolveBoard(session);
    if (board)
    {
        board->clearRadioIrqFlags(flags);
    }
}

int getPacketLength(Session* session, bool update)
{
    TLoRaPagerBoard* board = resolveBoard(session);
    return board ? board->getRadioPacketLength(update) : -1;
}

int readRadioData(Session* session, uint8_t* buffer, size_t size)
{
    TLoRaPagerBoard* board = resolveBoard(session);
    return board ? board->readRadioData(buffer, size) : -1;
}

void release(Session* session)
{
    if (!session)
    {
        return;
    }

    codecClose(session);
    restoreLora(session);

    if (session->paused_radio_tasks)
    {
        app::AppTasks::resumeRadioTasks();
    }

    *session = {};
}

} // namespace platform::esp::common::walkie_runtime

#else

namespace platform::esp::common::walkie_runtime
{

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

} // namespace platform::esp::common::walkie_runtime

#endif