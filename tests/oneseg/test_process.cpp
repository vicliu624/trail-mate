#include "platform/linux/oneseg_decoder_process.h"
#include "platform/linux/packet_radio.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using Process = platform::linux_runtime::OneSegDecoderProcess;
using Clock = std::chrono::steady_clock;
static std::string fixture;
static std::atomic_bool cancel{false};
static Process::Config config(const char* mode = "echo")
{
    Process::Config result;
    result.decoder = fixture;
    result.startup_timeout_ms = 3000;
    result.environment = {std::string("TRAIL_MATE_ONESEG_TEST_MODE=") + mode,
                          "TRAIL_MATE_ONESEG_TEST_LITERAL=literal;$(not-a-command)"};
    return result;
}
template <typename Predicate>
static void await(Predicate predicate)
{
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!predicate() && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    assert(predicate());
}
static void no_children()
{
    int status = 0;
    assert(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
}
static void profile_selection()
{
    auto research = config();
    research.automatic_parameters = false;
    research.environment.emplace_back("TRAIL_MATE_ONESEG_TEST_EXPECT_PROFILE=onesegcast-mode3-gi16-qpsk23-ti4");
    Process process(research);
    assert(process.open(13, 1, cancel));
    assert(process.finish());
    assert(process.snapshot().child_exit_code == 0);
    no_children();
}
static size_t fd_count()
{
    size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd"))
    {
        (void)entry;
        ++count;
    }
    return count;
}
static void restart_and_transport()
{
    const size_t before = fd_count();
    {
        Process process(config());
        for (unsigned session = 1; session <= 3; ++session)
        {
            assert(process.open(13, session, cancel) && process.healthy());
            assert(!process.open(13, session + 1, cancel) && process.healthy());
            std::array<uint8_t, 1280> iq{};
            assert(process.write(iq.data(), iq.size(), 0, session, cancel));
            await([&]
                  { return process.snapshot().programmes.pmt_valid; });
            const auto state = process.snapshot();
            assert(state.generation == session && state.programmes.selected_program == 24);
            assert(state.programmes.streams[0].pid == 0x530 && state.programmes.streams[1].pid == 0x531);
            assert(!state.programmes.pat_valid && state.ts_bytes == 188);
            assert(!process.selectProgram(8) && process.selectProgram(24));
            assert(process.finish() && !process.healthy());
            assert(process.snapshot().child_exit_code == 0 && process.snapshot().phase == Process::Phase::Stopped);
            assert(process.finish());
            no_children();
        }
    }
    assert(fd_count() == before);
}
static void startup_and_failure()
{
    Process early(config("early-exit"));
    assert(!early.open(13, 1, cancel));
    assert(early.snapshot().phase == Process::Phase::Error && early.snapshot().child_exit_code == 9);
    assert(std::strstr(early.snapshot().stderr_tail, "backend unavailable"));
    no_children();
    auto short_config = config("no-ready");
    short_config.startup_timeout_ms = 100;
    Process timeout(short_config);
    assert(!timeout.open(13, 2, cancel));
    assert(std::strstr(timeout.lastError(), "timed out"));
    no_children();
    Process cancelled(config("no-ready"));
    std::thread stop([]
                     { std::this_thread::sleep_for(std::chrono::milliseconds(100)); cancel = true; });
    assert(!cancelled.open(13, 3, cancel));
    stop.join();
    cancel = false;
    assert(cancelled.snapshot().phase == Process::Phase::Stopped);
    no_children();
    Process died(config("die-after-ready"));
    assert(died.open(13, 4, cancel));
    await([&]
          { return !died.healthy(); });
    assert(!died.finish() && died.snapshot().child_exit_code == 7);
    no_children();
    Process stubborn(config("ignore-term"));
    assert(stubborn.open(13, 5, cancel));
    assert(!stubborn.finish() && stubborn.snapshot().forced_kill && stubborn.snapshot().child_signal == SIGKILL);
    no_children();
}
static void pipe_isolation()
{
    const int temporary = ::open("/dev/null", O_RDONLY);
    assert(temporary >= 0);
    // Descriptor 3 belongs to the child handshake. Check a separate high FD,
    // independent of which descriptors the CTest launcher inherited.
    const int unrelated = fcntl(temporary, F_DUPFD, 64); // Intentionally lacks CLOEXEC.
    ::close(temporary);
    assert(unrelated >= 64);
    auto options = config("flood");
    options.environment.push_back("TRAIL_MATE_ONESEG_TEST_FD=" + std::to_string(unrelated));
    Process process(options);
    assert(process.open(13, 7, cancel));
    assert(std::strlen(process.snapshot().stderr_tail) <= 255);
    assert(std::strstr(process.snapshot().stderr_tail, "stderr drained"));
    assert(fcntl(unrelated, F_GETFD) >= 0); // Closing is child-local.
    assert(process.finish());
    ::close(unrelated);
    for (const char* mode : {"invalid-ts", "partial-ts"})
    {
        Process corrupt(config(mode));
        (void)corrupt.open(13, 8, cancel);
        await([&]
              { return !corrupt.healthy(); });
        assert(!corrupt.finish() && corrupt.snapshot().phase == Process::Phase::Error);
    }
    auto rejected = config();
    rejected.accept_ts = [](void*, const uint8_t*, size_t, uint64_t)
    { return false; };
    Process full(rejected);
    assert(full.open(13, 9, cancel));
    std::array<uint8_t, 1280> iq{};
    assert(full.write(iq.data(), iq.size(), 0, 9, cancel));
    await([&]
          { return !full.healthy(); });
    assert(std::strstr(full.lastError(), "queue rejected") && !full.finish());
    no_children();
}
static void actual_archive_backend(Process::Config options, const char* recorded_iq);
static void actual_backend(const char* decoder, const char* prefix, const char* recorded_iq = nullptr)
{
    auto options = config();
    options.decoder = decoder;
    options.environment.clear();
    std::string python, libraries;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(prefix))
    {
        const auto name = entry.path().filename().string();
        if (entry.is_directory() && (name == "site-packages" || name == "dist-packages")) python += entry.path().string() + ":";
        if (entry.is_regular_file() && name.find("libgnuradio-isdbt.so") == 0) libraries += entry.path().parent_path().string() + ":";
    }
    assert(!python.empty() && !libraries.empty());
    options.environment = {"PYTHONPATH=" + python, "LD_LIBRARY_PATH=" + libraries};
    const bool compact = std::getenv("TRAIL_MATE_TEST_COMPACT");
    if (compact) options.environment.emplace_back("TRAIL_MATE_ONESEG_COMPACT_FFT=576");
    const bool tracking = recorded_iq && std::getenv("TRAIL_MATE_TEST_OFDM_TRACKING");
    if (tracking)
    {
        options.environment.push_back("TRAIL_MATE_OFDM_DIAGNOSTICS=1");
        options.diagnostic_source = platform::linux_runtime::OneSegLabSource::RecordedIq;
    }
    Process process(options);
    if (tracking) assert(process.configureRecordedIq(576000));
    assert(process.open(13, 11, cancel));
    std::array<uint8_t, 1280> zero{};
    if (recorded_iq)
    {
        std::ifstream input(recorded_iq, std::ios::binary);
        assert(input);
        uint64_t pairs = 0;
        while (input.read(reinterpret_cast<char*>(zero.data()), zero.size()) || input.gcount())
        {
            const size_t count = static_cast<size_t>(input.gcount());
            assert(count % 2 == 0 && process.write(zero.data(), count, pairs, 11, cancel));
            pairs += count / 2;
            // Match real signed IQ8 throughput during automatic acquisition.
            std::this_thread::sleep_for(std::chrono::microseconds(count * 1000000ULL / 1152000));
        }
        assert(pairs > 0);
    }
    else
        for (unsigned i = 0; i < 16; ++i) assert(process.write(zero.data(), zero.size(), i * 640, 11, cancel));
    assert(process.finish());
    const auto state = process.snapshot();
    assert(state.child_exit_code == 0);
    if (recorded_iq)
    {
        assert(state.ts_bytes >= 188 * 50 && state.programmes.pmt_valid && !state.programmes.pat_valid);
        assert(state.programmes.selected_program == 8 && state.programmes.stream_count == 2);
        assert(state.programmes.streams[0].pid == 256 && state.programmes.streams[1].pid == 257);
        if (tracking)
        {
            assert(state.ofdm_phase.size >= 5 && state.rejected_ofdm_records == 0);
            assert(state.ofdm_phase.source == platform::linux_runtime::OneSegLabSource::RecordedIq);
            assert(state.fec.size >= 2 && state.fec.newest().valid());
            assert(state.fec.newest().words + 63 >= state.ts_bytes / 188);
            for (size_t i = 0; i < state.ofdm_phase.size; ++i)
                assert(state.ofdm_phase.chronological(i).has_prediction && state.ofdm_phase.chronological(i).valid());
            const auto& native = state.native_configuration;
            assert(native.available && native.valid() && native.source == platform::linux_runtime::OneSegLabSource::RecordedIq);
            assert(native.mode == 3 && native.fft_length == (compact ? 576 : 1024) && native.cp_length == (compact ? 36 : 64) && native.carriers == 432);
            assert(native.channel_average == 1 && native.equalizer_floor == 0 && native.integer_search == 10 && native.offset_confirmation == 4);
            if (std::getenv("TRAIL_MATE_TEST_CARRIERS"))
            {
                assert(state.ofdm_carriers && state.ofdm_carriers->source == platform::linux_runtime::OneSegLabSource::RecordedIq);
                const auto& burst = *state.ofdm_carriers;
                for (size_t symbol = 0; symbol < burst.symbols.size(); ++symbol)
                {
                    const auto& frame = burst.symbols[symbol];
                    assert(frame.metadata.valid() && frame.metadata.slot == symbol && frame.metadata.count == 432);
                    assert(frame.metadata.fft == (compact ? 576 : 1024));
                    assert(frame.metadata.burst == burst.symbols[0].metadata.burst);
                    if (symbol) assert(frame.metadata.sequence == burst.symbols[symbol - 1].metadata.sequence + 1);
                    for (size_t carrier = 0; carrier < frame.metadata.count; ++carrier)
                    {
                        const auto& point = frame.values[carrier].values;
                        const std::complex<double> raw(point[0], point[1]), equalized(point[2], point[3]), h(point[4], point[5]);
                        assert(std::abs(raw - equalized * h) < 5e-5 * (1 + std::abs(raw)));
                    }
                }
                std::fprintf(stderr, "Actual coherent carrier IPC: four consecutive symbols, 432 carriers each\n");
            }
            assert(std::abs(native.alpha - state.ofdm_phase.newest().alpha) < 1e-7 && std::abs(native.beta - state.ofdm_phase.newest().beta) < 1e-7);
            for (size_t i = 0; i < state.ofdm_signal.size(); ++i)
            {
                const auto& signal = state.ofdm_signal[i];
                assert(signal.valid() && signal.kind == i + 1 && signal.count >= 32);
                assert(signal.source == platform::linux_runtime::OneSegLabSource::RecordedIq);
                std::fprintf(stderr, "Actual signal IPC: kind=%u sequence=%llu points=%zu metric=%.6g\n",
                             signal.kind, (unsigned long long)signal.sequence, signal.count, signal.metric);
            }
            std::fprintf(stderr, "Actual decoder tracking IPC: %zu retained observations, last symbol=%llu\n",
                         state.ofdm_phase.size, (unsigned long long)state.ofdm_phase.newest().symbol);
        }
    }
    else
    {
        assert(state.ts_bytes == 0 && !state.programmes.pmt_valid);
        Process quiet(options);
        assert(quiet.open(13, 12, cancel));
        for (unsigned i = 0; i < 900; ++i)
            assert(quiet.write(zero.data(), zero.size(), i * 640, 12, cancel));
        await([&]
              { return !quiet.healthy(); });
        assert(!quiet.finish());
        const auto absent = quiet.snapshot();
        if (absent.child_exit_code != 4 || absent.ts_bytes || absent.programmes.pmt_valid)
            std::fprintf(stderr, "No-signal lifecycle: exit=%d signal=%d phase=%u bytes=%llu pmt=%d message=%s stderr=%s\n",
                         absent.child_exit_code, absent.child_signal, static_cast<unsigned>(absent.phase),
                         static_cast<unsigned long long>(absent.ts_bytes), absent.programmes.pmt_valid, absent.message, absent.stderr_tail);
        assert(absent.child_exit_code == 4 && !absent.ts_bytes && !absent.programmes.pmt_valid);
    }
    no_children();
    if (tracking) actual_archive_backend(options, recorded_iq);
}
class EmptyRadio final : public platform::linux_runtime::IPacketRadio
{
  public:
    Process& process;
    std::atomic_uint acquisitions{0}, restorations{0};
    explicit EmptyRadio(Process& value) : process(value) {}
    const char* name() const override { return "TEST ONLY - EMPTY FIFO"; }
    bool hardwareCandidatePresent() const override { return true; }
    bool supportsOneSegIq() const override { return true; }
    bool acquire() override
    {
        ++acquisitions;
        return true;
    }
    void release() override { (void)releaseWithStatus(); }
    bool releaseWithStatus() override
    {
        const auto state = process.snapshot();
        assert(state.child_exit_code >= 0 || state.child_signal != 0);
        ++restorations;
        return true;
    }
    bool configureLoRa(const platform::linux_runtime::RadioSettings&) override { return true; }
    bool startReceive() override { return true; }
    bool transmit(const uint8_t*, size_t) override
    {
        assert(false);
        return false;
    }
    bool pollReceive(platform::linux_runtime::RadioPacket*) override { return false; }
    float readRssi() override { return 0; }
    bool isOnline() const override { return true; }
    const char* lastError() const override { return "empty FIFO fixture"; }
    platform::linux_runtime::RadioSettings appliedLoRaConfig() const override { return {}; }
    platform::linux_runtime::RadioStatistics stats() const override { return {}; }
    bool streamOneSegIq(const platform::linux_runtime::OneSegIqRequest& request,
                        platform::linux_runtime::OneSegIqResult& result) override
    {
        while (!request.cancelled(request.context)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        result.cancelled = true;
        return true;
    }
};
static void actual_archive_backend(Process::Config options, const char* recorded_iq)
{
    using namespace platform::linux_runtime;
    OneSegIqArchive archive;
    OneSegIqArchive::Metadata metadata;
    metadata.rate = 576000;
    assert(archive.begin(metadata, std::filesystem::file_size(recorded_iq)));
    std::ifstream input(recorded_iq, std::ios::binary);
    std::array<uint8_t, 2048> block{};
    uint64_t first = 0;
    while (input.read(reinterpret_cast<char*>(block.data()), block.size()) || input.gcount())
    {
        const auto count = static_cast<size_t>(input.gcount());
        assert(archive.append(block.data(), count, first));
        first += count / 2;
    }
    const std::string path = "/tmp/trailmate-native-replay-" + std::to_string(getpid()) + ".tmiq";
    std::string error;
    assert(archive.save(path, error));
    Process process(std::move(options));
    EmptyRadio radio(process);
    OneSegReceiveWorker worker(radio, process, []
                               { assert(false); return false; });
    OneSegReceiveWorker::Source source;
    source.archive_path = path;
    assert(worker.start(13, {}, source));
    worker.waitUntilFinished();
    const auto received = worker.snapshot();
    const auto decoded = process.snapshot();
    if (received.error != OneSegReceiveWorker::Error::None)
        std::fprintf(stderr, "Archive replay failed: %s / %s / %s\n", received.message, decoded.message, decoded.stderr_tail);
    assert(received.phase == OneSegReceiveWorker::Phase::Stopped && received.recorded_source);
    assert(received.radio.validated_bytes == archive.size() && received.forwarded_pairs == archive.size() / 2);
    assert(!radio.acquisitions && !radio.restorations);
    assert(decoded.ts_bytes >= 188 * 50 && decoded.programmes.pmt_valid && decoded.programmes.selected_program == 8);
    assert(decoded.ofdm_phase.source == OneSegLabSource::RecordedIq && decoded.ofdm_phase.size >= 5);
    for (const auto& signal : decoded.ofdm_signal) assert(signal.valid() && signal.source == OneSegLabSource::RecordedIq);
    assert(!decoded.rejected_ofdm_records);
    std::fprintf(stderr, "Actual paced archive replay: IQ=%llu TS=%llu radio leases=0 tracking=%zu\n",
                 (unsigned long long)received.radio.validated_bytes, (unsigned long long)decoded.ts_bytes, decoded.ofdm_phase.size);
    assert(::unlink(path.c_str()) == 0);
}
static void source_configuration_scope()
{
    auto options = config("phase-telemetry");
    options.environment.emplace_back("TRAIL_MATE_ONESEG_INPUT_RATE=500000");
    Process process(options);
    assert(!process.configureRecordedIq(12345));
    assert(process.configureRecordedIq(576000));
    assert(process.open(13, 70, cancel));
    assert(!process.configureRecordedIq(450000));
    await([&]
          { return process.snapshot().ofdm_phase.size == 1; });
    assert(std::strstr(process.snapshot().stderr_tail, "input-rate=576000"));
    assert(process.finish());
    assert(process.configureReception({}));
    assert(process.open(13, 71, cancel));
    await([&]
          { return std::strstr(process.snapshot().stderr_tail, "fixture after tracking"); });
    assert(std::strstr(process.snapshot().stderr_tail, "input-rate=500000"));
    assert(process.snapshot().ofdm_phase.size == 0); // Original unavailable source restored.
    assert(process.finish());
}
static void empty_fifo_decoder_failure()
{
    using Worker = platform::linux_runtime::OneSegReceiveWorker;
    Process process(config("die-after-ready"));
    EmptyRadio radio(process);
    Worker worker(radio, process, []
                  { return true; });
    assert(worker.start(13));
    await([&]
          { return worker.snapshot().phase == Worker::Phase::Error; });
    worker.waitUntilFinished();
    const auto state = worker.snapshot();
    assert(state.error == Worker::Error::SinkWrite && !state.radio.validated_bytes);
    assert(!state.lease_restore_failed && radio.acquisitions == 1 && radio.restorations == 1);
    assert(process.snapshot().child_exit_code == 7);
    no_children();
    Process early(config("early-exit"));
    EmptyRadio untouched(early);
    Worker stopped(untouched, early, []
                   { return true; });
    assert(stopped.start(13));
    await([&]
          { return stopped.snapshot().phase == Worker::Phase::Error; });
    stopped.waitUntilFinished();
    assert(untouched.acquisitions == 0 && untouched.restorations == 0);
    no_children();
}
static void metadata_transport()
{
    Process process(config("metadata"));
    assert(process.open(13, 1, cancel));
    std::array<uint8_t, 1280> iq{};
    assert(process.write(iq.data(), iq.size(), 0, 1, cancel));
    await([&]
          { const auto state = process.snapshot(); return state.service_information.service_valid && state.service_information.present.valid; });
    const auto received = process.snapshot();
    assert(received.ts_bytes == 3 * 188);
    assert(received.service_information.service == received.programmes.selected_program);
    assert(received.service_information.name.size == 5 && received.service_information.name.bytes[1] == 'T');
    assert(received.service_information.present.title.size == 5 && received.service_information.present.title.bytes[1] == 'N');
    assert(process.finish());
    assert(!process.snapshot().service_information.service_valid && !process.snapshot().service_information.present.valid);
    assert(process.open(14, 2, cancel));
    const auto restarted = process.snapshot();
    assert(!restarted.service_information.service_valid && !restarted.service_information.present.valid);
    assert(process.finish());
    no_children();
}
static void reception_rate()
{
    const char* inherited = std::getenv("TRAIL_MATE_ONESEG_INPUT_RATE");
    const std::string original = inherited ? inherited : "";
    auto options = config();
    options.environment.push_back("TRAIL_MATE_ONESEG_INPUT_RATE=576000");
    Process process(options);
    platform::linux_runtime::OneSegReceptionSettings settings;
    settings.enabled = true;
    for (auto rate : {450000u, 500000u, 576000u})
    {
        settings.enabled = rate != 576000;
        settings.sample_rate_hz = rate;
        const auto expected = std::to_string(rate);
        setenv("TRAIL_MATE_ONESEG_TEST_EXPECT_INPUT_RATE", expected.c_str(), 1);
        assert(process.configureReception(settings));
        assert(process.open(13, rate, cancel));
        assert(!process.configureReception(settings));
        assert(process.finish());
        const char* current = std::getenv("TRAIL_MATE_ONESEG_INPUT_RATE");
        assert(std::string(current ? current : "") == original);
        no_children();
    }
    unsetenv("TRAIL_MATE_ONESEG_TEST_EXPECT_INPUT_RATE");
}
static void phase_telemetry()
{
    auto options = config("phase-telemetry");
    options.diagnostic_source = platform::linux_runtime::OneSegLabSource::RecordedIq;
    Process process(options);
    for (unsigned generation = 1; generation <= 2; ++generation)
    {
        assert(process.open(13, generation, cancel));
        await([&]
              { return process.snapshot().rejected_ofdm_records == 2; });
        const auto state = process.snapshot();
        assert(state.generation == generation && state.ofdm_phase.size == 1 && state.ts_bytes == 0);
        assert(state.ofdm_phase.source == platform::linux_runtime::OneSegLabSource::RecordedIq);
        assert(state.ofdm_phase.newest().symbol == 32 && state.ofdm_phase.newest().frequency_hz == 150);
        assert(std::strstr(state.stderr_tail, "TM_OFDM1") == nullptr);
        assert(process.finish());
        no_children();
    }
}
static void synthetic_process(const char* decoder)
{
    auto options = config();
    options.decoder = decoder;
    options.environment.clear();
    Process process(options);
    platform::linux_runtime::OneSegOfdmExperiment experiment;
    experiment.synthetic_cfo_hz = 0;
    experiment.synthetic_snr_db = 100;
    double clean = 0;
    for (unsigned trial = 0; trial < 2; ++trial)
    {
        experiment.synthetic_snr_db = trial ? 10 : 100;
        assert(process.configureExperiment(experiment, 64000000 / 63));
        assert(process.configureSynthetic());
        assert(process.open(13, trial + 100, cancel));
        await([&]
              { const auto s = process.snapshot(); return s.ofdm_phase.size >= 8 && s.ofdm_signal[3].count; });
        const auto state = process.snapshot();
        assert(!state.ts_bytes && !state.programmes.pmt_valid && !state.native_configuration.available);
        assert(state.ofdm_phase.source == platform::linux_runtime::OneSegLabSource::Synthetic);
        assert(state.ofdm_phase.newest().has_reference && state.ofdm_phase.newest().valid());
        assert(state.rejected_ofdm_records == 0);
        assert(state.ofdm_carriers && state.ofdm_carriers->source == platform::linux_runtime::OneSegLabSource::Synthetic);
        for (const auto& symbol : state.ofdm_carriers->symbols)
            assert(symbol.metadata.valid() && symbol.metadata.layout == 1 && symbol.metadata.count == 432);
        if (!trial)
        {
            clean = state.ofdm_phase.newest().reference_evm;
            assert(clean < .01);
        }
        else assert(state.ofdm_phase.newest().reference_evm > clean * 5);
        assert(process.finish());
        no_children();
    }
}
int main(int argc, char** argv)
{
    assert(argc == 2 || argc == 3 || argc == 4 || argc == 5);
    fixture = argv[1];
    if (argc == 3) synthetic_process(argv[2]);
    else if (argc >= 4) actual_backend(argv[2], argv[3], argc == 5 ? argv[4] : nullptr);
    else
    {
        phase_telemetry();
        reception_rate();
        profile_selection();
        restart_and_transport();
        metadata_transport();
        startup_and_failure();
        pipe_isolation();
        source_configuration_scope();
        empty_fifo_decoder_failure();
    }
}
