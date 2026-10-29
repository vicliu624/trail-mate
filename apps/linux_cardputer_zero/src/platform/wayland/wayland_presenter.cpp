#include "platform/wayland/wayland_presenter.h"
#include "platform/wayland/wayland_text_input.h"

#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <utility>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "core/canvas.h"
#include "core/display_profile.h"

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

namespace trailmate::cardputer_zero::platform::wayland
{
namespace
{

namespace app = trailmate::cardputer_zero::app;
namespace core = trailmate::cardputer_zero::core;

constexpr int kWidth = core::kDisplayWidth;
constexpr int kHeight = core::kDisplayHeight;
constexpr int kStride = kWidth * 4;
constexpr int kBufferSize = kStride * kHeight;

int createAnonymousFile(std::size_t size)
{
    int fd = -1;
#ifdef SYS_memfd_create
    fd = static_cast<int>(
        syscall(SYS_memfd_create, "trailmate-cardputer-zero-wayland",
                MFD_CLOEXEC));
    if (fd >= 0)
    {
        if (ftruncate(fd, static_cast<off_t>(size)) == 0)
        {
            return fd;
        }
        close(fd);
    }
#endif

    char name[] = "/trailmate-cardputer-zero-wayland-XXXXXX";
    fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0)
    {
        return -1;
    }
    shm_unlink(name);
    if (ftruncate(fd, static_cast<off_t>(size)) < 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

[[nodiscard]] std::uint32_t packPixel(core::Color color) noexcept
{
    return 0xFF000000U |
           (static_cast<std::uint32_t>(color.r) << 16U) |
           (static_cast<std::uint32_t>(color.g) << 8U) |
           static_cast<std::uint32_t>(color.b);
}

void enqueueSpecial(std::vector<app::InputEvent>& queue,
                    app::InputKey key,
                    std::string label)
{
    queue.push_back(app::InputEvent{key, std::move(label), '\0'});
}

[[nodiscard]] std::vector<app::InputEvent> translateKeysym(xkb_keysym_t sym)
{
    std::vector<app::InputEvent> events;
    switch (sym)
    {
    case XKB_KEY_BackSpace:
    case XKB_KEY_Delete:
        enqueueSpecial(events, app::InputKey::Backspace, "DEL");
        break;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        enqueueSpecial(events, app::InputKey::Enter, "OK");
        break;
    case XKB_KEY_Tab:
        enqueueSpecial(events, app::InputKey::Tab, "TAB");
        break;
    case XKB_KEY_Home:
        enqueueSpecial(events, app::InputKey::Home, "HOME");
        break;
    case XKB_KEY_End:
    case XKB_KEY_Page_Down:
        enqueueSpecial(events, app::InputKey::Next, "NEXT");
        break;
    case XKB_KEY_Escape:
        enqueueSpecial(events, app::InputKey::Power, "POWER");
        break;
    case XKB_KEY_Shift_L:
    case XKB_KEY_Shift_R:
        enqueueSpecial(events, app::InputKey::Shift, "SHIFT");
        break;
    case XKB_KEY_Control_L:
    case XKB_KEY_Control_R:
        enqueueSpecial(events, app::InputKey::Ctrl, "CTRL");
        break;
    case XKB_KEY_Alt_L:
    case XKB_KEY_Alt_R:
        enqueueSpecial(events, app::InputKey::Alt, "ALT");
        break;
    case XKB_KEY_Left:
        enqueueSpecial(events, app::InputKey::Left, "LEFT");
        break;
    case XKB_KEY_Right:
        enqueueSpecial(events, app::InputKey::Right, "RIGHT");
        break;
    case XKB_KEY_Up:
        enqueueSpecial(events, app::InputKey::Up, "UP");
        break;
    case XKB_KEY_Down:
        enqueueSpecial(events, app::InputKey::Down, "DOWN");
        break;
    case XKB_KEY_F1:
        enqueueSpecial(events, app::InputKey::F1, "F1");
        break;
    default:
        break;
    }
    return events;
}

[[nodiscard]] app::InputEvent makeCharacter(char ch)
{
    std::string label;
    if (ch == ' ')
    {
        label = "SPACE";
    }
    else
    {
        label.push_back(ch);
    }
    return app::makeCharacterInput(ch, std::move(label));
}

} // namespace

struct WaylandPresenter::Impl
{
    struct Buffer
    {
        wl_buffer* buffer{};
        std::uint32_t* pixels{};
        bool busy{false};
    };

