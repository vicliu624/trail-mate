#include "platform/desktop/sdl_window_presenter.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "app/input_event.h"
#include "core/canvas.h"

namespace trailmate::uconsole::desktop
{
namespace
{

namespace app = cardputer_zero::app;
namespace core = cardputer_zero::core;

void requireSdl(bool condition, std::string_view step)
{
    if (!condition)
    {
        throw std::runtime_error(std::string(step) + ": " + SDL_GetError());
    }
}

template <typename T>
T* requireSdl(T* pointer, std::string_view step)
{
    if (!pointer)
    {
        throw std::runtime_error(std::string(step) + ": " + SDL_GetError());
    }
    return pointer;
}

[[nodiscard]] std::uint32_t packPixel(core::Color color) noexcept
{
    return (static_cast<std::uint32_t>(color.a) << 24U) |
           (static_cast<std::uint32_t>(color.b) << 16U) |
           (static_cast<std::uint32_t>(color.g) << 8U) |
           static_cast<std::uint32_t>(color.r);
}

void enqueueSpecial(std::vector<app::InputEvent>& queue,
                    app::InputKey key,
                    std::string label)
{
    queue.push_back(app::InputEvent{key, std::move(label), '\0'});
}

void handleKeyboardEvent(std::vector<app::InputEvent>& queue,
                         const SDL_KeyboardEvent& event)
{
    if (event.repeat)
    {
        return;
    }

    switch (event.key)
    {
    case SDLK_BACKSPACE:
        enqueueSpecial(queue, app::InputKey::Backspace, "DEL");
        break;
    case SDLK_RETURN:
        enqueueSpecial(queue, app::InputKey::Enter, "OK");
        break;
    case SDLK_TAB:
        enqueueSpecial(queue, app::InputKey::Tab, "TAB");
        break;
    case SDLK_HOME:
        enqueueSpecial(queue, app::InputKey::Home, "HOME");
        break;
    case SDLK_END:
        enqueueSpecial(queue, app::InputKey::Next, "NEXT");
        break;
    case SDLK_ESCAPE:
        enqueueSpecial(queue, app::InputKey::Power, "POWER");
        break;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
        enqueueSpecial(queue, app::InputKey::Shift, "SHIFT");
        break;
    case SDLK_LCTRL:
    case SDLK_RCTRL:
        enqueueSpecial(queue, app::InputKey::Ctrl, "CTRL");
        break;
    case SDLK_LALT:
    case SDLK_RALT:
        enqueueSpecial(queue, app::InputKey::Alt, "ALT");
        break;
    case SDLK_LEFT:
        enqueueSpecial(queue, app::InputKey::Left, "LEFT");
        break;
    case SDLK_RIGHT:
        enqueueSpecial(queue, app::InputKey::Right, "RIGHT");
        break;
    case SDLK_UP:
        enqueueSpecial(queue, app::InputKey::Up, "UP");
        break;
    case SDLK_DOWN:
        enqueueSpecial(queue, app::InputKey::Down, "DOWN");
        break;
    default:
        break;
    }
}

void handleTextInput(std::vector<app::InputEvent>& queue,
                     const SDL_TextInputEvent& event)
{
    if (event.text == nullptr)
    {
        return;
    }

    for (const char* current = event.text; *current != '\0'; ++current)
    {
        const unsigned char value = static_cast<unsigned char>(*current);
        if (value > 127U || !std::isprint(value))
        {
            continue;
        }

        const char ch = static_cast<char>(value);
        std::string label{};
        if (ch == ' ')
        {
            label = "SPACE";
        }
        else
        {
            label.push_back(static_cast<char>(std::toupper(value)));
        }
        queue.push_back(app::makeCharacterInput(ch, std::move(label)));
    }
}

} // namespace

struct SdlWindowPresenter::Impl
{
    SdlWindowOptions options{};
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    bool running = true;
    int texture_width = 0;
    int texture_height = 0;
    int window_width = 0;
    int window_height = 0;
    std::vector<std::uint32_t> staging{};
    std::vector<app::InputEvent> input_queue{};
};

SdlWindowPresenter::SdlWindowPresenter(SdlWindowOptions options)
    : impl_(std::make_unique<Impl>())
{
    impl_->options = std::move(options);
    impl_->options.width = std::max(1, impl_->options.width);
    impl_->options.height = std::max(1, impl_->options.height);
    impl_->options.scale = std::max(1, impl_->options.scale);
    impl_->window_width = impl_->options.width * impl_->options.scale;
    impl_->window_height = impl_->options.height * impl_->options.scale;

    requireSdl(SDL_Init(SDL_INIT_VIDEO), "SDL_Init");

    const SDL_WindowFlags flags =
        impl_->options.fullscreen ? SDL_WINDOW_FULLSCREEN : 0;
    impl_->window =
        requireSdl(SDL_CreateWindow(impl_->options.title.c_str(),
                                    impl_->window_width,
                                    impl_->window_height,
                                    flags),
                   "SDL_CreateWindow");
    impl_->renderer =
        requireSdl(SDL_CreateRenderer(impl_->window, nullptr),
                   "SDL_CreateRenderer");
    requireSdl(SDL_StartTextInput(impl_->window), "SDL_StartTextInput");
}

SdlWindowPresenter::~SdlWindowPresenter()
{
    if (impl_->texture != nullptr)
    {
        SDL_DestroyTexture(impl_->texture);
    }
    if (impl_->renderer != nullptr)
    {
        SDL_DestroyRenderer(impl_->renderer);
    }
    if (impl_->window != nullptr)
    {
        SDL_DestroyWindow(impl_->window);
    }
    SDL_Quit();
}

bool SdlWindowPresenter::pump()
{
    SDL_Event event{};
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_EVENT_QUIT)
        {
            impl_->running = false;
            continue;
        }
        if (event.type == SDL_EVENT_KEY_DOWN)
        {
            handleKeyboardEvent(impl_->input_queue, event.key);
            continue;
        }
        if (event.type == SDL_EVENT_TEXT_INPUT)
        {
            handleTextInput(impl_->input_queue, event.text);
            continue;
        }
        if (event.type == SDL_EVENT_WINDOW_RESIZED)
        {
            impl_->window_width = event.window.data1;
            impl_->window_height = event.window.data2;
        }
    }

    return impl_->running;
}

