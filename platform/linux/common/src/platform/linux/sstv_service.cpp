#include "platform/linux/sstv_service.h"
#if defined(TRAIL_MATE_LINUX_HAS_MEDIA)
#include "platform/audio/audio_focus.h"
#include "platform/linux/runtime_paths.h"
#include "platform/linux/sdl_pcm_input.h"
#include "sstv/receiver.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>

namespace platform::linux_runtime
{
namespace
{
using Status = ::platform::ui::sstv::Status;
using State = ::platform::ui::sstv::State;
using Receiver = ::sstv::core::Receiver;
constexpr std::size_t pixels = Receiver::width * Receiver::height;
class Service;
std::atomic<Service*> existing{nullptr};

std::string save_image(const uint16_t* frame)
{
    auto root = resolve_paths().sd_root / "sstv";
    if (const auto* configured = std::getenv("TRAIL_MATE_SSTV_OUTPUT_DIR"))
        if (*configured) root = configured;
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error) throw std::runtime_error("Cannot create SSTV image directory");
    const auto prefix = std::chrono::system_clock::now().time_since_epoch().count();
    auto temporary = (root / ("sstv-" + std::to_string(prefix) + "-XXXXXX")).string();
    const int descriptor = mkstemp(temporary.data());
    if (descriptor < 0) throw std::runtime_error("Cannot create SSTV image file");
    FILE* file = fdopen(descriptor, "wb");
    if (!file)
    {
        close(descriptor);
        std::filesystem::remove(temporary, error);
        throw std::runtime_error("Cannot open SSTV image file");
    }
    constexpr uint32_t row_bytes = Receiver::width * 3;
    static_assert(row_bytes % 4 == 0, "SSTV BMP rows must be aligned");
    constexpr uint32_t image_bytes = row_bytes * Receiver::height;
    std::array<uint8_t, 54> header{};
    header[0] = 'B';
    header[1] = 'M';
    const auto put32 = [&](unsigned offset, uint32_t value)
    {
        for (unsigned byte = 0; byte < 4; ++byte) header[offset + byte] = value >> (byte * 8);
    };
    put32(2, 54 + image_bytes);
    put32(10, 54);
    put32(14, 40);
    put32(18, Receiver::width);
    put32(22, Receiver::height);
    put32(34, image_bytes);
    header[26] = 1;
    header[28] = 24;
    bool success = std::fwrite(header.data(), 1, header.size(), file) == header.size();
    std::array<uint8_t, row_bytes> row;
    for (unsigned y = 0; y < Receiver::height && success; ++y)
    {
        for (unsigned x = 0; x < Receiver::width; ++x)
        {
            const auto value = frame[(Receiver::height - 1 - y) * Receiver::width + x];
            row[x * 3] = (value & 31) * 255 / 31;
            row[x * 3 + 1] = ((value >> 5) & 63) * 255 / 63;
            row[x * 3 + 2] = ((value >> 11) & 31) * 255 / 31;
        }
        success = std::fwrite(row.data(), 1, row.size(), file) == row.size();
    }
    if (success) success = std::fflush(file) == 0 && fsync(descriptor) == 0;
    if (std::fclose(file) != 0) success = false;
    const auto final = temporary + ".bmp";
    if (success) std::filesystem::rename(temporary, final, error);
    if (!success || error)
    {
        std::filesystem::remove(temporary, error);
        throw std::runtime_error("Cannot persist SSTV image");
    }
    return final;
}

class Service
{
  public:
    Service() { existing = this; }
    ~Service() { shutdown(); }
    bool start()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || active_) return false;
        active_ = true;
        status_ = {};
        status_.state = State::Waiting;
        error_.clear();
        saved_.clear();
        mode_ = "Auto";
        if (++epoch_ == 0) ++epoch_;
        pending_ = epoch_.load();
        try
        {
            if (!worker_.joinable()) worker_ = std::thread([this]
                                                           { run(); });
        }
        catch (...)
        {
            active_ = false;
            status_.state = State::Error;
            error_ = "Cannot start SSTV worker";
            return false;
        }
        wake_.notify_all();
        return true;
    }
    void stop()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++epoch_;
        pending_ = 0;
        active_ = false;
        if (status_.state != State::Complete && status_.state != State::Error) status_.state = State::Idle;
        wake_.notify_all();
    }
    void shutdown()
    {
        stopping_ = true;
        stop();
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
    }
    Status status()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return status_;
    }
    bool active()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_;
    }
    std::string text(unsigned index)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return index == 0 ? error_ : index == 1 ? saved_
                                                : mode_;
    }
    const uint16_t* frame()
    {
        thread_local std::array<uint16_t, pixels> copy;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!status_.has_image) return nullptr;
        copy = frame_;
        return copy.data();
    }

  private:
    void publish(uint32_t id, const Receiver& receiver)
    {
        const auto input = receiver.status();
        std::lock_guard<std::mutex> lock(mutex_);
        if (epoch_ != id) return;
        status_.state = input.state != ::sstv::core::ReceiveState::Waiting ? State::Receiving : State::Waiting;
        status_.line = input.line;
        status_.progress = input.progress;
        status_.audio_level = input.audio_level;
        status_.has_image = input.has_image;
        if (input.has_image) std::copy_n(receiver.frame(), pixels, frame_.data());
        mode_ = receiver.mode_name();
    }
    void receive(uint32_t id)
    {
        auto receiver = std::make_unique<Receiver>();
        std::ifstream file;
        if (const auto* path = std::getenv("TRAIL_MATE_SSTV_PCM_FILE"))
            if (*path)
            {
                std::error_code error;
                if (!std::filesystem::is_regular_file(path, error))
                    throw std::runtime_error("SSTV PCM input must be a regular file");
                file.open(path, std::ios::binary);
            }
        const bool file_requested = std::getenv("TRAIL_MATE_SSTV_PCM_FILE") && *std::getenv("TRAIL_MATE_SSTV_PCM_FILE");
        if (file_requested && !file) throw std::runtime_error("Cannot open SSTV PCM input file");
        std::unique_ptr<::platform::audio::FocusScope> focus;
        std::unique_ptr<SdlPcmInput> microphone;
        if (!file_requested)
        {
            focus = std::make_unique<::platform::audio::FocusScope>(::platform::audio::FocusOwner::Sstv, true);
            if (!*focus) throw std::runtime_error("Audio focus unavailable for SSTV");
            microphone = std::make_unique<SdlPcmInput>();
            if (!microphone->begin(Receiver::sample_rate, std::getenv("TRAIL_MATE_SSTV_INPUT_DEVICE")))
                throw std::runtime_error(microphone->last_error());
        }
        std::array<int16_t, 512> samples;
        uint64_t dropped = 0;
        auto publish_at = std::chrono::steady_clock::now();
        while (!stopping_ && epoch_ == id)
        {
            std::size_t count;
            if (file_requested)
            {
                std::array<uint8_t, sizeof(samples)> bytes_in;
                file.read(reinterpret_cast<char*>(bytes_in.data()), bytes_in.size());
                const auto bytes = file.gcount();
                if (bytes % 2) throw std::runtime_error("SSTV PCM file ends with a partial sample");
                count = bytes / 2;
                for (std::size_t index = 0; index < count; ++index)
                    samples[index] = static_cast<int16_t>(uint16_t(bytes_in[index * 2]) | uint16_t(bytes_in[index * 2 + 1]) << 8);
                if (!count) throw std::runtime_error("Input ended before a complete SSTV image");
            }
            else
            {
                const auto lost = microphone->dropped_samples();
                if (lost != dropped)
                {
                    receiver->reset();
                    dropped = lost;
                }
                if (!microphone->active()) throw std::runtime_error("SSTV audio input disconnected");
                count = microphone->read(samples.data(), samples.size());
            }
            if (!count)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            receiver->feed(samples.data(), count);
            const auto now = std::chrono::steady_clock::now();
            if (now >= publish_at || receiver->status().state == ::sstv::core::ReceiveState::Complete)
            {
                publish(id, *receiver);
                publish_at = now + std::chrono::milliseconds(50);
            }
            if (receiver->status().state == ::sstv::core::ReceiveState::Complete)
            {
                microphone.reset();
                focus.reset();
                const auto saved = save_image(receiver->frame());
                std::lock_guard<std::mutex> lock(mutex_);
                if (epoch_ == id)
                {
                    saved_ = saved;
                    active_ = false;
                    status_.state = State::Complete;
                }
                return;
            }
        }
    }
    void run()
    {
        while (!stopping_)
        {
            uint32_t id;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this]
                           { return stopping_ || pending_; });
                if (stopping_) return;
                id = pending_;
                pending_ = 0;
            }
            try
            {
                receive(id);
            }
            catch (const std::exception& error)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (epoch_ == id)
                {
                    status_.state = State::Error;
                    error_ = error.what();
                    active_ = false;
                }
            }
        }
    }
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    std::atomic_bool stopping_{false};
    std::atomic<uint32_t> epoch_{0};
    uint32_t pending_ = 0;
    bool active_ = false;
    Status status_;
    std::array<uint16_t, pixels> frame_{};
    std::string error_, saved_, mode_ = "Auto";
};
Service& service()
{
    static Service value;
    return value;
}
const char* text(unsigned index)
{
    thread_local std::array<std::string, 3> values;
    values[index] = service().text(index);
    return values[index].c_str();
}
} // namespace
bool native_sstv_built() { return true; }
bool start_native_sstv() { return service().start(); }
void stop_native_sstv() { service().stop(); }
void shutdown_native_sstv()
{
    if (auto* value = existing.load()) value->shutdown();
}
bool native_sstv_active() { return service().active(); }
::platform::ui::sstv::Status native_sstv_status() { return service().status(); }
const char* native_sstv_error() { return text(0); }
const char* native_sstv_saved_path() { return text(1); }
const char* native_sstv_mode() { return text(2); }
const uint16_t* native_sstv_frame() { return service().frame(); }
} // namespace platform::linux_runtime
#else
namespace platform::linux_runtime
{
bool native_sstv_built() { return false; }
bool start_native_sstv() { return false; }
void stop_native_sstv() {}
void shutdown_native_sstv() {}
bool native_sstv_active() { return false; }
::platform::ui::sstv::Status native_sstv_status() { return {}; }
const char* native_sstv_error() { return "Native SSTV audio is not built"; }
const char* native_sstv_saved_path() { return ""; }
const char* native_sstv_mode() { return "Auto"; }
const uint16_t* native_sstv_frame() { return nullptr; }
} // namespace platform::linux_runtime
#endif
