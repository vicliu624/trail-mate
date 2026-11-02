#include "platform/linux/oneseg_tv_session.h"
#include "platform/linux/oneseg_pcm_output.h"
#include "platform/linux/oneseg_telemetry.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unistd.h>

namespace platform::linux_runtime
{
struct OneSegTvSession::Impl
{
    using Clock = std::chrono::steady_clock;
    mutable std::mutex mutex;
    std::condition_variable changed;
    Snapshot state;
    bool reception_tuning = false;
    OneSegReceptionSettings reception;
    OneSegOfdmExperiment experiment;
    OneSegReceiveWorker::Source source;
    std::string iq_export_path;
    bool iq_export_pending = false;
    uint64_t iq_export_serial = 0;
    std::string iq_catalog_directory;
    bool iq_catalog_pending = false;
    uint64_t iq_catalog_serial = 0;
    bool ofdm_experiments = false;
    bool pending = false, shutdown = false;
    uint16_t width = 320, height = 180, selection = 0;
    std::atomic_bool stop{false}, armed{false};
    std::atomic_uint callbacks{0};
    uint64_t routed_rf = 0, routed_media = 0, epoch_sequence = 0;
    uint64_t copied_ordinal = 0, acknowledged_ordinal = 0, observed_video = 0, observed_pairs = 0;
    Clock::time_point iq_progress{}, video_progress{}, presentation{};
    Clock::time_point video_anchor{};
    Clock::time_point video_observed_at{};
    uint64_t video_observed_ordinal = 0;
    int64_t video_observed_pts = INT64_MIN;
    int64_t video_anchor_pts = INT64_MIN;
    int64_t copied_pts_us = INT64_MIN, copied_delta_us = INT64_MIN;
    bool has_audio = false;
    OneSegPcmOutput audio;
    OneSegMediaWorker media{&audio};
    OneSegDecoderProcess decoder;
    OneSegReceiveWorker receive;
    OneSegServiceTextCache service_text;
    const bool telemetry_enabled = []
    {
        const char* value = std::getenv("TRAIL_MATE_ONESEG_TELEMETRY");
        return value && std::strcmp(value, "1") == 0;
    }();
    Clock::time_point telemetry_due{};
    std::thread owner;

    void logTelemetry(bool force = false)
    {
        if (!telemetry_enabled) return;
        const auto now = Clock::now();
        if (!force && now < telemetry_due) return;
        telemetry_due = now + std::chrono::seconds(1);
        char output[2048];
        char error_detail[160]{};
        uint64_t error_generation = 0;
        int bytes;
        {
            std::lock_guard<std::mutex> lock(mutex);
            bytes = format_oneseg_telemetry(output, sizeof(output), state,
                                            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()),
                                            age(presentation), audio.clock(), static_cast<uint64_t>(getpid()));
            if (force && state.error != Error::None)
            {
                std::snprintf(error_detail, sizeof(error_detail), "%s", state.message);
                error_generation = state.generation;
            }
        }
        // Optional diagnostic I/O stays on the session owner, outside its state
        // lock and outside the UI, PCM callback and realtime FIFO producer.
        if (bytes) (void)std::fwrite(output, 1, static_cast<size_t>(bytes), stderr);
        if (error_detail[0])
            std::fprintf(stderr, "[oneseg_error pid=%llu gen=%llu] %s\n", (unsigned long long)getpid(),
                         (unsigned long long)error_generation, error_detail);
    }

