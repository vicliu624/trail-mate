#include "platform/device/evdev_input.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <unistd.h>

namespace trailmate::cardputer_zero::platform::device
{
namespace
{

// ---------------------------------------------------------------------------
// Key mapping: Linux key code -> InputEvent
// ---------------------------------------------------------------------------

struct KeyMapping
{
    int linux_key;
    app::InputKey target;
    char ascii{'\0'};
};

// std::to_array<KeyMapping> avoids hand-counting and CTAD ambiguity.
constexpr auto kKeyMap = std::to_array<KeyMapping>({
    // Navigation
    {KEY_UP, app::InputKey::Up},
    {KEY_DOWN, app::InputKey::Down},
    {KEY_LEFT, app::InputKey::Left},
    {KEY_RIGHT, app::InputKey::Right},
    {KEY_HOME, app::InputKey::Home},
    {KEY_END, app::InputKey::Next},

    // Action keys
    {KEY_ENTER, app::InputKey::Enter},
    {KEY_KPENTER, app::InputKey::Enter},
    {KEY_BACKSPACE, app::InputKey::Backspace},
    {KEY_TAB, app::InputKey::Tab},
    {KEY_ESC, app::InputKey::Power},

    // Modifiers (mapped as modifiers – input handling may need expansion later)
    {KEY_LEFTSHIFT, app::InputKey::Shift},
    {KEY_RIGHTSHIFT, app::InputKey::Shift},
    {KEY_LEFTCTRL, app::InputKey::Ctrl},
    {KEY_RIGHTCTRL, app::InputKey::Ctrl},
    {KEY_LEFTALT, app::InputKey::Alt},
    {KEY_RIGHTALT, app::InputKey::Alt},

    // Printable ASCII (lowercase by default; shift handled by caller)
    {KEY_A, app::InputKey::Character, 'a'},
    {KEY_B, app::InputKey::Character, 'b'},
    {KEY_C, app::InputKey::Character, 'c'},
    {KEY_D, app::InputKey::Character, 'd'},
    {KEY_E, app::InputKey::Character, 'e'},
    {KEY_F, app::InputKey::Character, 'f'},
    {KEY_G, app::InputKey::Character, 'g'},
    {KEY_H, app::InputKey::Character, 'h'},
    {KEY_I, app::InputKey::Character, 'i'},
    {KEY_J, app::InputKey::Character, 'j'},
    {KEY_K, app::InputKey::Character, 'k'},
    {KEY_L, app::InputKey::Character, 'l'},
    {KEY_M, app::InputKey::Character, 'm'},
    {KEY_N, app::InputKey::Character, 'n'},
    {KEY_O, app::InputKey::Character, 'o'},
    {KEY_P, app::InputKey::Character, 'p'},
    {KEY_Q, app::InputKey::Character, 'q'},
    {KEY_R, app::InputKey::Character, 'r'},
    {KEY_S, app::InputKey::Character, 's'},
    {KEY_T, app::InputKey::Character, 't'},
    {KEY_U, app::InputKey::Character, 'u'},
    {KEY_V, app::InputKey::Character, 'v'},
    {KEY_W, app::InputKey::Character, 'w'},
    {KEY_X, app::InputKey::Character, 'x'},
    {KEY_Y, app::InputKey::Character, 'y'},
    {KEY_Z, app::InputKey::Character, 'z'},

    // Digits
    {KEY_0, app::InputKey::Character, '0'},
    {KEY_1, app::InputKey::Character, '1'},
    {KEY_2, app::InputKey::Character, '2'},
    {KEY_3, app::InputKey::Character, '3'},
    {KEY_4, app::InputKey::Character, '4'},
    {KEY_5, app::InputKey::Character, '5'},
    {KEY_6, app::InputKey::Character, '6'},
    {KEY_7, app::InputKey::Character, '7'},
    {KEY_8, app::InputKey::Character, '8'},
    {KEY_9, app::InputKey::Character, '9'},

    // Space & punctuation
    {KEY_SPACE, app::InputKey::Character, ' '},
    {KEY_COMMA, app::InputKey::Character, ','},
    {KEY_DOT, app::InputKey::Character, '.'},
    {KEY_SLASH, app::InputKey::Character, '/'},
    {KEY_SEMICOLON, app::InputKey::Character, ';'},
    {KEY_APOSTROPHE, app::InputKey::Character, '\''},
    {KEY_MINUS, app::InputKey::Character, '-'},
    {KEY_EQUAL, app::InputKey::Character, '='},
    {KEY_LEFTBRACE, app::InputKey::Character, '['},
    {KEY_RIGHTBRACE, app::InputKey::Character, ']'},
    {KEY_BACKSLASH, app::InputKey::Character, '\\'},
    {KEY_GRAVE, app::InputKey::Character, '`'},
});

const KeyMapping* findMapping(int linux_key)
{
    for (const auto& entry : kKeyMap)
    {
        if (entry.linux_key == linux_key)
        {
            return &entry;
        }
    }
    return nullptr;
}

app::InputEvent makeInputEvent(const KeyMapping& mapping, bool shift_held)
{
    app::InputEvent event{};
    event.key = mapping.target;

    if (mapping.target == app::InputKey::Character && mapping.ascii != '\0')
    {
        if (shift_held)
        {
            // Simple US keyboard shift mapping for letters and common symbols.
            switch (mapping.ascii)
            {
            case 'a':
                event.text = 'A';
                break;
            case 'b':
                event.text = 'B';
                break;
            case 'c':
                event.text = 'C';
                break;
            case 'd':
                event.text = 'D';
                break;
            case 'e':
                event.text = 'E';
                break;
            case 'f':
                event.text = 'F';
                break;
            case 'g':
                event.text = 'G';
                break;
            case 'h':
                event.text = 'H';
                break;
            case 'i':
                event.text = 'I';
                break;
            case 'j':
                event.text = 'J';
                break;
            case 'k':
                event.text = 'K';
                break;
            case 'l':
                event.text = 'L';
                break;
            case 'm':
                event.text = 'M';
                break;
            case 'n':
                event.text = 'N';
                break;
            case 'o':
                event.text = 'O';
                break;
            case 'p':
                event.text = 'P';
                break;
            case 'q':
                event.text = 'Q';
                break;
            case 'r':
                event.text = 'R';
                break;
            case 's':
                event.text = 'S';
                break;
            case 't':
                event.text = 'T';
                break;
            case 'u':
                event.text = 'U';
                break;
            case 'v':
                event.text = 'V';
                break;
            case 'w':
                event.text = 'W';
                break;
            case 'x':
                event.text = 'X';
                break;
            case 'y':
                event.text = 'Y';
                break;
            case 'z':
                event.text = 'Z';
                break;
            case '1':
                event.text = '!';
                break;
            case '2':
                event.text = '@';
                break;
            case '3':
                event.text = '#';
                break;
            case '4':
                event.text = '$';
                break;
            case '5':
                event.text = '%';
                break;
            case '6':
                event.text = '^';
                break;
            case '7':
                event.text = '&';
                break;
            case '8':
                event.text = '*';
                break;
            case '9':
                event.text = '(';
                break;
            case '0':
                event.text = ')';
                break;
            case '-':
                event.text = '_';
                break;
            case '=':
                event.text = '+';
                break;
            case '[':
                event.text = '{';
                break;
            case ']':
                event.text = '}';
                break;
            case '\\':
                event.text = '|';
                break;
            case ';':
                event.text = ':';
                break;
            case '\'':
                event.text = '"';
                break;
            case ',':
                event.text = '<';
                break;
            case '.':
                event.text = '>';
                break;
            case '/':
                event.text = '?';
                break;
            case '`':
                event.text = '~';
                break;
            default:
                event.text = mapping.ascii;
                break;
            }
        }
        else
        {
            event.text = mapping.ascii;
        }
    }

    // Build a short human-readable label for logging
    char label_buf[32]{};
    if (mapping.ascii != '\0')
    {
        std::snprintf(label_buf, sizeof(label_buf), "%c", event.text);
    }
    else
    {
        const char* label_str = "?";
        switch (mapping.target)
        {
        case app::InputKey::Enter:
            label_str = "Enter";
            break;
        case app::InputKey::Backspace:
            label_str = "Bksp";
            break;
        case app::InputKey::Tab:
            label_str = "Tab";
            break;
        case app::InputKey::Up:
            label_str = "Up";
            break;
        case app::InputKey::Down:
            label_str = "Down";
            break;
        case app::InputKey::Left:
            label_str = "Left";
            break;
        case app::InputKey::Right:
            label_str = "Right";
            break;
        case app::InputKey::Home:
            label_str = "Home";
            break;
        case app::InputKey::Next:
            label_str = "Next";
            break;
        case app::InputKey::Power:
            label_str = "Power";
            break;
        case app::InputKey::Shift:
            label_str = "Shift";
            break;
        case app::InputKey::Ctrl:
            label_str = "Ctrl";
            break;
        case app::InputKey::Alt:
            label_str = "Alt";
            break;
        default:
            break;
        }
        label_str = label_str;
        std::snprintf(label_buf, sizeof(label_buf), "%s", label_str);
    }
    event.label = std::string(label_buf);

    return event;
}

// ---------------------------------------------------------------------------
// Auto-discovery helpers
// ---------------------------------------------------------------------------

std::vector<std::string> candidateDevicePaths()
{
    std::vector<std::string> paths;

#if defined(__linux__)
    // 1. Env override (checked by caller, not here).
    // 2. by-path keyboard entries (guard against missing directory).
    {
        std::error_code ec;
        const std::filesystem::path by_path_dir("/dev/input/by-path");
        if (std::filesystem::is_directory(by_path_dir, ec) && !ec)
        {
            for (std::filesystem::directory_iterator it(by_path_dir, ec), end;
                 !ec && it != end; it.increment(ec))
            {
                const auto filename = it->path().filename().string();
                if (filename.find("-event-kbd") != std::string::npos ||
                    filename.find("-kbd") != std::string::npos)
                {
                    paths.push_back(it->path().string());
                }
            }
        }
    }

    // 3. by-id keyboard entries (guard against missing directory).
    {
        std::error_code ec;
        const std::filesystem::path by_id_dir("/dev/input/by-id");
        if (std::filesystem::is_directory(by_id_dir, ec) && !ec)
        {
            for (std::filesystem::directory_iterator it(by_id_dir, ec), end;
                 !ec && it != end; it.increment(ec))
            {
                const auto filename = it->path().filename().string();
                if (filename.find("-event-kbd") != std::string::npos ||
                    filename.find("-kbd") != std::string::npos)
                {
                    paths.push_back(it->path().string());
                }
            }
        }
    }

    // 4. Fallback: scan /dev/input/event*.
    for (int i = 0; i < 32; ++i)
    {
        char buf[64]{};
        std::snprintf(buf, sizeof(buf), "/dev/input/event%d", i);
        paths.push_back(std::string(buf));
    }
#endif

    return paths;
}

} // namespace

// ===================================================================
// EvdevInput::Impl
// ===================================================================

struct EvdevInput::Impl
{
    std::string device_path{};
    int fd{-1};
    bool shift_held{false};
    bool ctrl_held{false};
    bool alt_held{false};

