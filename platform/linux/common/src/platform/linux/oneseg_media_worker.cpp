#include "platform/linux/oneseg_media_worker.h"
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace platform::linux_runtime
{
struct OneSegMediaWorker::Impl
{
    static constexpr uint32_t slots = 512;
    static_assert(std::atomic<uint32_t>::is_always_lock_free && std::atomic_bool::is_always_lock_free);
    struct Video
    {
        std::vector<uint8_t> data;
        FrameInfo info;
    };
    OneSegMediaAudio* audio;
    Config config;
    mutable std::mutex mutex;
    Snapshot state;
    std::thread worker;
    std::atomic_bool cancel{false}, eof{false}, active{false};
    std::atomic<Error> error{Error::None};
    std::atomic<uint32_t> written{0}, read{0}, high_water{0};
    std::array<std::array<uint8_t, 188>, slots> packets;
    size_t input_offset = 0;
    static constexpr size_t video_slots = 12;
    std::array<Video, video_slots + 1> images; // Scratch plus bounded presentation queue.
    std::mutex frame_mutex;
    std::atomic<int> published{-1};
    std::atomic<uint64_t> consumed_ordinal{0};
    std::array<int16_t, 8192> pcm;
    AVFormatContext* format = nullptr;
    AVIOContext* io = nullptr;
    AVCodecContext *video = nullptr, *sound = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    SwsContext* scale = nullptr;
    SwrContext* resample = nullptr;
    AVChannelLayout audio_layout{};
    AVSampleFormat audio_format = AV_SAMPLE_FMT_NONE;
    int audio_rate = 0, video_index = -1, audio_index = -1;
    bool audio_open = false, audio_attempted = false;
    int64_t next_pcm_pts = INT64_MIN;
    char detail[112]{};
    explicit Impl(OneSegMediaAudio* value) : audio(value) {}
    void fail(Error reason)
    {
        Error expected = Error::None;
        (void)error.compare_exchange_strong(expected, reason);
        cancel.store(true, std::memory_order_release);
    }
    int input(uint8_t* destination, int capacity)
    {
        for (;;)
        {
            if (cancel.load()) return AVERROR_EXIT;
            const uint32_t current = read.load(std::memory_order_relaxed);
            if (current != written.load(std::memory_order_acquire))
            {
                const size_t count = std::min(size_t(capacity), size_t(188 - input_offset));
                std::memcpy(destination, packets[current % slots].data() + input_offset, count);
                input_offset += count;
                if (input_offset == 188)
                {
                    input_offset = 0;
                    read.store(current + 1, std::memory_order_release);
                }
                return static_cast<int>(count);
            }
            if (eof.load())
            {
                if (current != written.load(std::memory_order_acquire)) continue;
                return AVERROR_EOF;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    bool openCodec(int& index, uint16_t pid, AVMediaType type, AVCodecContext*& codec)
    {
        if (pid == 0x1fff) return true;
        for (unsigned i = 0; i < format->nb_streams; ++i)
            if (format->streams[i]->id == pid && format->streams[i]->codecpar->codec_type == type)
            {
                index = static_cast<int>(i);
                break;
            }
        if (index < 0)
        {
            std::snprintf(detail, sizeof(detail), "PID %u type %d missing in %u streams", pid, int(type), format->nb_streams);
            return false;
        }
        const auto* parameters = format->streams[index]->codecpar;
        const AVCodec* implementation = avcodec_find_decoder(parameters->codec_id);
        if (!implementation || !(codec = avcodec_alloc_context3(implementation))) return false;
        codec->thread_count = 1;
        codec->max_pixels = 1920 * 1080;
        codec->pkt_timebase = format->streams[index]->time_base;
        int result = avcodec_parameters_to_context(codec, parameters);
        if (result >= 0) result = avcodec_open2(codec, implementation, nullptr);
        if (result < 0) av_strerror(result, detail, sizeof(detail));
        return result >= 0;
    }
    bool open()
    {
        format = avformat_alloc_context();
        auto* buffer = static_cast<uint8_t*>(av_malloc(32768));
        if (!format || !buffer)
        {
            av_free(buffer);
            return false;
        }
        io = avio_alloc_context(
            buffer, 32768, 0, this,
            [](void* context, uint8_t* out, int count)
            { return static_cast<Impl*>(context)->input(out, count); },
            nullptr, nullptr);
        if (!io)
        {
            av_free(buffer);
            return false;
        }
        format->pb = io;
        format->flags |= AVFMT_FLAG_CUSTOM_IO;
        format->interrupt_callback = {[](void* context)
                                      { return static_cast<Impl*>(context)->cancel.load() ? 1 : 0; },
                                      this};
        // MPEG-TS scans this budget before enabling no-PAT stream discovery.
        // Keep live startup bounded by a short prefix instead of awaiting EOF.
        format->probesize = 32768;
        format->max_analyze_duration = 1000000;
        AVDictionary* options = nullptr;
        av_dict_set(&options, "scan_all_pmts", "1", 0);
        // A media epoch owns fixed received PIDs. PMT merging disables the
        // demuxer's no-PAT stream discovery on the validated 1seg fixture.
        const int opened = avformat_open_input(&format, nullptr, av_find_input_format("mpegts"), &options);
        av_dict_free(&options);
        if (opened < 0 || avformat_find_stream_info(format, nullptr) < 0) return false;
        return true;
    }
    void showVideo()
    {
        if (frame->width <= 0 || frame->height <= 0 || frame->width > 1920 || frame->height > 1080)
        {
            fail(Error::Video);
            return;
        }
        // Respect sample aspect ratio while fitting the configured viewport.
        AVRational aspect = frame->sample_aspect_ratio;
        if (aspect.num <= 0 || aspect.den <= 0) aspect = {1, 1};
        const double ratio = double(frame->width) * aspect.num / (double(frame->height) * aspect.den);
        if (!(ratio > .01 && ratio < 100))
        {
            fail(Error::Video);
            return;
        }
        unsigned width = config.width, height = std::max(1u, unsigned(width / ratio));
        if (height > config.height)
        {
            height = config.height;
            width = std::max(1u, unsigned(height * ratio));
        }
        width = std::min<unsigned>(width, config.width);
        auto& image = images[0];
        scale = sws_getCachedContext(scale, frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
                                     width, height, AV_PIX_FMT_RGB565LE, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!scale)
        {
            fail(Error::Video);
            return;
        }
        uint8_t* planes[4]{image.data.data(), nullptr, nullptr, nullptr};
        int strides[4]{static_cast<int>(width * 2), 0, 0, 0};
        if (sws_scale(scale, frame->data, frame->linesize, 0, frame->height, planes, strides) != static_cast<int>(height))
        {
            fail(Error::Video);
            return;
        }
        const int64_t pts = frame->best_effort_timestamp;
        {
            std::lock_guard<std::mutex> lock(mutex);
            image.info = {config.generation, ++state.video_frames,
                          pts == AV_NOPTS_VALUE ? INT64_MIN : av_rescale_q(pts, format->streams[video_index]->time_base, {1, 1000000}),
                          static_cast<uint16_t>(width), static_cast<uint16_t>(height), width * 2};
        }
        bool overwritten;
        {
            std::lock_guard<std::mutex> frame_lock(frame_mutex);
            const size_t slot = 1 + (image.info.ordinal - 1) % video_slots;
            overwritten = images[slot].info.ordinal > consumed_ordinal.load();
            std::memcpy(images[slot].data.data(), image.data.data(), size_t(image.info.stride) * image.info.height);
            images[slot].info = image.info;
            published.store(static_cast<int>(slot), std::memory_order_release);
        }
        if (overwritten)
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++state.superseded_frames;
        }
    }
    bool copyVideo(uint64_t generation, bool timed, int64_t pts, uint8_t* destination, size_t capacity, FrameInfo& info)
    {
        if (!destination || (timed && (pts < -INT64_MAX / 4 || pts > INT64_MAX / 4))) return false;
        std::unique_lock<std::mutex> lock(frame_mutex, std::try_to_lock);
        if (!lock || published.load(std::memory_order_acquire) < 0) return false;
        size_t selected = 0;
        for (size_t i = 1; i < images.size(); ++i)
        {
            const auto& frame = images[i].info;
            if (frame.generation != generation || !frame.ordinal) continue;
            if (timed && (frame.pts_us == INT64_MIN || frame.pts_us > pts + 30000 || frame.pts_us < pts - 250000)) continue;
            if (!selected || frame.ordinal > images[selected].info.ordinal) selected = i;
        }
        if (!selected) return false;
        const auto value = images[selected].info;
        const size_t bytes = size_t(value.stride) * value.height;
        if (bytes > capacity) return false;
        std::memcpy(destination, images[selected].data.data(), bytes);
        info = value;
        consumed_ordinal.store(std::max(consumed_ordinal.load(), value.ordinal));
        return true;
    }
    bool configureResampler()
    {
        if (frame->sample_rate < 8000 || frame->sample_rate > 96000 || frame->ch_layout.nb_channels <= 0 ||
            frame->ch_layout.nb_channels > 8 || frame->nb_samples <= 0 || frame->nb_samples > 8192) return false;
        if (resample && audio_rate == frame->sample_rate && audio_format == frame->format &&
            av_channel_layout_compare(&audio_layout, &frame->ch_layout) == 0) return true;
        // A midstream format change requires a fresh epoch; do not discard delay.
        if (resample) return false;
        AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
        if (swr_alloc_set_opts2(&resample, &mono, AV_SAMPLE_FMT_S16, 48000, &frame->ch_layout,
                                static_cast<AVSampleFormat>(frame->format), frame->sample_rate, 0, nullptr) < 0 ||
            !resample || swr_init(resample) < 0 || av_channel_layout_copy(&audio_layout, &frame->ch_layout) < 0) return false;
        audio_rate = frame->sample_rate;
        audio_format = static_cast<AVSampleFormat>(frame->format);
        return true;
    }
    void playAudio()
    {
        if (!configureResampler())
        {
            fail(Error::Audio);
            return;
        }
        const int needed = swr_get_out_samples(resample, frame->nb_samples);
        if (needed < 0 || needed > static_cast<int>(pcm.size()))
        {
            fail(Error::Audio);
            return;
        }
        uint8_t* output = reinterpret_cast<uint8_t*>(pcm.data());
        const int64_t delay = swr_get_delay(resample, audio_rate);
        const int count = swr_convert(resample, &output, pcm.size(), const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
        if (count < 0)
        {
            fail(Error::Audio);
            return;
        }
        const int64_t input_pts = frame->best_effort_timestamp;
        const int64_t pts = input_pts == AV_NOPTS_VALUE ? INT64_MIN : av_rescale_q(input_pts, format->streams[audio_index]->time_base, {1, 1000000}) - av_rescale(delay, 1000000, audio_rate);
        if (outputPcm(count, pts) && !cancel.load())
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++state.audio_frames;
        }
    }
    bool outputPcm(int count, int64_t pts)
    {
        if (!count) return true;
        if (!audio_open)
        {
            audio_attempted = true;
            if (!audio || !audio->begin(config.generation))
            {
                fail(Error::Audio);
                return false;
            }
            audio_open = true;
        }
        size_t offset = 0;
        while (offset < static_cast<size_t>(count) && !cancel.load())
        {
            const size_t samples = std::min<size_t>(2048, count - offset);
            if (!audio->write(pcm.data() + offset, samples,
                              pts == INT64_MIN ? INT64_MIN : pts + av_rescale(offset, 1000000, 48000), config.generation, cancel))
            {
                if (!cancel.load()) fail(Error::Audio);
                return false;
            }
            offset += samples;
            {
                std::lock_guard<std::mutex> lock(mutex);
                state.pcm_samples += samples;
            }
        }
        next_pcm_pts = pts == INT64_MIN ? INT64_MIN : pts + av_rescale(offset, 1000000, 48000);
        return !cancel.load();
    }
    void flushResampler()
    {
        if (!resample || !audio_open) return;
        for (unsigned batch = 0; batch < 16 && !cancel.load(); ++batch)
        {
            uint8_t* destination = reinterpret_cast<uint8_t*>(pcm.data());
            const int count = swr_convert(resample, &destination, pcm.size(), nullptr, 0);
            if (!count) return;
            if (count < 0)
            {
                fail(Error::Audio);
                return;
            }
            if (!outputPcm(count, next_pcm_pts)) return;
        }
        if (!cancel.load()) fail(Error::Audio);
    }
    void decode(AVCodecContext* codec, AVPacket* input, bool is_video)
    {
        int result = avcodec_send_packet(codec, input);
        if (result < 0 && result != AVERROR_EOF)
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++state.decode_errors;
            return;
        }
        while (!cancel.load())
        {
            result = avcodec_receive_frame(codec, frame);
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) break;
            if (result < 0)
            {
                std::lock_guard<std::mutex> lock(mutex);
                ++state.decode_errors;
                break;
            }
            if ((frame->flags & AV_FRAME_FLAG_CORRUPT) || frame->decode_error_flags)
            {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    ++state.decode_errors;
                }
                av_frame_unref(frame);
                continue;
            }
            if (is_video) showVideo();
            else playAudio();
            av_frame_unref(frame);
        }
    }
    void cleanup()
    {
        if (audio_attempted)
        {
            try
            {
                if (!audio->end(cancel.load())) fail(Error::Audio);
            }
            catch (...)
            {
                fail(Error::Internal);
            }
            audio_open = audio_attempted = false;
        }
        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&video);
        avcodec_free_context(&sound);
        sws_freeContext(scale);
        scale = nullptr;
        swr_free(&resample);
        av_channel_layout_uninit(&audio_layout);
        audio_format = AV_SAMPLE_FMT_NONE;
        audio_rate = 0;
        avformat_close_input(&format);
        if (io) av_freep(&io->buffer);
        avio_context_free(&io);
    }
    void run()
    {
        try
        {
            if (!open())
            {
                if (!cancel.load()) fail(Error::Demux);
            }
            else if (cancel.load())
            {
            }
            else if (!openCodec(video_index, config.video_pid, AVMEDIA_TYPE_VIDEO, video) ||
                     !openCodec(audio_index, config.audio_pid, AVMEDIA_TYPE_AUDIO, sound) ||
                     !(frame = av_frame_alloc()) || !(packet = av_packet_alloc()))
            {
                if (!cancel.load()) fail(Error::Codec);
            }
            else
            {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    state.phase = Phase::Decoding;
                }
                while (!cancel.load())
                {
                    const int result = av_read_frame(format, packet);
                    if (result == AVERROR_EOF)
                    {
                        if (video) decode(video, nullptr, true);
                        if (sound) decode(sound, nullptr, false);
                        flushResampler();
                        break;
                    }
                    if (result < 0)
                    {
                        if (!cancel.load()) fail(Error::Demux);
                        break;
                    }
                    if (packet->stream_index == video_index) decode(video, packet, true);
                    else if (packet->stream_index == audio_index) decode(sound, packet, false);
                    av_packet_unref(packet);
                }
            }
        }
        catch (...)
        {
            fail(Error::Internal);
        }
        try
        {
            cleanup();
        }
        catch (...)
        {
            fail(Error::Internal);
        }
        const Error reason = error.load();
        if (reason == Error::Audio && audio && !detail[0])
        {
            try
            {
                std::snprintf(detail, sizeof(detail), "%s", audio->lastError());
            }
            catch (...)
            {
            }
        }
        std::lock_guard<std::mutex> lock(mutex);
        state.error = reason;
        state.phase = reason == Error::None ? Phase::Stopped : Phase::Error;
        const char* messages[] = {"", "Invalid media configuration", "TV TS queue full", "Invalid TS input",
                                  "Cannot demux selected programme", "Cannot open selected codecs", "Video conversion failed",
                                  "Audio conversion or output failed", "Media worker failed"};
        std::snprintf(state.message, sizeof(state.message), "%s%s%.100s", messages[static_cast<unsigned>(reason)], detail[0] ? ": " : "", detail);
        active.store(false, std::memory_order_release);
    }
};