std::vector<cardputer_zero::app::InputEvent> SdlWindowPresenter::drainInput()
{
    auto drained = std::move(impl_->input_queue);
    impl_->input_queue.clear();
    return drained;
}

void SdlWindowPresenter::present(const core::Canvas& canvas)
{
    if (canvas.width() != impl_->texture_width ||
        canvas.height() != impl_->texture_height || impl_->texture == nullptr)
    {
        if (impl_->texture != nullptr)
        {
            SDL_DestroyTexture(impl_->texture);
        }
        impl_->texture_width = canvas.width();
        impl_->texture_height = canvas.height();
        impl_->staging.resize(
            static_cast<std::size_t>(impl_->texture_width) *
            static_cast<std::size_t>(impl_->texture_height));
        impl_->texture = requireSdl(
            SDL_CreateTexture(impl_->renderer,
                              SDL_PIXELFORMAT_RGBA32,
                              SDL_TEXTUREACCESS_STREAMING,
                              impl_->texture_width,
                              impl_->texture_height),
            "SDL_CreateTexture");
        requireSdl(SDL_SetTextureScaleMode(impl_->texture,
                                           SDL_SCALEMODE_LINEAR),
                   "SDL_SetTextureScaleMode");
    }

    const auto& pixels = canvas.pixels();
    for (std::size_t index = 0; index < pixels.size(); ++index)
    {
        impl_->staging[index] = packPixel(pixels[index]);
    }

    requireSdl(SDL_UpdateTexture(
                   impl_->texture,
                   nullptr,
                   impl_->staging.data(),
                   impl_->texture_width *
                       static_cast<int>(sizeof(std::uint32_t))),
               "SDL_UpdateTexture");
    requireSdl(SDL_SetRenderDrawColor(impl_->renderer, 0, 0, 0, 255),
               "SDL_SetRenderDrawColor");
    requireSdl(SDL_RenderClear(impl_->renderer), "SDL_RenderClear");

    if (!impl_->options.fullscreen)
    {
        SDL_GetWindowSize(impl_->window, &impl_->window_width,
                          &impl_->window_height);
    }
    const float scale_x = static_cast<float>(impl_->window_width) /
                          static_cast<float>(impl_->texture_width);
    const float scale_y = static_cast<float>(impl_->window_height) /
                          static_cast<float>(impl_->texture_height);
    const float scale = std::min(scale_x, scale_y);
    const float render_width = static_cast<float>(impl_->texture_width) * scale;
    const float render_height =
        static_cast<float>(impl_->texture_height) * scale;
    const SDL_FRect destination{
        (static_cast<float>(impl_->window_width) - render_width) / 2.0F,
        (static_cast<float>(impl_->window_height) - render_height) / 2.0F,
        render_width,
        render_height,
    };

    requireSdl(SDL_RenderTexture(impl_->renderer, impl_->texture, nullptr,
                                 &destination),
               "SDL_RenderTexture");
    requireSdl(SDL_RenderPresent(impl_->renderer), "SDL_RenderPresent");
}

} // namespace trailmate::uconsole::desktop