    bool openDevice(const std::string& path)
    {
        closeDevice();

        if (path.empty())
        {
            return false;
        }

        fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0)
        {
            std::fprintf(stderr, "[evdev] Failed to open %s: %s\n",
                         path.c_str(), std::strerror(errno));
            return false;
        }

        device_path = path;
        std::fprintf(stderr, "[evdev] Opened input device: %s\n", path.c_str());
        return true;
    }

    void closeDevice()
    {
        if (fd >= 0)
        {
            ::close(fd);
            fd = -1;
        }
        device_path.clear();
        shift_held = false;
        ctrl_held = false;
        alt_held = false;
    }

    bool tryAutoDiscovery()
    {
#if defined(__linux__)
        for (const auto& path : candidateDevicePaths())
        {
            // Quick-existence check before attempting open.
            if (!std::filesystem::exists(path))
            {
                continue;
            }

            if (openDevice(path))
            {
                // Verify it actually supports EV_KEY events.
                unsigned long ev_bits[EV_MAX / (sizeof(unsigned long) * 8) + 1]{};
                if (::ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) >= 0)
                {
                    if (ev_bits[EV_KEY / (sizeof(unsigned long) * 8)] &
                        (1UL << (EV_KEY % (sizeof(unsigned long) * 8))))
                    {
                        // Also verify it has at least one keyboard key.
                        unsigned long key_bits[KEY_MAX / (sizeof(unsigned long) * 8) + 1]{};
                        if (::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) >= 0)
                        {
                            bool has_keys = false;
                            for (int k = KEY_ESC; k <= KEY_DOWN && !has_keys; ++k)
                            {
                                if (key_bits[k / (sizeof(unsigned long) * 8)] &
                                    (1UL << (k % (sizeof(unsigned long) * 8))))
                                {
                                    has_keys = true;
                                }
                            }
                            if (has_keys)
                            {
                                std::fprintf(stderr, "[evdev] Auto-discovered keyboard: %s\n",
                                             path.c_str());
                                return true;
                            }
                        }
                    }
                }
                // Not a keyboard — try next candidate.
                closeDevice();
            }
        }