    struct Mapping
    {
        uint16_t programme = 0, pmt = 0, video = 0x1fff, audio = 0x1fff, pcr = 0x1fff;
        uint8_t version = 0, video_type = 0, audio_type = 0;
        bool operator==(const Mapping& other) const
        {
            return programme == other.programme && pmt == other.pmt && video == other.video && audio == other.audio &&
                   pcr == other.pcr && version == other.version && video_type == other.video_type && audio_type == other.audio_type;
        }
        bool supported() const { return video != 0x1fff || audio != 0x1fff; }
    };
    static Mapping mapping(const OneSegDecoderProcess::Snapshot& snapshot)
    {
        Mapping result;
        const auto& table = snapshot.programmes;
        if (!table.pmt_valid) return result;
        result.programme = table.selected_program;
        result.pmt = table.pmt_pid;
        result.pcr = table.pcr_pid;
        result.version = table.pmt_version;
        for (size_t i = 0; i < table.stream_count; ++i)
        {
            const auto stream = table.streams[i];
            if (result.video == 0x1fff && platform::shared::radio::TransportStreamPrograms::video(stream.type))
            {
                result.video = stream.pid;
                result.video_type = stream.type;
            }
            if (result.audio == 0x1fff && platform::shared::radio::TransportStreamPrograms::audio(stream.type))
            {
                result.audio = stream.pid;
                result.audio_type = stream.type;
            }
        }
        return result;
    }
    OneSegDecoderProcess::Config bind(Config config)
    {
        OneSegDecoderProcess::Config result;
        result.python = std::move(config.python);
        result.decoder = std::move(config.decoder);
        result.environment = std::move(config.environment);
        result.diagnostic_source = config.diagnostic_source;
        if (config.diagnostic_source != OneSegLabSource::Unavailable)
            result.environment.emplace_back("TRAIL_MATE_OFDM_DIAGNOSTICS=1");
        result.startup_timeout_ms = config.startup_timeout_ms;
        result.automatic_parameters = config.automatic_parameters;
        state.automatic_parameters = config.automatic_parameters;
        reception_tuning = config.reception_tuning;
        ofdm_experiments = config.ofdm_experiments;
        result.ts_context = this;
        result.accept_ts = [](void* context, const uint8_t* data, size_t bytes, uint64_t generation)
        {
            auto& self = *static_cast<Impl*>(context);
            if (!self.armed.load()) return true;
            ++self.callbacks;
            bool accepted = true;
            if (self.armed.load())
                accepted = generation == self.routed_rf && self.media.acceptTs(data, bytes, self.routed_media, 3000);
            --self.callbacks;
            return accepted || !self.armed.load();
        };
        return result;
    }
    Impl(IPacketRadio& radio, Config config, bool (*handoff)())
        : decoder(bind(std::move(config))), receive(radio, decoder, handoff), owner([this]
                                                                                    { loop(); }) {}
    ~Impl()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            shutdown = true;
            stop = true;
        }
        changed.notify_one();
        owner.join();
    }
    static uint32_t age(Clock::time_point value)
    {
        if (value == Clock::time_point{}) return UINT32_MAX;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - value).count();
        return static_cast<uint32_t>(std::min<int64_t>(elapsed, UINT32_MAX));
    }
    void fail(Error error, const char* message)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (state.error == Error::None)
        {
            state.error = error;
            state.phase = Phase::Error;
            std::snprintf(state.message, sizeof(state.message), "%s", message);
        }
    }
    void stopMedia()
    {
        armed = false;
        // Wake a producer waiting for media space before draining callbacks.
        media.requestStop();
        // A callback that saw the old arm either completes or rechecks the gate.
        // The decoder owner remains alive while media state is reconfigured.
        while (callbacks.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        media.join();
        std::lock_guard<std::mutex> lock(mutex);
        state.media_epoch = 0;
        state.media = {};
        state.presented_frames = 0;
        copied_ordinal = acknowledged_ordinal = observed_video = 0;
        video_progress = presentation = {};
        video_anchor = {};
        video_observed_at = {};
        video_observed_ordinal = 0;
        video_observed_pts = INT64_MIN;
        state.video_clock_reanchors = 0;
        video_anchor_pts = INT64_MIN;
        state.timing = Timing::Unavailable;
        state.presented_pts_us = state.av_delta_us = INT64_MIN;
        copied_pts_us = copied_delta_us = INT64_MIN;
    }
    void publish(const OneSegReceiveWorker::Snapshot& rx, const OneSegDecoderProcess::Snapshot& decoded,
                 const OneSegMediaWorker::Snapshot& picture, bool media_started)
    {
        const auto converted = service_text.update(decoded.service_information);
        std::lock_guard<std::mutex> lock(mutex);
        state.receive = rx;
        state.decoder = decoded;
        state.service_text = state.phase == Phase::Stopping || state.error != Error::None ? OneSegServiceText{} : converted;
        if (media_started && picture.generation == state.media_epoch)
        {
            const uint64_t count = std::max(state.media.video_frames, picture.video_frames);
            state.media = picture;
            state.media.video_frames = count;
            if (count > observed_video)
            {
                observed_video = count;
                video_progress = Clock::now();
            }
        }
        if (rx.forwarded_pairs > observed_pairs)
        {
            observed_pairs = rx.forwarded_pairs;
            iq_progress = Clock::now();
        }
        state.iq_age_ms = age(iq_progress);
        state.video_age_ms = age(video_progress);
        if (state.phase == Phase::Stopping) return;
        if (state.error != Error::None) state.phase = Phase::Error;
        else if (media_started)
            state.phase = acknowledged_ordinal && age(presentation) < 2000 && state.video_age_ms < 2000 && state.iq_age_ms < 2000 &&
                                  (state.timing == Timing::AudioQueueEstimate || state.timing == Timing::VideoMonotonic)
                              ? Phase::FramePresented
                              : Phase::Decoding;
        else state.phase = rx.forwarded_pairs ? Phase::ReceivingIq : Phase::Starting;
    }
    void runSynthetic(unsigned channel)
    {
        try
        {
            auto training = experiment;
            training.automatic_waveform = true; // Known framing, no manual TV FEC.
            if (!decoder.configureExperiment(training, 64000000 / 63) || !decoder.configureSynthetic())
                fail(Error::Configuration, "Invalid synthetic OFDM configuration");
            else if (!stop.load() && !decoder.open(channel, state.generation, stop))
                fail(Error::Decoder, decoder.lastError());
            else
            {
                std::unique_lock<std::mutex> lock(mutex);
                state.input_rate_hz = 64000000 / 63;
                while (!stop.load())
                {
                    lock.unlock();
                    const auto decoded = decoder.snapshot();
                    const bool healthy = decoder.healthy();
                    lock.lock();
                    state.decoder = decoded;
                    state.phase = Phase::Decoding;
                    if (!healthy) break;
                    changed.wait_for(lock, std::chrono::milliseconds(20), [this]
                                     { return stop.load(); });
                }
            }
        }
        catch (...)
        {
            fail(Error::Internal, "Synthetic OFDM model failed");
        }
        if (!decoder.finish()) fail(Error::Decoder, decoder.lastError());
        const auto decoded = decoder.snapshot();
        std::lock_guard<std::mutex> lock(mutex);
        state.decoder = decoded;
        state.active = false;
        state.phase = state.error == Error::None ? Phase::Stopped : Phase::Error;
    }
    void run(unsigned channel, uint16_t target_width, uint16_t target_height)
    {
        if (source.synthetic)
        {
            runSynthetic(channel);
            return;
        }
        logTelemetry(true);
        service_text.reset();
        bool media_started = false, media_attempted = false, receive_started = false, receive_attempted = false;
        uint16_t desired = 0;
        Mapping current;
        try
        {
            uint32_t input_rate = reception.sample_rate_hz;
            if (!source.archive_path.empty() && !stop.load())
            {
                // Validate off the UI before applying rate-dependent controls.
                // Worker independently validates again before decoder admission.
                OneSegIqArchive archive;
                std::string error;
                if (!archive.load(source.archive_path, error))
                {
                    fail(Error::Configuration, error.c_str());
                    stop = true;
                }
                else
                {
                    input_rate = archive.metadata().rate;
                    channel = archive.metadata().channel;
                    std::lock_guard<std::mutex> lock(mutex);
                    state.channel = channel;
                }
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                state.input_rate_hz = !source.archive_path.empty() || reception.enabled ? input_rate : 0;
            }
            if (!stop.load())
            {
                if (ofdm_experiments && !decoder.configureExperiment(experiment, input_rate))
                {
                    fail(Error::Configuration, "Invalid OFDM experiment or decoder still active");
                    stop = true;
                }
            }
            if (!stop.load())
            {
                receive_attempted = true;
                receive_started = receive.start(channel, reception, source);
                if (!receive_started) fail(Error::Radio, receive.snapshot().message);
            }
            while (receive_started && !stop.load())
            {
                const auto rx = receive.snapshot();
                auto decoded = decoder.snapshot();
                if (decoded.generation != rx.generation) decoded = {};
                const auto picture = media.snapshot();
                if (media_started && !media.healthy())
                {
                    fail(Error::Media, picture.message[0] ? picture.message : "Television media worker stopped");
                    break;
                }
                if (decoded.phase == OneSegDecoderProcess::Phase::Error)
                {
                    fail(Error::Decoder, decoded.message);
                    break;
                }
                if (rx.phase == OneSegReceiveWorker::Phase::Error)
                {
                    fail(Error::Radio, rx.message);
                    break;
                }
                if (rx.phase == OneSegReceiveWorker::Phase::Stopped) break;
                uint16_t requested;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    requested = selection;
                    selection = 0;
                }
                if (requested && decoder.selectProgram(requested))
                {
                    desired = requested;
                    if (current.programme != requested)
                    {
                        stopMedia();
                        media_started = false;
                        current = {};
                    }
                }
                if (desired && decoded.programmes.pat_valid)
                {
                    bool listed = false;
                    for (size_t i = 0; i < decoded.programmes.program_count; ++i)
                        listed = listed || decoded.programmes.programs[i].number == desired;
                    if (!listed)
                    {
                        desired = 0;
                        std::lock_guard<std::mutex> lock(mutex);
                        state.requested_programme = 0;
                        std::snprintf(state.message, sizeof(state.message), "%s", "Selected programme removed; following received programme table");
                    }
                }
                const Mapping next = mapping(decoded);
                if (decoded.generation == rx.generation && next.supported() && (!desired || next.programme == desired))
                {
                    if (!media_started || !(next == current))
                    {
                        stopMedia();
                        if (++epoch_sequence == 0) ++epoch_sequence;
                        const uint64_t epoch = epoch_sequence;
                        {
                            std::lock_guard<std::mutex> lock(mutex);
                            state.media_epoch = epoch;
                            has_audio = next.audio != 0x1fff;
                            state.timing = has_audio ? Timing::WaitingForAudio : Timing::Unavailable;
                            state.presented_frames = 0;
                            state.media = {};
                        }
                        media_attempted = true;
                        if (!media.start({epoch, next.video, next.audio, target_width, target_height,
                                          next.programme, next.pmt, decoded.programmes.transport_id}))
                        {
                            fail(Error::Media, media.snapshot().message);
                            break;
                        }
                        current = next;
                        media_started = true;
                        routed_rf = rx.generation;
                        routed_media = epoch;
                        armed = true;
                    }
                }
                else if (media_started && (!decoded.programmes.pmt_valid || !next.supported()))
                {
                    stopMedia();
                    media_started = false;
                    current = {};
                }
                publish(rx, decoded, media.snapshot(), media_started);
                logTelemetry();
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait_for(lock, std::chrono::milliseconds(20), [this]
                                 { return stop.load() || selection; });
            }
        }
        catch (...)
        {
            fail(Error::Internal, "Television session failed");
        }
        stopMedia();
        receive.requestStop();
        receive.waitUntilFinished();
        const auto rx = receive.snapshot();
        const auto decoded = decoder.snapshot();
        const auto picture = media.snapshot();
        if (media_attempted && picture.error != OneSegMediaWorker::Error::None) fail(Error::Media, picture.message);
        if (rx.error != OneSegReceiveWorker::Error::None && receive_started) fail(Error::Radio, rx.message);
        if (decoded.phase == OneSegDecoderProcess::Phase::Error && receive_started) fail(Error::Decoder, decoded.message);
        std::lock_guard<std::mutex> lock(mutex);
        if (receive_attempted)
        {
            state.receive = rx;
            if (decoded.generation == rx.generation) state.decoder = decoded;
        }
        if (media_started) state.media = picture;
        state.active = false;
        state.service_text = {};
        state.phase = state.error == Error::None ? Phase::Stopped : Phase::Error;
    }
    void loop()
    {
        std::unique_lock<std::mutex> lock(mutex);
        for (;;)
        {
            changed.wait(lock, [this]
                         { return pending || iq_export_pending || iq_catalog_pending || shutdown; });
            if (shutdown && !pending && !iq_export_pending && !iq_catalog_pending) return;
            if (iq_export_pending)
            {
                const auto path = iq_export_path;
                iq_export_pending = false;
                lock.unlock();
                bool ok = false;
                std::string error;
                try
                {
                    std::error_code directory_error;
                    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), directory_error);
                    if (directory_error) error = "Cannot create IQ recording directory";
                    else ok = receive.exportRecording(path, error);
                }
                catch (...)
                {
                    error = "IQ archive export failed";
                }
                lock.lock();
                state.iq_exporting = false;
                state.iq_export_ok = ok;
                state.iq_export_serial = ++iq_export_serial;
                std::snprintf(state.iq_export_message, sizeof(state.iq_export_message), "%s", ok ? "IQ archive saved" : error.c_str());
                continue;
            }
            if (iq_catalog_pending)
            {
                const auto directory = iq_catalog_directory;
                iq_catalog_pending = false;
                lock.unlock();
                std::vector<std::string> paths;
                std::string error;
                try
                {
                    std::error_code ec;
                    if (std::filesystem::exists(directory, ec))
                    {
                        std::filesystem::directory_iterator it(directory, ec), end;
                        for (unsigned visited = 0; it != end && !ec && visited < 4096; it.increment(ec), ++visited)
                        {
                            const auto& item = *it;
                            if (!item.is_symlink(ec) && item.is_regular_file(ec) && item.path().extension() == ".tmiq" &&
                                item.path().filename().string().find_first_of("\r\n") == std::string::npos)
                                paths.push_back(item.path().string());
                        }
                    }
                    if (ec) error = "Cannot read IQ recording directory";
                    std::sort(paths.rbegin(), paths.rend());
                    if (paths.size() > 24) paths.resize(24);
                }
                catch (...)
                {
                    paths.clear();
                    error = "Cannot list IQ recordings";
                }
                lock.lock();
                state.iq_archives = std::move(paths);
                state.iq_catalog_loading = false;
                state.iq_catalog_serial = ++iq_catalog_serial;
                std::snprintf(state.iq_catalog_message, sizeof(state.iq_catalog_message), "%s", error.c_str());
                continue;
            }
            const unsigned channel = state.channel;
            const uint16_t w = width, h = height;
            pending = false;
            lock.unlock();
            run(channel, w, h);
            logTelemetry(true);
            lock.lock();
        }
    }
};