    Impl()
    {
        display = wl_display_connect(nullptr);
        if (display == nullptr)
        {
            throw std::runtime_error("wl_display_connect failed");
        }

        registry = wl_display_get_registry(display);
        wl_registry_add_listener(registry, &kRegistryListener, this);
        wl_display_roundtrip(display);
        wl_display_roundtrip(display);

        if (compositor == nullptr || shm == nullptr || wm == nullptr)
        {
            throw std::runtime_error(
                "Wayland compositor is missing wl_compositor, wl_shm, or "
                "xdg_wm_base");
        }

        surface = wl_compositor_create_surface(compositor);
        if (surface == nullptr)
        {
            throw std::runtime_error("wl_compositor_create_surface failed");
        }
        if (text_manager && seat) text_input = std::make_unique<WaylandTextInput>(text_manager, seat, surface, pending_inputs);

        xdg_surface_obj = xdg_wm_base_get_xdg_surface(wm, surface);
        xdg_surface_add_listener(xdg_surface_obj, &kSurfaceListener, this);
        toplevel = xdg_surface_get_toplevel(xdg_surface_obj);
        xdg_toplevel_add_listener(toplevel, &kToplevelListener, this);
        xdg_toplevel_set_title(toplevel, "Trail Mate Cardputer Zero");
        xdg_toplevel_set_app_id(toplevel,
                                "io.github.vicliu624.trailmate");
        xdg_toplevel_set_min_size(toplevel, kWidth, kHeight);
        xdg_toplevel_set_max_size(toplevel, kWidth, kHeight);
        if (decoration_manager != nullptr)
        {
            decoration =
                zxdg_decoration_manager_v1_get_toplevel_decoration(
                    decoration_manager,
                    toplevel);
            zxdg_toplevel_decoration_v1_set_mode(
                decoration,
                ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
        }

        if (!createBuffer(buffers[0]) || !createBuffer(buffers[1]))
        {
            throw std::runtime_error("Wayland shm buffer allocation failed");
        }

        wl_surface_commit(surface);
        wl_display_flush(display);
    }

    ~Impl()
    {
        text_input.reset();
        if (text_manager) zwp_text_input_manager_v3_destroy(text_manager);
        for (auto& buffer : buffers)
        {
            if (buffer.buffer != nullptr)
            {
                wl_buffer_destroy(buffer.buffer);
            }
            if (buffer.pixels != nullptr)
            {
                munmap(buffer.pixels, kBufferSize);
            }
        }
        if (xkb_state_ != nullptr)
        {
            xkb_state_unref(xkb_state_);
        }
        if (xkb_keymap_ != nullptr)
        {
            xkb_keymap_unref(xkb_keymap_);
        }
        if (xkb_context_ != nullptr)
        {
            xkb_context_unref(xkb_context_);
        }
        if (keyboard != nullptr)
        {
            wl_keyboard_destroy(keyboard);
        }
        if (seat != nullptr)
        {
            wl_seat_destroy(seat);
        }
        if (decoration != nullptr)
        {
            zxdg_toplevel_decoration_v1_destroy(decoration);
        }
        if (toplevel != nullptr)
        {
            xdg_toplevel_destroy(toplevel);
        }
        if (xdg_surface_obj != nullptr)
        {
            xdg_surface_destroy(xdg_surface_obj);
        }
        if (surface != nullptr)
        {
            wl_surface_destroy(surface);
        }
        if (wm != nullptr)
        {
            xdg_wm_base_destroy(wm);
        }
        if (decoration_manager != nullptr)
        {
            zxdg_decoration_manager_v1_destroy(decoration_manager);
        }
        if (shm != nullptr)
        {
            wl_shm_destroy(shm);
        }
        if (compositor != nullptr)
        {
            wl_compositor_destroy(compositor);
        }
        if (registry != nullptr)
        {
            wl_registry_destroy(registry);
        }
        if (display != nullptr)
        {
            wl_display_disconnect(display);
        }
    }

    bool createBuffer(Buffer& buffer)
    {
        const int fd = createAnonymousFile(kBufferSize);
        if (fd < 0)
        {
            return false;
        }

        void* mapped =
            mmap(nullptr, kBufferSize, PROT_READ | PROT_WRITE, MAP_SHARED,
                 fd, 0);
        if (mapped == MAP_FAILED)
        {
            close(fd);
            return false;
        }

        wl_shm_pool* pool = wl_shm_create_pool(shm, fd, kBufferSize);
        if (pool == nullptr)
        {
            munmap(mapped, kBufferSize);
            close(fd);
            return false;
        }

        buffer.buffer =
            wl_shm_pool_create_buffer(pool, 0, kWidth, kHeight, kStride,
                                      WL_SHM_FORMAT_XRGB8888);
        if (buffer.buffer == nullptr)
        {
            wl_shm_pool_destroy(pool);
            munmap(mapped, kBufferSize);
            close(fd);
            return false;
        }

        buffer.pixels = static_cast<std::uint32_t*>(mapped);
        buffer.busy = false;
        wl_buffer_add_listener(buffer.buffer, &kBufferListener, &buffer);
        wl_shm_pool_destroy(pool);
        close(fd);
        return buffer.buffer != nullptr;
    }

    [[nodiscard]] Buffer* nextBuffer()
    {
        for (auto& buffer : buffers)
        {
            if (!buffer.busy)
            {
                return &buffer;
            }
        }
        return nullptr;
    }

    void updateKeymap(int fd, std::uint32_t size)
    {
        char* map = static_cast<char*>(
            mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
        if (map == MAP_FAILED)
        {
            close(fd);
            return;
        }

        if (xkb_context_ == nullptr)
        {
            xkb_context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        }
        if (xkb_keymap_ != nullptr)
        {
            xkb_keymap_unref(xkb_keymap_);
        }
        if (xkb_state_ != nullptr)
        {
            xkb_state_unref(xkb_state_);
        }

        xkb_keymap_ = xkb_keymap_new_from_string(
            xkb_context_,
            map,
            XKB_KEYMAP_FORMAT_TEXT_V1,
            XKB_KEYMAP_COMPILE_NO_FLAGS);
        xkb_state_ = xkb_keymap_ != nullptr ? xkb_state_new(xkb_keymap_)
                                            : nullptr;

        munmap(map, size);
        close(fd);
    }

    void handleKey(std::uint32_t key, std::uint32_t state)
    {
        if (state != WL_KEYBOARD_KEY_STATE_PRESSED || xkb_state_ == nullptr)
        {
            return;
        }

        const auto keycode = static_cast<xkb_keycode_t>(key + 8U);
        const xkb_keysym_t sym = xkb_state_key_get_one_sym(xkb_state_,
                                                           keycode);
        auto translated = translateKeysym(sym);
        if (!translated.empty())
        {
            pending_inputs.insert(pending_inputs.end(), translated.begin(),
                                  translated.end());
            return;
        }

        char utf8[64] = {};
        const int len =
            xkb_state_key_get_utf8(xkb_state_, keycode, utf8, sizeof(utf8));
        if (len > 0 && len < static_cast<int>(sizeof(utf8)) && text_state.enabled && app::validUtf8(utf8) &&
            static_cast<unsigned char>(utf8[0]) >= 0x20 && pending_inputs.size() < 128)
        {
            app::InputEvent event;
            event.key = app::InputKey::TextEdit;
            event.edit.focus = text_state.focus;
            event.edit.commit = utf8;
            pending_inputs.push_back(std::move(event));
            return;
        }
        if (len == 1)
        {
            const auto value = static_cast<unsigned char>(utf8[0]);
            if (value >= 0x20U && value <= 0x7EU)
            {
                pending_inputs.push_back(makeCharacter(utf8[0]));
            }
        }
    }

    static void registryGlobal(void* data,
                               wl_registry* registry,
                               std::uint32_t name,
                               const char* interface,
                               std::uint32_t version)
    {
        auto* self = static_cast<Impl*>(data);
        if (std::strcmp(interface, wl_compositor_interface.name) == 0)
        {
            self->compositor = static_cast<wl_compositor*>(
                wl_registry_bind(registry, name, &wl_compositor_interface,
                                 std::min(version, 4U)));
        }
        else if (std::strcmp(interface, zwp_text_input_manager_v3_interface.name) == 0)
        {
            self->text_manager = static_cast<zwp_text_input_manager_v3*>(wl_registry_bind(registry, name, &zwp_text_input_manager_v3_interface, 1));
        }
        else if (std::strcmp(interface, wl_shm_interface.name) == 0)
        {
            self->shm = static_cast<wl_shm*>(
                wl_registry_bind(registry, name, &wl_shm_interface, 1));
        }
        else if (std::strcmp(interface, wl_seat_interface.name) == 0)
        {
            self->seat = static_cast<wl_seat*>(
                wl_registry_bind(registry, name, &wl_seat_interface,
                                 std::min(version, 7U)));
            wl_seat_add_listener(self->seat, &kSeatListener, self);
        }
        else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0)
        {
            self->wm = static_cast<xdg_wm_base*>(
                wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
            xdg_wm_base_add_listener(self->wm, &kWmListener, self);
        }
        else if (std::strcmp(interface,
                             zxdg_decoration_manager_v1_interface.name) == 0)
        {
            self->decoration_manager =
                static_cast<zxdg_decoration_manager_v1*>(
                    wl_registry_bind(
                        registry,
                        name,
                        &zxdg_decoration_manager_v1_interface,
                        1));
        }
    }

    static void registryRemove(void*, wl_registry*, std::uint32_t) {}

    static void wmPing(void*, xdg_wm_base* wm, std::uint32_t serial)
    {
        xdg_wm_base_pong(wm, serial);
    }

    static void surfaceConfigure(void* data,
                                 xdg_surface* surface,
                                 std::uint32_t serial)
    {
        auto* self = static_cast<Impl*>(data);
        xdg_surface_ack_configure(surface, serial);
        self->configured = true;
    }

    static void toplevelConfigure(void*,
                                  xdg_toplevel*,
                                  std::int32_t,
                                  std::int32_t,
                                  wl_array*)
    {
    }

    static void toplevelClose(void* data, xdg_toplevel*)
    {
        static_cast<Impl*>(data)->running = false;
    }

    static void seatCapabilities(void* data,
                                 wl_seat*,
                                 std::uint32_t capabilities)
    {
        auto* self = static_cast<Impl*>(data);
        if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0U &&
            self->keyboard == nullptr)
        {
            self->keyboard = wl_seat_get_keyboard(self->seat);
            wl_keyboard_add_listener(self->keyboard, &kKeyboardListener,
                                     self);
        }
        else if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) == 0U &&
                 self->keyboard != nullptr)
        {
            wl_keyboard_destroy(self->keyboard);
            self->keyboard = nullptr;
        }
    }

