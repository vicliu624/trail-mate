#include "platform/desktop/sdl_lvgl_host.h"

#include <SDL.h>
#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <stdexcept>
#include <vector>

#include "lvgl.h"

namespace platform::desktop
{
struct SdlLvglHost::Impl
{
    DeviceOptions options;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    lv_display_t* display = nullptr;
    lv_indev_t* pointer = nullptr;
    lv_indev_t* keyboard = nullptr;
    std::vector<std::uint16_t> pixels;
    std::deque<std::pair<std::uint32_t, lv_indev_state_t>> keys;
    std::map<uint32_t, bool> held_keys;
    KeyObserver observer = nullptr;
    void* observer_context = nullptr;
    TextEnabled text_enabled = nullptr;
    TextCommit text_commit = nullptr;
    TextComposition text_composition = nullptr;
    void* text_context = nullptr;
    lv_point_t point{};
    struct PointerSample
    {
        lv_point_t point;
        bool pressed;
        bool motion;
    };
    std::deque<PointerSample> pointer_samples;
    bool pressed = false;
    bool keyboard_focus = true;
    bool running = true;
    bool dirty = true;

    ~Impl()
    {
        cancelInput();
        if (keyboard) lv_indev_delete(keyboard);
        if (pointer) lv_indev_delete(pointer);
        if (display) lv_display_delete(display);
        if (texture) SDL_DestroyTexture(texture);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    }

    void cancelInput()
    {
        if (observer)
        {
            for (const auto& key : held_keys) observer(observer_context, key.first, false);
            observer(observer_context, 0, false);
        }
        held_keys.clear();
        keys.clear();
        pointer_samples.clear();
        pressed = false;
    }

    void samplePointer(bool motion = false)
    {
        if (motion && !pointer_samples.empty() && pointer_samples.back().motion && pointer_samples.back().pressed == pressed)
        {
            pointer_samples.back().point = point;
            return;
        }
        if (pointer_samples.size() >= 64)
        {
            // Cancel an overloaded gesture rather than leave the pointer held.
            pointer_samples.clear();
            pressed = false;
        }
        pointer_samples.push_back({point, pressed, motion});
    }

    void position(float x, float y)
    {
        float px = 0;
        float py = 0;
        SDL_RenderWindowToLogical(renderer, static_cast<int>(x), static_cast<int>(y), &px, &py);
        const int width = options.width;
        const int height = options.height;
        switch (options.rotation)
        {
        case 90:
            point = {static_cast<int32_t>(py), height - 1 - static_cast<int32_t>(px)};
            break;
        case 180:
            point = {width - 1 - static_cast<int32_t>(px), height - 1 - static_cast<int32_t>(py)};
            break;
        case 270:
            point = {width - 1 - static_cast<int32_t>(py), static_cast<int32_t>(px)};
            break;
        default:
            point = {static_cast<int32_t>(px), static_cast<int32_t>(py)};
            break;
        }
        point.x = std::clamp<int32_t>(point.x, 0, width - 1);
        point.y = std::clamp<int32_t>(point.y, 0, height - 1);
    }

    static void flush(lv_display_t* display, const lv_area_t*, uint8_t*)
    {
        auto* self = static_cast<Impl*>(lv_display_get_user_data(display));
        self->dirty = true;
        lv_display_flush_ready(display);
    }

    static void readPointer(lv_indev_t* indev, lv_indev_data_t* data)
    {
        auto* self = static_cast<Impl*>(lv_indev_get_user_data(indev));
        data->point = self->point;
        data->state = self->pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        if (!self->pointer_samples.empty())
        {
            const auto sample = self->pointer_samples.front();
            self->pointer_samples.pop_front();
            data->point = sample.point;
            data->state = sample.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
            data->continue_reading = !self->pointer_samples.empty();
        }
    }

