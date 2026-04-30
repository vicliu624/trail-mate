#include "app/demo_app.h"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "core/bitmap_font.h"
#include "core/display_profile.h"

namespace trailmate::cardputer_zero::app
{
namespace
{

using core::rgba;

constexpr auto kBackground = rgba(8, 12, 18);
constexpr auto kPanel = rgba(18, 27, 39);
constexpr auto kHeader = rgba(18, 71, 109);
constexpr auto kBorder = rgba(83, 192, 243);
constexpr auto kTitle = rgba(237, 247, 252);
constexpr auto kText = rgba(236, 242, 247);
constexpr auto kMuted = rgba(133, 153, 171);
constexpr auto kAccent = rgba(255, 177, 64);
constexpr auto kInputFill = rgba(7, 10, 15);
constexpr auto kInputBorder = rgba(79, 97, 115);
constexpr auto kGood = rgba(150, 220, 132);
constexpr auto kWarn = rgba(255, 214, 93);

int centeredX(std::string_view text, int scale) noexcept
{
    return (core::kDisplayWidth - core::bitmap_font::measureText(text, scale)) / 2;
}

std::string clippedTail(const std::string& value, std::size_t max_length)
{
    if (value.size() <= max_length)
    {
        return value;
    }

    return value.substr(value.size() - max_length);
}

} // namespace

DemoApp::DemoApp()
    : started_at_(std::chrono::steady_clock::now())
{
}

void DemoApp::handleInput(const InputEvent& event) noexcept
{
    if (!event.label.empty())
    {
        last_key_label_ = event.label;
    }

    switch (event.key)
    {
    case InputKey::Character:
        if (event.text != '\0')
        {
            if (typed_text_.size() >= 26)
            {
                typed_text_.erase(0, 1);
            }
            typed_text_.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(event.text))));
            status_text_ = "TEXT INPUT CAPTURED";
        }
        break;
    case InputKey::Backspace:
        if (!typed_text_.empty())
        {
            typed_text_.pop_back();
        }
        status_text_ = "DELETE LAST CHARACTER";
        break;
    case InputKey::Enter:
        status_text_ = "ENTER PRESSED";
        break;
    case InputKey::Tab:
        if (typed_text_.size() < 25)
        {
            typed_text_ += ' ';
        }
        status_text_ = "TAB INSERTED SPACE";
        break;
    case InputKey::Home:
        typed_text_.clear();
        status_text_ = "HOME CLEARED INPUT";
        break;
    case InputKey::Next:
        status_text_ = "NEXT REQUESTED";
        break;
    case InputKey::Power:
        status_text_ = "POWER SWITCH TOGGLED";
        break;
    case InputKey::Fn:
        status_text_ = "FN MODIFIER PRESSED";
        break;
    case InputKey::Ctrl:
        status_text_ = "CTRL MODIFIER PRESSED";
        break;
    case InputKey::Alt:
        status_text_ = "ALT MODIFIER PRESSED";
        break;
    case InputKey::Shift:
        status_text_ = "SHIFT MODIFIER PRESSED";
        break;
    case InputKey::Left:
        status_text_ = "LEFT NAVIGATION";
        break;
    case InputKey::Right:
        status_text_ = "RIGHT NAVIGATION";
        break;
    case InputKey::Up:
        status_text_ = "UP NAVIGATION";
        break;
    case InputKey::Down:
        status_text_ = "DOWN NAVIGATION";
        break;
    case InputKey::Unknown:
        status_text_ = "UNKNOWN INPUT";
        break;
    }
}

void DemoApp::render(core::Canvas& canvas) const noexcept
{
    canvas.clear(kBackground);
    canvas.fillRect(8, 8, core::kDisplayWidth - 16, core::kDisplayHeight - 16, kPanel);
    canvas.strokeRect(8, 8, core::kDisplayWidth - 16, core::kDisplayHeight - 16, kBorder, 2);
    canvas.fillRect(18, 18, core::kDisplayWidth - 36, 22, kHeader);

    core::bitmap_font::drawText(canvas, "TRAIL MATE", centeredX("TRAIL MATE", 2), 23, kTitle, 2);
    core::bitmap_font::drawText(canvas, "CARDPUTER ZERO LINUX BASELINE", centeredX("CARDPUTER ZERO LINUX BASELINE", 1), 52, kMuted, 1);

    canvas.fillRect(24, 66, core::kDisplayWidth - 48, 28, kInputFill);
    canvas.strokeRect(24, 66, core::kDisplayWidth - 48, 28, kInputBorder, 1);
    core::bitmap_font::drawText(canvas, "INPUT", 34, 72, kMuted, 1);
    core::bitmap_font::drawText(
        canvas,
        hasTypedText() ? visibleInput() : std::string("TYPE OR CLICK THE SHELL"),
        84,
        72,
        hasTypedText() ? kText : kMuted,
        1);

    canvas.fillRect(24, 100, core::kDisplayWidth - 48, 20, kInputFill);
    canvas.strokeRect(24, 100, core::kDisplayWidth - 48, 20, kInputBorder, 1);
    core::bitmap_font::drawText(canvas, "LAST", 34, 106, kMuted, 1);
    core::bitmap_font::drawText(canvas, visibleLastKey(), 84, 106, kGood, 1);

    canvas.fillRect(24, 126, core::kDisplayWidth - 48, 20, kInputFill);
    canvas.strokeRect(24, 126, core::kDisplayWidth - 48, 20, kInputBorder, 1);
    core::bitmap_font::drawText(canvas, "STATUS", 34, 132, kMuted, 1);
    core::bitmap_font::drawText(canvas, visibleStatus(), 84, 132, kText, 1);

    core::bitmap_font::drawText(canvas, "SIM  WIN  WSL  DOCKER  PI OS", 31, 152, kWarn, 1);

    if (blinkOn())
    {
        canvas.fillRect(265, 75, 12, 4, kAccent);
    }
}

bool DemoApp::blinkOn() const noexcept
{
    const auto elapsed = std::chrono::steady_clock::now() - started_at_;
    const auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 450;
    return (ticks % 2) == 0;
}

bool DemoApp::hasTypedText() const noexcept
{
    return !typed_text_.empty();
}

std::string DemoApp::visibleInput() const
{
    return clippedTail(typed_text_, 24);
}

std::string DemoApp::visibleStatus() const
{
    return clippedTail(status_text_, 24);
}

std::string DemoApp::visibleLastKey() const
{
    return clippedTail(last_key_label_, 10);
}

} // namespace trailmate::cardputer_zero::app