    static void seatName(void*, wl_seat*, const char*) {}

    static void keyboardKeymap(void* data,
                               wl_keyboard*,
                               std::uint32_t format,
                               std::int32_t fd,
                               std::uint32_t size)
    {
        if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
        {
            close(fd);
            return;
        }
        static_cast<Impl*>(data)->updateKeymap(fd, size);
    }

    static void keyboardEnter(void*,
                              wl_keyboard*,
                              std::uint32_t,
                              wl_surface*,
                              wl_array*)
    {
    }

    static void keyboardLeave(void*, wl_keyboard*, std::uint32_t, wl_surface*)
    {
    }

    static void keyboardKey(void* data,
                            wl_keyboard*,
                            std::uint32_t,
                            std::uint32_t,
                            std::uint32_t key,
                            std::uint32_t state)
    {
        static_cast<Impl*>(data)->handleKey(key, state);
    }

    static void keyboardModifiers(void* data,
                                  wl_keyboard*,
                                  std::uint32_t,
                                  std::uint32_t depressed,
                                  std::uint32_t latched,
                                  std::uint32_t locked,
                                  std::uint32_t group)
    {
        auto* self = static_cast<Impl*>(data);
        if (self->xkb_state_ != nullptr)
        {
            xkb_state_update_mask(self->xkb_state_, depressed, latched,
                                  locked, 0, 0, group);
        }
    }