    static void readKeyboard(lv_indev_t* indev, lv_indev_data_t* data)
    {
        auto* self = static_cast<Impl*>(lv_indev_get_user_data(indev));
        data->state = LV_INDEV_STATE_RELEASED;
        if (self->keys.empty()) return;
        data->key = self->keys.front().first;
        data->state = self->keys.front().second;
        self->keys.pop_front();
        data->continue_reading = !self->keys.empty();
    }
};

namespace
{
void require(bool ok, const char* operation)
{
    if (!ok) throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}

std::uint32_t keyCode(SDL_Keycode key)
{
    switch (key)
    {
    case SDLK_RETURN:
        return LV_KEY_ENTER;
    case SDLK_ESCAPE:
        return LV_KEY_ESC;
    case SDLK_BACKSPACE:
        return LV_KEY_BACKSPACE;
    case SDLK_TAB:
        return LV_KEY_NEXT;
    case SDLK_LEFT:
        return LV_KEY_LEFT;
    case SDLK_RIGHT:
        return LV_KEY_RIGHT;
    case SDLK_UP:
        return LV_KEY_UP;
    case SDLK_DOWN:
        return LV_KEY_DOWN;
    case SDLK_F1:
        return 0x110001U;
    default:
        return key >= 32 && key < 127 ? static_cast<std::uint32_t>(key) : 0U;
    }
}
} // namespace

SdlLvglHost::SdlLvglHost(const DeviceOptions& options) : impl_(std::make_unique<Impl>())
{
    validateDeviceOptions(options);
    auto& state = *impl_;
    state.options = options;
    require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0, "SDL_Init");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#if SDL_VERSION_ATLEAST(2, 0, 22)
    SDL_SetHint(SDL_HINT_IME_SUPPORT_EXTENDED_TEXT, "1");
#endif
    const bool quarter_turn = options.rotation == 90 || options.rotation == 270;
    const int physical_width = quarter_turn ? options.height : options.width;
    const int physical_height = quarter_turn ? options.width : options.height;
    state.window = SDL_CreateWindow(options.title.c_str(), SDL_WINDOWPOS_CENTERED,
                                    SDL_WINDOWPOS_CENTERED, physical_width, physical_height,
                                    options.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    require(state.window != nullptr, "SDL_CreateWindow");
    state.renderer = SDL_CreateRenderer(state.window, -1, SDL_RENDERER_ACCELERATED);
    if (!state.renderer) state.renderer = SDL_CreateRenderer(state.window, -1, SDL_RENDERER_SOFTWARE);
    require(state.renderer != nullptr, "SDL_CreateRenderer");
    require(SDL_RenderSetLogicalSize(state.renderer, physical_width, physical_height) == 0,
            "SDL_RenderSetLogicalSize");
    state.texture = SDL_CreateTexture(state.renderer, SDL_PIXELFORMAT_RGB565,
                                      SDL_TEXTUREACCESS_STREAMING, options.width, options.height);
    require(state.texture != nullptr, "SDL_CreateTexture");
    state.pixels.resize(static_cast<std::size_t>(options.width) * options.height);
    state.display = lv_display_create(options.width, options.height);
    if (!state.display) throw std::runtime_error("Cannot create LVGL display");
    lv_display_set_default(state.display);
    lv_display_set_user_data(state.display, &state);
    lv_display_set_color_format(state.display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(state.display, state.pixels.data(), nullptr,
                           static_cast<uint32_t>(state.pixels.size() * sizeof(std::uint16_t)),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(state.display, Impl::flush);
    state.pointer = lv_indev_create();
    if (options.hardware_keyboard) state.keyboard = lv_indev_create();
    if (!state.pointer || (options.hardware_keyboard && !state.keyboard))
        throw std::runtime_error("Cannot create LVGL input");
    lv_indev_set_type(state.pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(state.pointer, state.display);
    lv_indev_set_user_data(state.pointer, &state);
    lv_indev_set_read_cb(state.pointer, Impl::readPointer);
    if (state.keyboard)
    {
        lv_indev_set_type(state.keyboard, LV_INDEV_TYPE_KEYPAD);
        lv_indev_set_display(state.keyboard, state.display);
        lv_indev_set_user_data(state.keyboard, &state);
        lv_indev_set_read_cb(state.keyboard, Impl::readKeyboard);
    }
}

SdlLvglHost::~SdlLvglHost() = default;

void SdlLvglHost::setKeyObserver(KeyObserver observer, void* context)
{
    impl_->cancelInput();
    impl_->observer = observer;
    impl_->observer_context = context;
}

void SdlLvglHost::setRotation(unsigned rotation)
{
    if (rotation > 270 || rotation % 90) throw std::invalid_argument("Invalid display rotation");
    auto& state = *impl_;
    if (state.options.rotation == static_cast<int>(rotation)) return;
    state.options.rotation = static_cast<int>(rotation);
    lv_indev_reset(state.pointer, nullptr);
    const bool quarter_turn = rotation == 90 || rotation == 270;
    require(SDL_RenderSetLogicalSize(state.renderer, quarter_turn ? state.options.height : state.options.width,
                                     quarter_turn ? state.options.width : state.options.height) == 0,
            "SDL_RenderSetLogicalSize");
    state.pressed = false;
    state.pointer_samples.clear();
    state.dirty = true;
}

void SdlLvglHost::setTextObserver(TextEnabled enabled, TextCommit commit, void* context, TextComposition composition)
{
    impl_->text_enabled = enabled;
    impl_->text_commit = commit;
    impl_->text_context = context;
    impl_->text_composition = composition;
}

void SdlLvglHost::setInputObserver(InputObserver observer, void* context)
{
    input_observer_ = observer;
    input_context_ = context;
}

bool SdlLvglHost::pump()
{
    auto& state = *impl_;
    bool editing = state.keyboard_focus && state.text_enabled && state.text_commit && state.text_enabled(state.text_context);
    if (editing && !SDL_IsTextInputActive()) SDL_StartTextInput();
    else if (!editing && SDL_IsTextInputActive())
    {
        SDL_StopTextInput();
        if (state.text_composition) state.text_composition(state.text_context, "", 0, 0);
    }
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_QUIT ||
            (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE) ||
            (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F4 &&
             (event.key.keysym.mod & KMOD_ALT))) state.running = false;
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_EXPOSED)
            state.dirty = true;
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
        {
            if (input_observer_) input_observer_(input_context_, {InputActivity::Kind::Cancel});
            state.keyboard_focus = false;
            editing = false;
            SDL_StopTextInput();
            if (state.text_composition) state.text_composition(state.text_context, "", 0, 0);
            state.cancelInput();
        }
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
        {
            state.keyboard_focus = true;
            editing = state.text_enabled && state.text_commit && state.text_enabled(state.text_context);
            if (editing) SDL_StartTextInput();
        }
        if (input_observer_)
        {
            InputActivity activity{InputActivity::Kind::Cancel};
            bool observed = true;
            if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)
            {
                activity.kind = InputActivity::Kind::Key;
                activity.key = keyCode(event.key.keysym.sym);
                activity.pressed = event.type == SDL_KEYDOWN;
            }
            else if (event.type == SDL_MOUSEMOTION)
            {
                state.position(event.motion.x, event.motion.y);
                activity.kind = InputActivity::Kind::Pointer;
                activity.pressed = (event.motion.state & SDL_BUTTON_LMASK) != 0;
                activity.motion = true;
            }
            else if ((event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) && event.button.button == SDL_BUTTON_LEFT)
            {
                state.position(event.button.x, event.button.y);
                activity.kind = InputActivity::Kind::Pointer;
                activity.pressed = event.type == SDL_MOUSEBUTTONDOWN;
            }
            else if (event.type == SDL_FINGERDOWN || event.type == SDL_FINGERMOTION || event.type == SDL_FINGERUP)
            {
                int width = 0, height = 0;
                SDL_GetWindowSize(state.window, &width, &height);
                state.position(event.tfinger.x * width, event.tfinger.y * height);
                activity.kind = InputActivity::Kind::Pointer;
                activity.pressed = event.type != SDL_FINGERUP;
                activity.motion = event.type == SDL_FINGERMOTION;
            }
            else if (event.type == SDL_TEXTINPUT || event.type == SDL_TEXTEDITING
#if SDL_VERSION_ATLEAST(2, 0, 22)
                     || event.type == SDL_TEXTEDITING_EXT
#endif
            )
                activity.kind = InputActivity::Kind::Text;
            else observed = false;
            activity.x = state.point.x;
            activity.y = state.point.y;
            if (observed && input_observer_(input_context_, activity))
            {
                state.cancelInput();
#if SDL_VERSION_ATLEAST(2, 0, 22)
                if (event.type == SDL_TEXTEDITING_EXT) SDL_free(event.editExt.text);
#endif
                continue;
            }
        }
        if (event.type == SDL_MOUSEMOTION)
        {
            state.position(event.motion.x, event.motion.y);
            if (state.pressed) state.samplePointer(true);
        }
        if ((event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) &&
            event.button.button == SDL_BUTTON_LEFT)
        {
            state.position(event.button.x, event.button.y);
            state.pressed = event.type == SDL_MOUSEBUTTONDOWN;
            state.samplePointer();
        }
        if (event.type == SDL_FINGERDOWN || event.type == SDL_FINGERMOTION || event.type == SDL_FINGERUP)
        {
            int width = 0;
            int height = 0;
            SDL_GetWindowSize(state.window, &width, &height);
            state.position(event.tfinger.x * width, event.tfinger.y * height);
            state.pressed = event.type != SDL_FINGERUP;
            state.samplePointer(event.type == SDL_FINGERMOTION);
        }
        if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)
        {
            const auto key = keyCode(event.key.keysym.sym);
            if (!key) continue;
            const bool down = event.type == SDL_KEYDOWN;
            auto held = state.held_keys.find(key);
            bool consumed = held != state.held_keys.end() && held->second;
            if (down && held == state.held_keys.end())
            {
                consumed = state.observer && state.observer(state.observer_context, key, true);
                state.held_keys.emplace(key, consumed);
            }
            else if (!down)
            {
                if (state.observer) consumed = state.observer(state.observer_context, key, false) || consumed;
                state.held_keys.erase(key);
            }
            // Printable text comes from SDL_TEXTINPUT, with layout/modifiers/IME
            // already applied. Raw keycodes here would duplicate or alter it.
            if (!consumed && !(editing && key >= 32 && key < 127) && state.keys.size() < 128)
                state.keys.emplace_back(key, down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED);
        }
        if (event.type == SDL_TEXTINPUT && editing)
        {
            if (state.text_composition) state.text_composition(state.text_context, "", 0, 0);
            state.text_commit(state.text_context, event.text.text);
        }
        if (event.type == SDL_TEXTEDITING && editing && state.text_composition)
            state.text_composition(state.text_context, event.edit.text, event.edit.start, event.edit.length);
#if SDL_VERSION_ATLEAST(2, 0, 22)
        if (event.type == SDL_TEXTEDITING_EXT)
        {
            if (editing && state.text_composition && event.editExt.text)
                state.text_composition(state.text_context, event.editExt.text, event.editExt.start, event.editExt.length);
            SDL_free(event.editExt.text);
        }
#endif
    }
    if (!state.running) state.cancelInput();
    return state.running;
}