OneSegTvSession::OneSegTvSession(IPacketRadio& radio, Config config, bool (*handoff)())
    : impl_(std::make_unique<Impl>(radio, std::move(config), handoff)) {}
OneSegTvSession::~OneSegTvSession() = default;
bool OneSegTvSession::start(unsigned channel, uint16_t width, uint16_t height, OneSegReceptionSettings reception, OneSegOfdmExperiment experiment)
{
    return start(channel, width, height, reception, experiment, {});
}
bool OneSegTvSession::start(unsigned channel, uint16_t width, uint16_t height, OneSegReceptionSettings reception, OneSegOfdmExperiment experiment, OneSegReceiveWorker::Source source)
{
    auto& s = *impl_;
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.state.active || s.state.iq_exporting || s.state.iq_catalog_loading || s.shutdown) return false;
    if (source.synthetic && (!source.archive_path.empty() || source.recording_capacity)) return false;
    if (source.recording_capacity > OneSegIqArchive::maximum_bytes || source.recording_capacity % 2 ||
        (source.recording_capacity && !source.archive_path.empty()) ||
        (!source.archive_path.empty() && (source.archive_path.front() != '/' || source.archive_path.size() > 4096 || source.archive_path.find('\0') != std::string::npos))) return false;
    if (!reception.enabled && s.reception_tuning)
    {
        reception = s.reception;
        reception.enabled = true;
    }
    if (reception.enabled && (!s.reception_tuning || !reception.valid())) return false;
    if (source.recording_capacity && !reception.enabled) return false;
    if ((s.ofdm_experiments || source.synthetic) && !experiment.valid(source.synthetic ? 64000000 / 63 : reception.sample_rate_hz, source.synthetic)) return false;
    s.reception = reception;
    s.experiment = experiment;
    s.source = std::move(source);
    if (channel < 13 || channel > 62 || !width || !height || width > OneSegMediaWorker::max_width || height > OneSegMediaWorker::max_height)
    {
        const uint64_t generation = s.state.generation;
        const unsigned volume = s.state.volume;
        const bool automatic = s.state.automatic_parameters;
        s.state = {};
        s.state.generation = generation;
        s.state.volume = volume;
        s.state.automatic_parameters = automatic;
        s.state.channel = channel;
        s.state.phase = Phase::Error;
        s.state.error = Error::Configuration;
        std::snprintf(s.state.message, sizeof(s.state.message), "%s", "Invalid television channel or viewport");
        return false;
    }
    const uint64_t generation = s.state.generation + 1;
    const unsigned volume = s.state.volume;
    const bool automatic = s.state.automatic_parameters;
    s.state = {};
    s.state.generation = generation ? generation : 1;
    s.state.reception = s.reception;
    s.state.experiment = s.experiment;
    s.state.source = s.source;
    s.state.iq_export_serial = s.iq_export_serial;
    s.state.iq_catalog_serial = s.iq_catalog_serial;
    s.state.volume = volume;
    s.state.automatic_parameters = automatic;
    s.state.channel = channel;
    s.state.active = true;
    if (s.ofdm_experiments) s.state.automatic_parameters = experiment.automatic_waveform;
    s.state.phase = Phase::Starting;
    s.width = width;
    s.height = height;
    s.selection = 0;
    s.copied_ordinal = s.acknowledged_ordinal = s.observed_pairs = s.observed_video = 0;
    s.iq_progress = s.video_progress = s.presentation = {};
    s.stop = false;
    s.pending = true;
    s.changed.notify_one();
    return true;
}
bool OneSegTvSession::requestIqExport(const std::string& path)
{
    auto& s = *impl_;
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.pending || s.state.iq_exporting || s.state.iq_catalog_loading || s.shutdown || path.empty() || path.front() != '/' ||
        path.size() > 4096 || path.find('\0') != std::string::npos || !s.state.receive.recorded_bytes) return false;
    if (s.state.active && !s.state.source.recording_capacity) return false;
    s.iq_export_path = path;
    s.state.iq_exporting = s.iq_export_pending = true;
    s.state.iq_export_ok = false;
    std::snprintf(s.state.iq_export_message, sizeof(s.state.iq_export_message), "%s", "Saving IQ archive on background owner");
    if (s.state.active)
    {
        s.stop = true;
        s.state.phase = Phase::Stopping;
    }
    s.changed.notify_one();
    return true;
}
bool OneSegTvSession::requestIqCatalog(const std::string& directory)
{
    auto& s = *impl_;
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.state.active || s.pending || s.state.iq_exporting || s.state.iq_catalog_loading || s.shutdown ||
        directory.empty() || directory.front() != '/' || directory.size() > 4096 || directory.find('\0') != std::string::npos) return false;
    s.iq_catalog_directory = directory;
    s.state.iq_catalog_loading = s.iq_catalog_pending = true;
    s.changed.notify_one();
    return true;
}
bool OneSegTvSession::receptionTuningAvailable() const
{
    return impl_->reception_tuning;
}
void OneSegTvSession::requestStop(uint64_t generation)
{
    auto& s = *impl_;
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.state.active || (generation && generation != s.state.generation)) return;
    s.stop = true;
    s.state.phase = Phase::Stopping;
    s.changed.notify_one();
}
bool OneSegTvSession::selectProgramme(uint64_t generation, uint16_t number)
{
    auto& s = *impl_;
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.state.active || s.stop.load() || s.state.error != Error::None || generation != s.state.generation) return false;
    const auto& table = s.state.decoder.programmes;
    for (size_t i = 0; i < table.program_count; ++i)
        if (table.programs[i].number == number)
        {
            s.selection = number;
            s.state.requested_programme = number;
            s.state.message[0] = 0;
            s.changed.notify_one();
            return true;
        }
    return false;
}
void OneSegTvSession::setVolume(unsigned percent)
{
    const unsigned volume = std::min(percent, 100u);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->audio.setVolume(volume);
    impl_->state.volume = volume;
}
OneSegTvSession::Snapshot OneSegTvSession::snapshot() const
{
    auto& s = *impl_;
    std::lock_guard<std::mutex> lock(s.mutex);
    auto value = s.state;
    OneSegServiceInformation::expire(value.decoder.service_information, static_cast<uint64_t>(
                                                                            std::chrono::duration_cast<std::chrono::milliseconds>(Impl::Clock::now().time_since_epoch()).count()));
    const auto& information = value.decoder.service_information;
    if (!value.active || value.phase == Phase::Stopping || value.error != Error::None ||
        information.service != value.service_text.service ||
        (value.requested_programme && value.service_text.service != value.requested_programme))
        value.service_text = {};
    else
    {
        value.service_text.service_valid = information.service_valid;
        value.service_text.present_valid = information.present.valid;
        value.service_text.following_valid = information.following.valid;
        if (!information.service_valid) value.service_text.name = value.service_text.provider = {};
        if (!information.present.valid) value.service_text.present_title = value.service_text.present_description = {};
        if (!information.following.valid) value.service_text.following_title = value.service_text.following_description = {};
    }
    value.iq_age_ms = Impl::age(s.iq_progress);
    value.video_age_ms = Impl::age(s.video_progress);
    value.presentation_age_ms = Impl::age(s.presentation);
    value.audio_clock = s.audio.clock();
    if (value.active && value.media_epoch && s.has_audio &&
        (value.audio_clock.generation != value.media_epoch || !value.audio_clock.valid))
        value.timing = value.audio_clock.generation == value.media_epoch && value.audio_clock.starved ? Timing::AudioStarved : Timing::WaitingForAudio;
    if (value.phase == Phase::FramePresented &&
        (value.presentation_age_ms >= 2000 || value.timing == Timing::AudioStarved || value.timing == Timing::WaitingForAudio))
        value.phase = Phase::Decoding;
    return value;
}
bool OneSegTvSession::copyLatestFrame(uint64_t generation, uint64_t epoch, uint8_t* destination, size_t capacity,
                                      OneSegMediaWorker::FrameInfo& info)
{
    auto& s = *impl_;
    bool has_audio;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.state.active || s.stop.load() || s.state.error != Error::None || s.state.generation != generation ||
            !epoch || s.state.media_epoch != epoch) return false;
        has_audio = s.has_audio;
    }
    int64_t target_pts;
    if (has_audio)
    {
        const auto clock = s.audio.clock();
        std::lock_guard<std::mutex> lock(s.mutex);
        if (s.state.media_epoch != epoch) return false;
        s.state.timing = clock.generation == epoch && clock.valid ? Timing::AudioQueueEstimate : clock.generation == epoch && clock.starved ? Timing::AudioStarved
                                                                                                                                            : Timing::WaitingForAudio;
        if (s.state.timing != Timing::AudioQueueEstimate) return false;
        target_pts = clock.pts_us;
    }
    else
    {
        OneSegMediaWorker::FrameInfo latest;
        if (!s.media.latestFrameInfo(epoch, latest)) return false;
        std::lock_guard<std::mutex> lock(s.mutex);
        if (s.state.media_epoch != epoch) return false;
        if (!OneSegAudioClock::timestampValid(latest.pts_us))
        {
            s.state.timing = Timing::MissingVideoPts;
            return false;
        }
        const auto now = Impl::Clock::now();
        bool discontinuity = false;
        if (s.video_observed_ordinal && latest.ordinal > s.video_observed_ordinal)
        {
            const auto wall = std::chrono::duration_cast<std::chrono::microseconds>(now - s.video_observed_at).count();
            const int64_t drift = (latest.pts_us - s.video_observed_pts) - std::min<int64_t>(wall, INT64_MAX / 4);
            discontinuity = drift > 250000 || drift < -250000;
        }
        if (s.video_anchor == Impl::Clock::time_point{} || discontinuity)
        {
            s.video_anchor = now;
            s.video_anchor_pts = latest.pts_us;
            if (discontinuity) ++s.state.video_clock_reanchors;
        }
        if (latest.ordinal > s.video_observed_ordinal)
        {
            s.video_observed_ordinal = latest.ordinal;
            s.video_observed_pts = latest.pts_us;
            s.video_observed_at = now;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - s.video_anchor).count();
        target_pts = s.video_anchor_pts + std::min<int64_t>(elapsed, INT64_MAX / 4);
        s.state.timing = Timing::VideoMonotonic;
    }
    OneSegMediaWorker::FrameInfo copied;
    if (!s.media.copyFrameAt(epoch, target_pts, destination, capacity, copied)) return false;
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.state.active || s.stop.load() || s.state.error != Error::None || s.state.generation != generation || s.state.media_epoch != epoch) return false;
    if (copied.ordinal > s.observed_video)
    {
        s.observed_video = copied.ordinal;
        s.video_progress = Impl::Clock::now();
    }
    if (Impl::age(s.video_progress) >= 2000) return false;
    s.copied_ordinal = copied.ordinal;
    s.copied_pts_us = copied.pts_us;
    s.copied_delta_us = has_audio ? copied.pts_us - target_pts : INT64_MIN;
    s.state.media.video_frames = std::max(s.state.media.video_frames, copied.ordinal);
    info = copied;
    return true;
}
bool OneSegTvSession::acknowledgeFrame(uint64_t generation, uint64_t epoch, uint64_t ordinal)
{
    auto& s = *impl_;
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.state.active || s.stop.load() || s.state.error != Error::None || generation != s.state.generation ||
        !epoch || epoch != s.state.media_epoch || !ordinal || ordinal != s.copied_ordinal || ordinal <= s.acknowledged_ordinal) return false;
    s.acknowledged_ordinal = ordinal;
    s.presentation = Impl::Clock::now();
    s.state.presented_pts_us = s.copied_pts_us;
    s.state.av_delta_us = s.copied_delta_us;
    ++s.state.presented_frames;
    return true;
}
} // namespace platform::linux_runtime
