#pragma once
#include <cstddef>
#include <cstdint>
namespace platform::shared::radio
{
struct FmIqRequest;
struct FmIqResult;
} // namespace platform::shared::radio

namespace platform::esp::idf_common
{
// Packet interface uses the existing IDF logical IRQ bits, never raw LR2021
// masks. All users share this singleton, its lock, SPI transport and leases.
class Lr2021Radio
{
  public:
    static Lr2021Radio& instance();
    bool acquire();
    void release();
    bool isOnline() const;
    bool configureLoRaReceive(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr,
                              int8_t tx_power, uint16_t preamble_len, uint8_t sync_word, uint8_t crc_len);
    bool configureFsk(float freq_mhz, int8_t tx_power, float bit_rate_kbps, float freq_dev_khz,
                      float rx_bw_khz, uint16_t preamble_len, const uint8_t* sync_word,
                      size_t sync_word_len, uint8_t crc_len);
    bool startReceive();
    bool standby();
    float readRssi();
    float readSnr();
    int startTransmit(const uint8_t* data, size_t size);
    uint32_t getIrqFlags();
    void clearIrqFlags(uint32_t flags);
    int getPacketLength(bool update);
    int readPacket(uint8_t* buffer, size_t size);
    struct IqCaptureResult
    {
        size_t bytes = 0;
        uint32_t nominal_rate_hz = 0; // Zero means the finite route's rate is unqualified.
        uint64_t elapsed_us = 0;
        int capture_error = 0;
        int restore_error = 0;
        int recovery_error = 0;
    };
    // Diagnostic finite capture, not continuous audio. Caller must own the
    // foreground radio and pause AppTasks first. Both non-overlapping buffers
    // need >=256 words in persistent/caller-owned storage. Samples are valid
    // only on true, after SRAM verification and prior radio state restoration.
    bool captureIq(uint32_t* words, uint32_t* scratch, size_t capacity_words, IqCaptureResult& result);
    // Synchronous worker-only experimental FM RX. Caller owns the foreground
    // lease, has paused AppTasks and supplies persistent staging/cancellation.
    // Holds the physical mutex until checked FIFO recovery and restoration of
    // the committed LoRa/FSK packet mode and its RX/standby state.
    bool streamFmIq(const platform::shared::radio::FmIqRequest&, platform::shared::radio::FmIqResult&);
    const char* lastError() const;

  private:
    Lr2021Radio() = default;
};
} // namespace platform::esp::idf_common
