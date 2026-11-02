#pragma once

#include "platform/linux/oneseg_fec_statistics.h"
#include "platform/linux/oneseg_ofdm_lab.h"
#include "platform/linux/oneseg_ofdm_measurements.h"
#include "platform/linux/oneseg_ofdm_native_configuration.h"
#include "platform/linux/oneseg_receive_worker.h"
#include "platform/linux/oneseg_service_information.h"
#include "platform/shared/radio/transport_stream_programs.h"
#include <string>
#include <vector>

namespace platform::linux_runtime
{
// Owns exactly one decoder child per IQ generation. Open/write/finish are worker
// operations; snapshot/selectProgram may be called by the UI. Destroy only after
// the receive worker has stopped using this sink. No child runs in a shell.
class OneSegDecoderProcess final : public OneSegIqSink
{
  public:
    struct Config
    {
        std::string python = "/usr/bin/python3";
        std::string decoder;
        // KEY=VALUE overrides, for an explicitly selected native private prefix.
        std::vector<std::string> environment;
        unsigned startup_timeout_ms = 15000;
        bool automatic_parameters = true;
        // Must copy into a bounded queue and return promptly; false aborts RX.
        void* ts_context = nullptr;
        bool (*accept_ts)(void*, const uint8_t*, size_t, uint64_t generation) = nullptr;
        OneSegLabSource diagnostic_source = OneSegLabSource::Unavailable;
    };
    enum class Phase : uint8_t
    {
        Idle,
        Starting,
        Running,
        Stopping,
        Stopped,
        Error
    };
    struct Snapshot
    {
        Phase phase = Phase::Idle;
        uint64_t generation = 0, ts_bytes = 0;
        int child_exit_code = -1, child_signal = 0;
        bool forced_kill = false;
        platform::shared::radio::TransportStreamPrograms::Snapshot programmes;
        OneSegServiceInformation::Snapshot service_information;
        OneSegPhaseHistory ofdm_phase;
        OneSegNativeConfiguration native_configuration;
        std::array<OneSegSignalMeasurement, 5> ofdm_signal{};
        OneSegFecHistory fec;
        uint64_t rejected_ofdm_records = 0;
        char message[160]{}, stderr_tail[256]{};
    };
    explicit OneSegDecoderProcess(Config config);
    ~OneSegDecoderProcess() override;
    bool open(unsigned channel, uint64_t generation, const std::atomic_bool& cancel) override;
    bool configureReception(const OneSegReceptionSettings&) override;
    bool configureRecordedIq(uint32_t rate) override;
    bool configureSynthetic();
    bool configureExperiment(const OneSegOfdmExperiment&, uint32_t input_rate);
    bool write(const uint8_t*, size_t, uint64_t first_pair, uint64_t generation,
               const std::atomic_bool& cancel) override;
    bool finish() override;
    bool healthy() const override;
    const char* lastError() const override;
    Snapshot snapshot() const;
    bool selectProgram(uint16_t number);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace platform::linux_runtime
