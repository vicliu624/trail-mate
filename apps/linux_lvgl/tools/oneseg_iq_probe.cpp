#include "platform/desktop/device_options.h"
#include "platform/linux/oneseg_capture_process.h"
#include "platform/linux/oneseg_decoder_process.h"
#include "platform/linux/oneseg_pipe_sink.h"
#include "platform/linux/packet_radio.h"
#if defined(TRAIL_MATE_ONESEG_MEDIA)
#include "platform/linux/oneseg_pcm_output.h"
#endif
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sched.h>
#include <signal.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace
{
#if defined(TRAIL_MATE_ONESEG_MEDIA)
struct MediaRoute
{
    platform::linux_runtime::OneSegMediaWorker* media = nullptr;
    std::atomic_bool armed{false};
    static bool accept(void* context, const uint8_t* data, size_t size, uint64_t generation)
    {
        auto& self = *static_cast<MediaRoute*>(context);
        if (!self.armed.load(std::memory_order_acquire)) return true;
        const bool accepted = self.media->acceptTs(data, size, generation, 3000);
        // Disarming precedes media cancellation. A concurrent final packet is
        // an expected stop, rather than a failed transport queue.
        return accepted || !self.armed.load(std::memory_order_acquire);
    }
};
#endif
volatile sig_atomic_t interrupted = 0;
void stop_signal(int) { interrupted = 1; }
class StopSignals
{
  public:
    StopSignals()
    {
        struct sigaction action
        {
        };
        action.sa_handler = stop_signal;
        sigemptyset(&action.sa_mask);
        if (sigaction(SIGINT, &action, &old_int_) != 0) throw std::runtime_error("Cannot install stop handler");
        if (sigaction(SIGTERM, &action, &old_term_) != 0)
        {
            sigaction(SIGINT, &old_int_, nullptr);
            throw std::runtime_error("Cannot install terminate handler");
        }
    }
    ~StopSignals()
    {
        sigaction(SIGINT, &old_int_, nullptr);
        sigaction(SIGTERM, &old_term_, nullptr);
    }

  private:
    struct sigaction old_int_
    {
    }, old_term_{};
};
unsigned number(const std::string& value, unsigned maximum = 3600)
{
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("Expected an unsigned integer");
    size_t used = 0;
    const auto result = std::stoul(value, &used);
    if (used != value.size() || result > maximum) throw std::runtime_error("Integer outside supported range");
    return static_cast<unsigned>(result);
}
const char* phase(platform::linux_runtime::OneSegReceiveWorker::Phase value)
{
    using Phase = platform::linux_runtime::OneSegReceiveWorker::Phase;
    switch (value)
    {
    case Phase::Idle:
        return "idle";
    case Phase::Starting:
        return "starting";
    case Phase::ReceivingIq:
        return "receiving_iq";
    case Phase::Stopping:
        return "stopping";
    case Phase::Stopped:
        return "stopped";
    case Phase::Error:
        return "error";
    }
    return "error";
}
void report(const platform::linux_runtime::OneSegReceiveWorker::Snapshot& value)
{
    std::fprintf(stderr, "state=%s channel=%u generation=%llu forwarded_pairs=%llu queue_high_water=%u capture_error=%d recovery_error=%d lease_restore_failed=%u message=%s\n",
                 phase(value.phase), value.channel, static_cast<unsigned long long>(value.generation),
                 static_cast<unsigned long long>(value.forwarded_pairs), value.queue_high_water,
                 value.radio.capture_error, value.radio.recovery_error, value.lease_restore_failed ? 1u : 0u, value.message);
}
} // namespace

