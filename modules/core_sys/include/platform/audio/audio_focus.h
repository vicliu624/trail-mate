#pragma once
#include <cstdint>
namespace platform::audio
{
enum class FocusOwner : uint8_t
{
    Notification,
    VoiceMessage,
    Walkie,
    Call,
    Sstv,
    FmRadio,
    Television
};
struct FocusToken
{
    uint32_t generation = 0;
    uint8_t slot = 0;
    explicit operator bool() const { return generation != 0; }
};
struct MusicFocusClient
{
    void* context = nullptr;
    // Must acknowledge after closing the physical codec/I2S, with a bounded wait.
    bool (*suspend)(void*) = nullptr;
    // Wake only. The worker rechecks music_blocked() and the user's play intent before opening audio.
    void (*resume)(void*) = nullptr;
};
void register_music_focus_client(MusicFocusClient client);
FocusToken request_focus(FocusOwner owner);
FocusToken request_exclusive_focus(FocusOwner owner);
void release_focus(FocusToken token);
bool music_blocked();
class FocusScope
{
    FocusToken token_;

  public:
    explicit FocusScope(FocusOwner owner) : token_(request_focus(owner)) {}
    FocusScope(FocusOwner owner, bool exclusive)
        : token_(exclusive ? request_exclusive_focus(owner) : request_focus(owner)) {}
    ~FocusScope() { release_focus(token_); }
    explicit operator bool() const { return bool(token_); }
    FocusScope(const FocusScope&) = delete;
    FocusScope& operator=(const FocusScope&) = delete;
};
} // namespace platform::audio