void SdlLvglHost::present()
{
    auto& state = *impl_;
    if (!state.dirty) return;
    require(SDL_UpdateTexture(state.texture, nullptr, state.pixels.data(), state.options.width * 2) == 0,
            "SDL_UpdateTexture");
    const bool quarter_turn = state.options.rotation == 90 || state.options.rotation == 270;
    const int width = quarter_turn ? state.options.height : state.options.width;
    const int height = quarter_turn ? state.options.width : state.options.height;
    SDL_Rect destination{(width - state.options.width) / 2, (height - state.options.height) / 2,
                         state.options.width, state.options.height};
    SDL_SetRenderDrawColor(state.renderer, 0, 0, 0, 255);
    SDL_RenderClear(state.renderer);
    require(SDL_RenderCopyEx(state.renderer, state.texture, nullptr, &destination,
                             state.options.rotation, nullptr, SDL_FLIP_NONE) == 0,
            "SDL_RenderCopyEx");
    SDL_RenderPresent(state.renderer);
    state.dirty = false;
}

void SdlLvglHost::saveScreenshot(const std::string& path)
{
    auto& state = *impl_;
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(state.pixels.data(), state.options.width,
                                                              state.options.height, 16, state.options.width * 2, SDL_PIXELFORMAT_RGB565);
    require(surface != nullptr, "SDL_CreateRGBSurfaceWithFormatFrom");
    const int result = SDL_SaveBMP(surface, path.c_str());
    SDL_FreeSurface(surface);
    require(result == 0, "SDL_SaveBMP");
}
} // namespace platform::desktop