OneSegMediaWorker::OneSegMediaWorker(OneSegMediaAudio* audio) : impl_(std::make_unique<Impl>(audio)) {}
OneSegMediaWorker::~OneSegMediaWorker()
{
    requestStop();
    join();
}
bool OneSegMediaWorker::start(Config config)
{
    auto& s = *impl_;
    if (s.worker.joinable()) return false;
    const auto valid_pid = [](uint16_t pid)
    { return pid >= 0x10 && pid <= 0x1fff; };
    if (!config.generation || !config.width || !config.height || config.width > max_width || config.height > max_height ||
        !valid_pid(config.video_pid) || !valid_pid(config.audio_pid) || config.video_pid == config.audio_pid ||
        (config.audio_pid != 0x1fff && !s.audio))
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        s.state = {};
        s.state.generation = config.generation;
        s.state.phase = Phase::Error;
        s.state.error = Error::Configuration;
        std::snprintf(s.state.message, sizeof(s.state.message), "%s", "Invalid media configuration");
        s.published = -1;
        return false;
    }
    s.config = config;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        s.state = {};
        s.state.generation = config.generation;
        s.state.phase = Phase::Opening;
    }
    s.cancel = s.eof = false;
    s.error = Error::None;
    s.written = s.read = s.high_water = 0;
    s.published = -1;
    s.consumed_ordinal = 0;
    s.input_offset = 0;
    s.video_index = s.audio_index = -1;
    s.detail[0] = 0;
    s.next_pcm_pts = INT64_MIN;
    s.active = true;
    try
    {
        {
            std::lock_guard<std::mutex> lock(s.frame_mutex);
            for (auto& image : s.images)
            {
                image.info = {};
                image.data.resize(size_t(config.width) * config.height * 2);
            }
        }
        s.worker = std::thread([&s]
                               { s.run(); });
    }
    catch (...)
    {
        s.active = false;
        s.error = Error::Internal;
        std::lock_guard<std::mutex> lock(s.mutex);
        s.state.phase = Phase::Error;
        s.state.error = Error::Internal;
        std::snprintf(s.state.message, sizeof(s.state.message), "%s", "Cannot start media worker thread");
        return false;
    }
    return true;
}
bool OneSegMediaWorker::acceptTs(const uint8_t* data, size_t bytes, uint64_t generation)
{
    auto& s = *impl_;
    if (!s.active.load() || s.cancel.load() || s.eof.load()) return false;
    if (!data || bytes != 188 || data[0] != 0x47 || generation != s.config.generation)
    {
        s.fail(Error::Input);
        return false;
    }
    const uint32_t write = s.written.load(std::memory_order_relaxed), read = s.read.load(std::memory_order_acquire);
    if (write - read >= Impl::slots)
    {
        s.fail(Error::QueueFull);
        return false;
    }
    std::memcpy(s.packets[write % Impl::slots].data(), data, bytes);
    s.written.store(write + 1, std::memory_order_release);
    if (write - read + 1 > s.high_water.load()) s.high_water.store(write - read + 1);
    return true;
}
void OneSegMediaWorker::endInput() { impl_->eof = true; }
void OneSegMediaWorker::requestStop() { impl_->cancel = true; }
void OneSegMediaWorker::join()
{
    if (impl_->worker.joinable()) impl_->worker.join();
}
bool OneSegMediaWorker::healthy() const { return impl_->active.load() && !impl_->cancel.load(); }
OneSegMediaWorker::Snapshot OneSegMediaWorker::snapshot() const
{
    auto& s = *impl_;
    std::lock_guard<std::mutex> lock(s.mutex);
    auto result = s.state;
    result.ts_packets = s.written.load();
    result.queue_high_water = s.high_water.load();
    return result;
}
bool OneSegMediaWorker::copyLatestFrame(uint64_t generation, uint8_t* destination, size_t capacity, FrameInfo& info)
{
    return impl_->copyVideo(generation, false, 0, destination, capacity, info);
}
bool OneSegMediaWorker::copyFrameAt(uint64_t generation, int64_t pts, uint8_t* destination, size_t capacity, FrameInfo& info)
{
    return impl_->copyVideo(generation, true, pts, destination, capacity, info);
}
bool OneSegMediaWorker::latestFrameInfo(uint64_t generation, FrameInfo& info) const
{
    auto& s = *impl_;
    std::unique_lock<std::mutex> lock(s.frame_mutex, std::try_to_lock);
    if (!lock) return false;
    const int slot = s.published.load(std::memory_order_acquire);
    if (slot < 0 || s.images[slot].info.generation != generation) return false;
    info = s.images[slot].info;
    return true;
}
} // namespace platform::linux_runtime