    static void keyboardRepeat(void*, wl_keyboard*, std::int32_t, std::int32_t)
    {
    }

    static void bufferRelease(void* data, wl_buffer*)
    {
        static_cast<Buffer*>(data)->busy = false;
    }

    static const wl_registry_listener kRegistryListener;
    static const xdg_wm_base_listener kWmListener;
    static const xdg_surface_listener kSurfaceListener;
    static const xdg_toplevel_listener kToplevelListener;
    static const wl_seat_listener kSeatListener;
    static const wl_keyboard_listener kKeyboardListener;
    static const wl_buffer_listener kBufferListener;

    wl_display* display{};
    wl_registry* registry{};
    wl_compositor* compositor{};
    wl_shm* shm{};
    wl_seat* seat{};
    wl_keyboard* keyboard{};
    wl_surface* surface{};
    xdg_wm_base* wm{};
    xdg_surface* xdg_surface_obj{};
    xdg_toplevel* toplevel{};
    zxdg_decoration_manager_v1* decoration_manager{};
    zxdg_toplevel_decoration_v1* decoration{};
    xkb_context* xkb_context_{};
    xkb_keymap* xkb_keymap_{};
    xkb_state* xkb_state_{};
    Buffer buffers[2]{};
    bool configured{false};
    bool running{true};
    std::vector<app::InputEvent> pending_inputs{};
    zwp_text_input_manager_v3* text_manager = nullptr;
    std::unique_ptr<WaylandTextInput> text_input;
    app::TextInputState text_state;
};

const wl_registry_listener WaylandPresenter::Impl::kRegistryListener{
    WaylandPresenter::Impl::registryGlobal,
    WaylandPresenter::Impl::registryRemove,
};

const xdg_wm_base_listener WaylandPresenter::Impl::kWmListener{
    WaylandPresenter::Impl::wmPing,
};

const xdg_surface_listener WaylandPresenter::Impl::kSurfaceListener{
    WaylandPresenter::Impl::surfaceConfigure,
};

const xdg_toplevel_listener WaylandPresenter::Impl::kToplevelListener{
    WaylandPresenter::Impl::toplevelConfigure,
    WaylandPresenter::Impl::toplevelClose,
};

const wl_seat_listener WaylandPresenter::Impl::kSeatListener{
    WaylandPresenter::Impl::seatCapabilities,
    WaylandPresenter::Impl::seatName,
};

const wl_keyboard_listener WaylandPresenter::Impl::kKeyboardListener{
    WaylandPresenter::Impl::keyboardKeymap,
    WaylandPresenter::Impl::keyboardEnter,
    WaylandPresenter::Impl::keyboardLeave,
    WaylandPresenter::Impl::keyboardKey,
    WaylandPresenter::Impl::keyboardModifiers,
    WaylandPresenter::Impl::keyboardRepeat,
};

const wl_buffer_listener WaylandPresenter::Impl::kBufferListener{
    WaylandPresenter::Impl::bufferRelease,
};

WaylandPresenter::WaylandPresenter() : impl_(std::make_unique<Impl>()) {}

WaylandPresenter::~WaylandPresenter() = default;
void WaylandPresenter::setTextInputState(const app::TextInputState& state)
{
    impl_->text_state = state;
    if (impl_->text_input) impl_->text_input->update(state);
}

bool WaylandPresenter::pump()
{
    while (wl_display_prepare_read(impl_->display) != 0)
    {
        wl_display_dispatch_pending(impl_->display);
    }
    wl_display_flush(impl_->display);

    pollfd pfd{};
    pfd.fd = wl_display_get_fd(impl_->display);
    pfd.events = POLLIN;
    const int rc = poll(&pfd, 1, 0);
    if (rc > 0 && (pfd.revents & POLLIN) != 0)
    {
        wl_display_read_events(impl_->display);
        wl_display_dispatch_pending(impl_->display);
    }
    else
    {
        wl_display_cancel_read(impl_->display);
        wl_display_dispatch_pending(impl_->display);
    }

    return impl_->running && wl_display_get_error(impl_->display) == 0;
}

std::vector<app::InputEvent> WaylandPresenter::drainInput()
{
    auto drained = std::move(impl_->pending_inputs);
    impl_->pending_inputs.clear();
    return drained;
}

void WaylandPresenter::present(const core::Canvas& canvas)
{
    if (!impl_->configured)
    {
        return;
    }

    auto* buffer = impl_->nextBuffer();
    if (buffer == nullptr)
    {
        return;
    }

    const auto width = std::min(canvas.width(), kWidth);
    const auto height = std::min(canvas.height(), kHeight);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            buffer->pixels[(y * kWidth) + x] = packPixel(canvas.pixel(x, y));
        }
    }

    wl_surface_attach(impl_->surface, buffer->buffer, 0, 0);
    wl_surface_damage_buffer(impl_->surface, 0, 0, kWidth, kHeight);
    wl_surface_commit(impl_->surface);
    buffer->busy = true;
    wl_display_flush(impl_->display);
}

} // namespace trailmate::cardputer_zero::platform::wayland