int main(int argc, char** argv)
{
    using namespace platform::linux_runtime;
    try
    {
        std::string profile, decoder;
        bool media_requested = false;
        int capture_result_fd = -1;
        OneSegReceptionSettings reception;
        unsigned channel = 13, seconds = 0;
        for (int n = 1; n < argc; ++n)
        {
            const std::string key = argv[n];
            if (key == "--help")
            {
                std::puts("Usage: trailmate-linux-oneseg-iq --profile FILE [--channel 13..62] [--seconds 0..3600] [--decoder FILE]\n"
                          "Engineering RX tool: signed IQ8 on stdout; sample rate comes from the device profile.\n"
                          "Stdout must be a decoder pipe; all status goes to stderr.\n"
                          "--decoder launches the real named-profile Python decoder, reads TS/metadata,\n"
                          "and reaps it before radio release. Requires its native backend in the environment.\n"
                          "--media also decodes video and writes real PCM when the media component is built.\n"
                          "--seconds is an owner-lifetime budget including setup; 0 waits for Ctrl-C.\n"
                          "Raw capture child overrides: --sample-rate 450000|500000 --gain-step 0..13\n"
                          "and --spi-clock 8350000|8700000|16000000.\n"
                          "Exit 0 confirms clean stopping, not TV lock or programme playback.");
                return 0;
            }
            if (key == "--media")
            {
                media_requested = true;
                continue;
            }
            if (n + 1 >= argc) throw std::runtime_error("Missing argument value");
            if (key == "--profile") profile = argv[++n];
            else if (key == "--decoder") decoder = argv[++n];
            else if (key == "--channel") channel = number(argv[++n]);
            else if (key == "--seconds") seconds = number(argv[++n]);
            else if (key == "--sample-rate")
            {
                reception.sample_rate_hz = number(argv[++n], 500000);
                reception.enabled = true;
            }
            else if (key == "--gain-step")
            {
                reception.gain_step = number(argv[++n], 13);
                reception.enabled = true;
            }
            else if (key == "--spi-clock")
            {
                reception.spi_clock_hz = number(argv[++n], 16000000);
                reception.enabled = true;
            }
            else if (key == "--capture-result-fd")
            {
                if (number(argv[++n]) != 3) throw std::runtime_error("Capture receipt requires fd3");
                capture_result_fd = 3;
            }
            else throw std::runtime_error("Unknown argument");
        }
        if (profile.empty() || channel < 13 || channel > 62) throw std::runtime_error("A CM0 profile and channel 13..62 are required");
        if (media_requested && decoder.empty()) throw std::runtime_error("--media requires the real --decoder composition");
        if (capture_result_fd >= 0)
        {
            struct stat descriptor
            {
            };
            const char* marker = std::getenv("TRAIL_MATE_ONESEG_CAPTURE_CHILD");
            if (!marker || std::strcmp(marker, "1") || !decoder.empty() || media_requested ||
                fstat(capture_result_fd, &descriptor) || !S_ISFIFO(descriptor.st_mode))
                throw std::runtime_error("Invalid raw capture child invocation");
        }
#if !defined(TRAIL_MATE_ONESEG_MEDIA)
        if (media_requested) throw std::runtime_error("Native 1seg media component is not built");
#endif
        auto options = platform::desktop::loadDeviceOptions(profile);
        if (reception.enabled)
        {
            if (capture_result_fd < 0 || !reception.valid()) throw std::runtime_error("Invalid capture reception settings");
            options.radio.oneseg_sample_rate_hz = reception.sample_rate_hz;
            options.radio.oneseg_gain_step = reception.gain_step;
            const auto clock = std::to_string(reception.spi_clock_hz);
            if (setenv("TRAIL_MATE_ONESEG_DMA_CLOCK", clock.c_str(), 1)) throw std::runtime_error("Cannot configure capture clock");
        }
        const char* simulated = std::getenv("TRAIL_MATE_LORA_SIMULATED");
        const bool simulated_radio = simulated && (!std::strcmp(simulated, "1") || !std::strcmp(simulated, "true") ||
                                                   !std::strcmp(simulated, "TRUE") || !std::strcmp(simulated, "yes") || !std::strcmp(simulated, "YES"));
        if (options.radio.driver != "lr2021" || options.radio.rf_switch_profile != Lr2021RfSwitchProfile::Cm0_16E8 ||
            options.runtime_mode == LinuxRuntimeMode::SimulatorDemo || resolve_runtime_mode() == LinuxRuntimeMode::SimulatorDemo || simulated_radio)
            throw std::runtime_error("A real CM0 LR2021 composition is required");
        StopSignals signals;
        if (capture_result_fd >= 0)
        {
            const long online = sysconf(_SC_NPROCESSORS_ONLN);
            if (online < 2 || online > CPU_SETSIZE) throw std::runtime_error("Raw capture child requires multiple CPUs");
            cpu_set_t cpus;
            CPU_ZERO(&cpus);
            for (long cpu = 0; cpu < online; ++cpu) CPU_SET(cpu, &cpus);
            if (sched_setaffinity(0, sizeof(cpus), &cpus)) throw std::runtime_error("Cannot admit raw capture CPUs");
        }
        PacketRadioCoordinator coordinator(make_lr2021_radio(options.radio));
#if defined(TRAIL_MATE_ONESEG_MEDIA)
        std::unique_ptr<OneSegPcmOutput> television_audio;
        std::unique_ptr<OneSegMediaWorker> television_media;
        MediaRoute routing;
        uint16_t media_programme = 0, media_video_pid = 0x1fff, media_audio_pid = 0x1fff;
        if (media_requested)
        {
            television_audio = std::make_unique<OneSegPcmOutput>();
            television_media = std::make_unique<OneSegMediaWorker>(television_audio.get());
            routing.media = television_media.get();
        }
#endif
        std::unique_ptr<OneSegIqSink> sink;
        OneSegDecoderProcess* process = nullptr;
        if (decoder.empty())
            sink = std::make_unique<OneSegPipeSink>(OneSegPipeSink::Factory{nullptr, [](void*, unsigned, uint64_t, const std::atomic_bool&)
                                                                            { return dup(STDOUT_FILENO); }});
        else
        {
            OneSegDecoderProcess::Config config;
            config.decoder = decoder;
            config.environment.emplace_back("TRAIL_MATE_ONESEG_INPUT_RATE=" + std::to_string(options.radio.oneseg_sample_rate_hz));
#if defined(TRAIL_MATE_ONESEG_MEDIA)
            if (media_requested)
            {
                config.ts_context = &routing;
                config.accept_ts = MediaRoute::accept;
            }
#endif
            auto owned = std::make_unique<OneSegDecoderProcess>(std::move(config));
            process = owned.get();
            sink = std::move(owned);
        }
        // This standalone process has no packet receive worker. The normal app
        // uses make_oneseg_receive_worker and its physical handoff barrier.
        OneSegReceiveWorker worker(coordinator.foreground(), *sink, []
                                   { return true; });
        if (capture_result_fd >= 0)
        {
            // The already-created owner retains the full mask and selects its
            // highest CPU. Keep the reporting thread off that capture CPU.
            cpu_set_t cpus;
            if (sched_getaffinity(0, sizeof(cpus), &cpus)) throw std::runtime_error("Cannot read raw capture affinity");
            int capture_cpu = CPU_SETSIZE - 1;
            while (capture_cpu >= 0 && !CPU_ISSET(capture_cpu, &cpus)) --capture_cpu;
            if (capture_cpu >= 0) CPU_CLR(capture_cpu, &cpus);
            if (!CPU_COUNT(&cpus) || sched_setaffinity(0, sizeof(cpus), &cpus))
                throw std::runtime_error("Cannot isolate raw capture reporting");
        }
        if (!worker.start(channel))
        {
            report(worker.snapshot());
            return 3;
        }
        const auto started = std::chrono::steady_clock::now();
        auto next_report = started;
        bool stop_sent = false;
        bool pipeline_error = false;
        for (;;)
        {
            const auto state = worker.snapshot();
            if (state.phase == OneSegReceiveWorker::Phase::Stopped || state.phase == OneSegReceiveWorker::Phase::Error) break;
            const auto now = std::chrono::steady_clock::now();
#if defined(TRAIL_MATE_ONESEG_MEDIA)
            if (media_requested && !stop_sent)
            {
                const auto decoded = process->snapshot();
                if (decoded.programmes.pmt_valid)
                {
                    OneSegMediaWorker::Config config;
                    config.generation = state.generation;
                    config.programme_number = decoded.programmes.selected_program;
                    config.pmt_pid = decoded.programmes.pmt_pid;
                    config.transport_id = decoded.programmes.transport_id;
                    for (size_t i = 0; i < decoded.programmes.stream_count; ++i)
                    {
                        const auto stream = decoded.programmes.streams[i];
                        if (config.video_pid == 0x1fff && platform::shared::radio::TransportStreamPrograms::video(stream.type)) config.video_pid = stream.pid;
                        if (config.audio_pid == 0x1fff && platform::shared::radio::TransportStreamPrograms::audio(stream.type)) config.audio_pid = stream.pid;
                    }
                    if (!routing.armed.load())
                    {
                        if (television_media->start(config))
                        {
                            media_programme = decoded.programmes.selected_program;
                            media_video_pid = config.video_pid;
                            media_audio_pid = config.audio_pid;
                            routing.armed.store(true, std::memory_order_release);
                        }
                        else pipeline_error = true;
                    }
                    else if (media_programme != decoded.programmes.selected_program ||
                             media_video_pid != config.video_pid || media_audio_pid != config.audio_pid)
                    {
                        std::fprintf(stderr, "media_error=Received programme mapping changed; restart this engineering session\n");
                        pipeline_error = true;
                    }
                }
                if (routing.armed.load() && !television_media->healthy()) pipeline_error = true;
                if (pipeline_error)
                {
                    routing.armed = false;
                    television_media->requestStop();
                    worker.requestStop();
                    stop_sent = true;
                }
            }
#endif
            if (!stop_sent && (interrupted || (seconds && now - started >= std::chrono::seconds(seconds))))
            {
#if defined(TRAIL_MATE_ONESEG_MEDIA)
                if (media_requested)
                {
                    routing.armed = false;
                    television_media->requestStop();
                }
#endif
                worker.requestStop();
                stop_sent = true;
            }
            if (now >= next_report)
            {
                report(state);
                if (process)
                {
                    const auto decoded = process->snapshot();
                    std::fprintf(stderr, "decoder_ts_bytes=%llu programme_count=%zu selected_programme=%u pmt_valid=%u decoder_message=%s\n",
                                 static_cast<unsigned long long>(decoded.ts_bytes), decoded.programmes.program_count,
                                 decoded.programmes.selected_program, decoded.programmes.pmt_valid ? 1u : 0u, decoded.message);
                }
#if defined(TRAIL_MATE_ONESEG_MEDIA)
                if (media_requested)
                {
                    const auto media = television_media->snapshot();
                    std::fprintf(stderr, "decoded_video_frames=%llu accepted_pcm_samples=%llu media_decode_errors=%llu media_error=%u\n",
                                 static_cast<unsigned long long>(media.video_frames), static_cast<unsigned long long>(media.pcm_samples),
                                 static_cast<unsigned long long>(media.decode_errors), unsigned(media.error));
                }
#endif
                next_report = now + std::chrono::seconds(1);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        worker.waitUntilFinished();
#if defined(TRAIL_MATE_ONESEG_MEDIA)
        if (media_requested)
        {
            routing.armed = false;
            if (pipeline_error) television_media->requestStop();
            else television_media->endInput();
            television_media->join();
            const auto media = television_media->snapshot();
            pipeline_error = pipeline_error || media.error != OneSegMediaWorker::Error::None;
            std::fprintf(stderr, "media_final_video_frames=%llu media_final_pcm_samples=%llu media_message=%s\n",
                         static_cast<unsigned long long>(media.video_frames), static_cast<unsigned long long>(media.pcm_samples), media.message);
        }
#endif
        const auto final = worker.snapshot();
        report(final);
        if (capture_result_fd >= 0)
        {
            auto radio = final.radio;
            if (final.phase != OneSegReceiveWorker::Phase::Stopped && !radio.capture_error) radio.capture_error = -5;
            const auto receipt = encode_oneseg_capture_result(radio);
            ssize_t written;
            do
            {
                written = ::write(capture_result_fd, receipt.data(), receipt.size());
            } while (written < 0 && errno == EINTR);
            close(capture_result_fd);
            if (written != static_cast<ssize_t>(receipt.size())) return 3;
        }
        if (process)
        {
            const auto decoded = process->snapshot();
            std::fprintf(stderr, "decoder_exit=%d decoder_signal=%d decoder_forced_kill=%u decoder_message=%s decoder_stderr_tail=%s\n",
                         decoded.child_exit_code, decoded.child_signal, decoded.forced_kill ? 1u : 0u, decoded.message, decoded.stderr_tail);
        }
        return final.phase == OneSegReceiveWorker::Phase::Stopped && !pipeline_error ? 0 : 3;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "oneseg_iq_error=%s\n", error.what());
        return 2;
    }
}
