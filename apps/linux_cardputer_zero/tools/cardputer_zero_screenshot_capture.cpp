#include "ui/shell_ui_runner.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "app/input_event.h"
#include "core/canvas.h"
#include "lvgl.h"

#define LODEPNG_NO_COMPILE_CPP
#include "src/libs/lodepng/lodepng.h"

namespace
{

using trailmate::cardputer_zero::app::InputEvent;
using trailmate::cardputer_zero::app::InputKey;
using trailmate::cardputer_zero::core::Canvas;
using trailmate::cardputer_zero::linux_ui::CanvasLvglHost;
using trailmate::cardputer_zero::linux_ui::ShellSession;

constexpr auto kFrameDelay = std::chrono::milliseconds(16);
constexpr auto kBootSplashGate = std::chrono::milliseconds(3200);

struct CaptureTarget
{
    const char* id = nullptr;
    const char* filename = nullptr;
    int menu_steps = 0;
};

constexpr CaptureTarget kTargets[] = {
    {"dashboard", "dashboard.png", -1},
    {"chat", "chat.png", 0},
    {"contacts", "contacts.png", 1},
    {"map", "map.png", 2},
    {"sky_plot", "sky-plot.png", 3},
    {"team", "team.png", 4},
    {"tracker", "tracker.png", 5},
    {"calculator", "calculator.png", 6},
    {"geocaching", "geocaching.png", 7},
    {"energy_sweep", "protocol-probe.png", 8},
    {"walkie", "walkie.png", 9},
    {"extensions", "extensions.png", 10},
    {"settings", "settings.png", 11},
};

void tick_for(ShellSession& shell,
              CanvasLvglHost& host,
              std::chrono::milliseconds duration)
{
    (void)shell;
    const auto end = std::chrono::steady_clock::now() + duration;
    do
    {
        host.tick();
        std::this_thread::sleep_for(kFrameDelay);
    } while (std::chrono::steady_clock::now() < end);

    host.tick();
}

void render_now(CanvasLvglHost& host)
{
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(nullptr);
    host.tick();
}

void wait_until_capture_ready(ShellSession& shell, CanvasLvglHost& host)
{
    const auto started = std::chrono::steady_clock::now();
    do
    {
        host.tick();
        std::this_thread::sleep_for(kFrameDelay);
    } while (!shell.ready() ||
             std::chrono::steady_clock::now() - started < kBootSplashGate);

    host.tick();
}

void press(ShellSession& shell, InputKey key)
{
    shell.enqueueInputs(std::vector<InputEvent>{{key, {}, '\0'}});
}

void press_char(ShellSession& shell, char ch)
{
    shell.enqueueInputs(std::vector<InputEvent>{
        {InputKey::Character, std::string(1, ch), ch}});
}

void press_action(ShellSession& shell,
                  CanvasLvglHost& host,
                  const std::string& action)
{
    if (action == "f1")
    {
        press(shell, InputKey::F1);
    }
    else if (action == "left")
    {
        press(shell, InputKey::Left);
    }
    else if (action == "right")
    {
        press(shell, InputKey::Right);
    }
    else if (action == "up")
    {
        press(shell, InputKey::Up);
    }
    else if (action == "down")
    {
        press(shell, InputKey::Down);
    }
    else if (action == "plus")
    {
        press_char(shell, '+');
    }
    else if (action == "minus")
    {
        press_char(shell, '-');
    }
    else if (action == "pos" || action == "position" || action == "center" ||
             action == "p" || action == "c")
    {
        press_char(shell, 'p');
    }
    else if (action == "layer" || action == "l")
    {
        press_char(shell, 'l');
    }
    else if (action == "contour" || action == "cont" || action == "o")
    {
        press_char(shell, 'o');
    }
    else if (action == "route" || action == "r")
    {
        press_char(shell, 'r');
    }
    else if (action.size() == 1)
    {
        press_char(shell, action[0]);
    }
    else if (action == "enter")
    {
        press(shell, InputKey::Enter);
    }
    else if (action == "back")
    {
        press(shell, InputKey::Backspace);
    }
    else
    {
        throw std::runtime_error("unknown capture action: " + action);
    }
    tick_for(shell, host, std::chrono::milliseconds(220));
    render_now(host);
}

std::vector<std::string> split_actions(const std::string& text)
{
    std::vector<std::string> actions;
    std::string current;
    for (const char ch : text)
    {
        if (ch == ',')
        {
            if (!current.empty())
            {
                actions.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty())
    {
        actions.push_back(current);
    }
    return actions;
}

void save_png(const Canvas& canvas, const std::filesystem::path& path)
{
    std::filesystem::create_directories(path.parent_path());

    std::vector<unsigned char> rgba;
    rgba.reserve(static_cast<std::size_t>(canvas.width() * canvas.height() * 4));
    for (int y = 0; y < canvas.height(); ++y)
    {
        for (int x = 0; x < canvas.width(); ++x)
        {
            const auto& pixel = canvas.pixel(x, y);
            rgba.push_back(pixel.r);
            rgba.push_back(pixel.g);
            rgba.push_back(pixel.b);
            rgba.push_back(pixel.a);
        }
    }

    unsigned char* encoded = nullptr;
    std::size_t encoded_size = 0;
    const unsigned error = lodepng_encode32(
        &encoded,
        &encoded_size,
        rgba.data(),
        static_cast<unsigned>(canvas.width()),
        static_cast<unsigned>(canvas.height()));
    if (error != 0U)
    {
        throw std::runtime_error("failed to encode " + path.string() + ": " +
                                 lodepng_error_text(error));
    }

    std::ofstream out(path, std::ios::binary);
    if (!out)
    {
        free(encoded);
        throw std::runtime_error("failed to open " + path.string() +
                                 " for writing");
    }
    out.write(reinterpret_cast<const char*>(encoded),
              static_cast<std::streamsize>(encoded_size));
    free(encoded);
    if (!out)
    {
        throw std::runtime_error("failed to write " + path.string());
    }
}

void capture_current(const CanvasLvglHost& host,
                     const std::filesystem::path& output_dir,
                     const char* filename)
{
    const auto path = output_dir / filename;
    save_png(host.canvas(), path);
    std::cout << "captured " << path.string() << '\n';
}

void open_focused_app(ShellSession& shell, CanvasLvglHost& host)
{
    press(shell, InputKey::Enter);
    tick_for(shell, host, std::chrono::milliseconds(650));
    render_now(host);
}

void return_to_menu(ShellSession& shell, CanvasLvglHost& host)
{
    press(shell, InputKey::Backspace);
    tick_for(shell, host, std::chrono::milliseconds(650));
}

void focus_next_menu_item(ShellSession& shell, CanvasLvglHost& host)
{
    press(shell, InputKey::Right);
    tick_for(shell, host, std::chrono::milliseconds(180));
}

} // namespace

int main(int argc, char** argv)
{
    const std::filesystem::path output_dir =
        argc >= 2 ? std::filesystem::path(argv[1])
                  : std::filesystem::path("cardputerzero-screenshots");
    const std::string requested_id = argc >= 3 ? argv[2] : "";
    const auto actions = argc >= 4 ? split_actions(argv[3])
                                   : std::vector<std::string>{};

    try
    {
        for (const auto& target : kTargets)
        {
            if (!requested_id.empty() && requested_id != target.id)
            {
                continue;
            }

            ShellSession shell;
            CanvasLvglHost host{shell};

            if (!shell.begin())
            {
                std::cerr << "failed to start Cardputer Zero shell session\n";
                return 1;
            }

            wait_until_capture_ready(shell, host);
            if (target.menu_steps >= 0)
            {
                for (int step = 0; step < target.menu_steps; ++step)
                {
                    focus_next_menu_item(shell, host);
                }
                open_focused_app(shell, host);
            }
            for (const auto& action : actions)
            {
                press_action(shell, host, action);
            }
            capture_current(host, output_dir, target.filename);

            if (!requested_id.empty())
            {
                std::fflush(stdout);
                std::_Exit(0);
            }
        }

        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