#endif
        return false;
    }
};

// ===================================================================
// EvdevInput (public)
// ===================================================================

EvdevInput::EvdevInput(std::string device_path)
    : impl_(std::make_unique<Impl>())
{
#if defined(__linux__)
    // 1. Env override: TRAIL_MATE_INPUT_DEVICE
    if (device_path.empty())
    {
        const char* env = std::getenv("TRAIL_MATE_INPUT_DEVICE");
        if (env != nullptr && env[0] != '\0')
        {
            device_path = env;
        }
    }

    // 2. Explicit device path (or env override)
    if (!device_path.empty())
    {
        if (impl_->openDevice(device_path))
        {
            return;
        }
        std::fprintf(stderr, "[evdev] Explicit device '%s' failed; trying auto-discovery.\n",
                     device_path.c_str());
    }

    // 3. Auto-discovery
    if (impl_->tryAutoDiscovery())
    {
        return;
    }

    std::fprintf(stderr,
                 "[evdev] No keyboard input device found. "
                 "UI will display but cannot accept keyboard input. "
                 "Set TRAIL_MATE_INPUT_DEVICE to override.\n");
#else
    (void)device_path;
    std::fprintf(stderr, "[evdev] Non-Linux host: input disabled.\n");
#endif
}

EvdevInput::~EvdevInput()
{
    impl_->closeDevice();
}

