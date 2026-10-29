#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "platform/desktop/device_options.h"

namespace platform::desktop
{
// Owns window, display buffers and input. Contains no board or page decisions.
// LVGL must be initialized before construction and outlive this object.
class SdlLvglHost
{
  public:
    explicit SdlLvglHost(const DeviceOptions& options);
    ~SdlLvglHost();
    SdlLvglHost(const SdlLvglHost&) = delete;
    SdlLvglHost& operator=(const SdlLvglHost&) = delete;
    bool pump();
    // Return true to consume a key. Key 0/released cancels held input on focus loss.
    // The observer context must outlive this host or be detached before destruction.
    using KeyObserver = bool (*)(void*, uint32_t, bool);
    void setKeyObserver(KeyObserver observer, void* context);
    struct InputActivity
    {
        enum class Kind
        {
            Key,
            Pointer,
            Text,
            Cancel
        } kind;
        uint32_t key = 0;
        bool pressed = false;
        bool motion = false;
        int x = 0;
        int y = 0;
    };
    using InputObserver = bool (*)(void*, const InputActivity&);
    // Runs before input reaches pages, text editors or key observers. Return
    // true to consume wake input and cancel any previously queued gesture.
    void setInputObserver(InputObserver observer, void* context);
    using TextEnabled = bool (*)(void*);
    using TextCommit = void (*)(void*, const char*);
    using TextComposition = void (*)(void*, const char*, int, int);
    // Callbacks run on the UI thread; their context must outlive the host.
    void setTextObserver(TextEnabled enabled, TextCommit commit, void* context, TextComposition composition = nullptr);
    void present();
    void setRotation(unsigned rotation);
    void saveScreenshot(const std::string& path);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    InputObserver input_observer_ = nullptr;
    void* input_context_ = nullptr;
};
} // namespace platform::desktop