bool EvdevInput::isOpen() const noexcept
{
    return impl_->fd >= 0;
}

std::vector<app::InputEvent> EvdevInput::drainInput()
{
    std::vector<app::InputEvent> events;

#if defined(__linux__)
    if (impl_->fd < 0)
    {
        return events;
    }

    struct input_event raw
    {
    };

    for (;;)
    {
        const ssize_t bytes = ::read(impl_->fd, &raw, sizeof(raw));
        if (bytes < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break; // no more events
            }
            // Device error — close and stop.
            std::fprintf(stderr, "[evdev] Read error on %s: %s\n",
                         impl_->device_path.c_str(), std::strerror(errno));
            impl_->closeDevice();
            break;
        }
        if (bytes == 0)
        {
            break;
        }
        if (static_cast<std::size_t>(bytes) < sizeof(raw))
        {
            continue;
        }

        // We only handle EV_KEY events.
        if (raw.type != EV_KEY)
        {
            continue;
        }

        // Track modifier state.
        if (raw.code == KEY_LEFTSHIFT || raw.code == KEY_RIGHTSHIFT)
        {
            impl_->shift_held = (raw.value != 0);
        }
        if (raw.code == KEY_LEFTCTRL || raw.code == KEY_RIGHTCTRL)
        {
            impl_->ctrl_held = (raw.value != 0);
        }
        if (raw.code == KEY_LEFTALT || raw.code == KEY_RIGHTALT)
        {
            impl_->alt_held = (raw.value != 0);
        }

        // Ignore key releases and repeats — we only emit on press.
        // (Repeat is handled by the UI layer via LVGL key repeat.)
        if (raw.value != 1) // EV_KEY value: 0=release, 1=press, 2=repeat
        {
            continue;
        }

        const KeyMapping* mapping = findMapping(raw.code);
        if (mapping == nullptr)
        {
            // Unmapped key — silently ignore.
            continue;
        }

        events.push_back(makeInputEvent(*mapping, impl_->shift_held));
    }
#endif

    return events;
}

const std::string& EvdevInput::devicePath() const noexcept
{
    return impl_->device_path;
}

bool EvdevInput::reopen(std::string device_path)
{
#if defined(__linux__)
    impl_->closeDevice();
    if (device_path.empty())
    {
        const char* env = std::getenv("TRAIL_MATE_INPUT_DEVICE");
        if (env != nullptr && env[0] != '\0')
        {
            device_path = env;
        }
    }
    if (!device_path.empty())
    {
        return impl_->openDevice(device_path);
    }
    return impl_->tryAutoDiscovery();
#else
    (void)device_path;
    return false;
#endif
}
} // namespace trailmate::cardputer_zero::platform::device
