#include "platform/simulator/sdl_simulator.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "app/input_event.h"
#include "core/bitmap_font.h"
#include "core/canvas.h"
#include "core/display_profile.h"

namespace trailmate::cardputer_zero::platform::simulator {
namespace {

using core::rgba;

constexpr int kDeviceWidth = 800;
constexpr int kDeviceHeight = 520;
constexpr int kScreenWidth = core::kDisplayWidth;
constexpr int kScreenHeight = core::kDisplayHeight;
constexpr int kScreenFrameRectX = 180;
constexpr int kScreenFrameRectY = 26;
constexpr int kScreenFrameRectWidth = 374;
constexpr int kScreenFrameRectHeight = 202;
constexpr int kScreenLipInsetX = 9;
constexpr int kScreenLipInsetY = 11;
constexpr int kScreenLipInsetBottom = 9;
constexpr int kScreenLipWidth = kScreenFrameRectWidth - (kScreenLipInsetX * 2);
constexpr int kScreenLipHeight = kScreenFrameRectHeight - kScreenLipInsetY - kScreenLipInsetBottom;
constexpr int kScreenOpeningInset = 6;
constexpr int kScreenOpeningWidth = kScreenWidth + (kScreenOpeningInset * 2);
constexpr int kScreenOpeningHeight = kScreenHeight + (kScreenOpeningInset * 2);
constexpr int kScreenOpeningX = kScreenFrameRectX + kScreenLipInsetX + ((kScreenLipWidth - kScreenOpeningWidth) / 2);
constexpr int kScreenOpeningY = kScreenFrameRectY + kScreenLipInsetY + ((kScreenLipHeight - kScreenOpeningHeight) / 2);
constexpr int kScreenX = kScreenOpeningX + kScreenOpeningInset;
constexpr int kScreenY = kScreenOpeningY + kScreenOpeningInset;
constexpr int kRightPanelX = 592;
constexpr int kRightPanelY = 24;
constexpr int kRightPanelWidth = 166;
constexpr int kRightPanelHeight = 226;
constexpr int kPowerSwitchWidth = 58;
constexpr int kPowerSwitchHeight = 34;
constexpr int kPowerSwitchX = kRightPanelX + ((kRightPanelWidth - kPowerSwitchWidth) / 2);
constexpr int kPowerSwitchY = 54;
constexpr int kPanelButtonWidth = 62;
constexpr int kPanelButtonHeight = 22;
constexpr int kPanelButtonGap = 8;
constexpr int kPanelButtonsTotalWidth = (kPanelButtonWidth * 2) + kPanelButtonGap;
constexpr int kHomeButtonX = kRightPanelX + ((kRightPanelWidth - kPanelButtonsTotalWidth) / 2);
constexpr int kNextButtonX = kHomeButtonX + kPanelButtonWidth + kPanelButtonGap;
constexpr int kPanelButtonsY = 185;
constexpr int kKeyboardDeckX = 18;
constexpr int kKeyboardDeckY = 240;
constexpr int kKeyboardDeckWidth = 740;
constexpr int kKeyboardDeckHeight = 242;
constexpr int kKeyboardDeckSeparatorInset = 18;
constexpr int kKeyboardDeckSeparatorX = kKeyboardDeckX + kKeyboardDeckSeparatorInset;
constexpr int kKeyboardDeckSeparatorWidth = kKeyboardDeckWidth - (kKeyboardDeckSeparatorInset * 2);
constexpr int kKeyUnitWidth = 54;
constexpr int kKeyGap = 8;
constexpr int kKeyAreaHeight = 42;
constexpr int kKeyboardRowStartX = 36;
constexpr int kKeyboardRow1Y = 254;
constexpr int kKeyboardRowGapY = 54;
constexpr auto kPressFlash = std::chrono::milliseconds(140);

constexpr auto kBackdrop = rgba(6, 34, 58);
constexpr auto kShellShadow = rgba(80, 88, 96);
constexpr auto kShell = rgba(223, 225, 227);
constexpr auto kShellBorder = rgba(188, 191, 194);
constexpr auto kInset = rgba(210, 212, 214);
constexpr auto kScreenFrame = rgba(18, 20, 24);
constexpr auto kScreenLip = rgba(32, 35, 40);
constexpr auto kLabelDark = rgba(40, 43, 47);
constexpr auto kKeyBody = rgba(26, 29, 33);
constexpr auto kKeyPressed = rgba(81, 101, 122);
constexpr auto kAccentPink = rgba(220, 91, 142);
constexpr auto kAccentBlue = rgba(74, 155, 240);
constexpr auto kAccentOrange = rgba(243, 150, 63);
constexpr auto kAccentYellow = rgba(245, 217, 84);
constexpr auto kStickerWhite = rgba(248, 248, 248);
constexpr auto kPowerRed = rgba(221, 69, 63);
constexpr auto kPowerRedPressed = rgba(245, 96, 88);
constexpr auto kPanelDark = rgba(70, 74, 79);

enum class ButtonStyle {
    Keyboard,
    Panel,
    Switch,
};

enum class LatchMode {
    None,
    OneShot,
    Sticky,
};

enum class LegendSymbol {
    None,
    ArrowLeft,
    ArrowRight,
    ArrowUp,
    ArrowDown,
    BackArrow,
    SpaceBar,
};

struct Rect {
    int x{};
    int y{};
    int width{};
    int height{};

    [[nodiscard]] bool contains(int px, int py) const noexcept
    {
        return px >= x && py >= y && px < (x + width) && py < (y + height);
    }
};

struct KeyboardLegend {
    std::string text{};
    core::Color color{rgba(40, 43, 47)};
    LegendSymbol symbol{LegendSymbol::None};
    bool badge{false};
    core::Color badge_fill{rgba(0, 0, 0, 0)};
};

struct VirtualButton {
    Rect bounds{};
    app::InputEvent event{};
    KeyboardLegend primary_legend{};
    KeyboardLegend secondary_legend{};
    std::vector<std::string> highlight_aliases{};
    ButtonStyle style{ButtonStyle::Keyboard};
    std::chrono::steady_clock::time_point pressed_until{};
    LatchMode latch_mode{LatchMode::None};
};

[[nodiscard]] std::string upperCopy(std::string_view value)
{
    std::string result{};
    result.reserve(value.size());

    for (const char ch : value) {
        result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }

    return result;
}

void requireSdl(bool condition, std::string_view step)
{
    if (!condition) {
        throw std::runtime_error(std::string(step) + ": " + SDL_GetError());
    }
}

template <typename T>
T* requireSdl(T* pointer, std::string_view step)
{
    if (!pointer) {
        throw std::runtime_error(std::string(step) + ": " + SDL_GetError());
    }

    return pointer;
}

} // namespace

struct SdlSimulator::Impl {
    SDL_Window* window{};
    SDL_Renderer* renderer{};
    SDL_Texture* texture{};
    bool running{true};
    int scale{1};
    int window_width{};
    int window_height{};
    core::Canvas device_canvas{kDeviceWidth, kDeviceHeight};
    std::vector<std::uint32_t> staging = std::vector<std::uint32_t>(static_cast<std::size_t>(kDeviceWidth * kDeviceHeight));
    std::vector<app::InputEvent> input_queue{};
    std::vector<VirtualButton> buttons{};
};

[[nodiscard]] bool isModifierKey(app::InputKey key) noexcept
{
    switch (key) {
    case app::InputKey::Fn:
    case app::InputKey::Ctrl:
    case app::InputKey::Alt:
    case app::InputKey::Shift:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool isModifierButton(const VirtualButton& button) noexcept
{
    return isModifierKey(button.event.key);
}

[[nodiscard]] KeyboardLegend textLegend(std::string text, core::Color color = kLabelDark)
{
    return KeyboardLegend{std::move(text), color, LegendSymbol::None, false, rgba(0, 0, 0, 0)};
}

[[nodiscard]] KeyboardLegend badgeLegend(std::string text, core::Color text_color, core::Color badge_fill)
{
    return KeyboardLegend{std::move(text), text_color, LegendSymbol::None, true, badge_fill};
}

[[nodiscard]] KeyboardLegend symbolLegend(LegendSymbol symbol, core::Color color = kLabelDark)
{
    return KeyboardLegend{{}, color, symbol, false, rgba(0, 0, 0, 0)};
}

void addKeyboardButton(
    std::vector<VirtualButton>& buttons,
    int x,
    int y,
    KeyboardLegend primary_legend,
    app::InputEvent event,
    KeyboardLegend secondary_legend = {},
    std::initializer_list<std::string_view> aliases = {})
{
    VirtualButton button{};
    button.bounds = Rect{x, y, kKeyUnitWidth, kKeyAreaHeight};
    button.primary_legend = std::move(primary_legend);
    button.secondary_legend = std::move(secondary_legend);
    button.style = ButtonStyle::Keyboard;
    button.event = std::move(event);
    if (!button.event.label.empty()) {
        button.highlight_aliases.push_back(button.event.label);
    }
    for (const auto alias : aliases) {
        button.highlight_aliases.emplace_back(alias);
    }
    buttons.push_back(std::move(button));
}

void addPanelButton(
    std::vector<VirtualButton>& buttons,
    Rect bounds,
    std::string primary,
    app::InputEvent event,
    ButtonStyle style = ButtonStyle::Panel,
    std::initializer_list<std::string_view> aliases = {})
{
    VirtualButton button{};
    button.bounds = bounds;
    button.primary_legend = textLegend(std::move(primary), rgba(247, 247, 247));
    button.style = style;
    button.event = std::move(event);
    for (const auto alias : aliases) {
        button.highlight_aliases.emplace_back(alias);
    }
    buttons.push_back(std::move(button));
}

void buildKeyboardButtons(std::vector<VirtualButton>& buttons)
{
    const auto key_x = [](int column) noexcept {
        return kKeyboardRowStartX + (column * (kKeyUnitWidth + kKeyGap));
    };

    addKeyboardButton(buttons, key_x(0), kKeyboardRow1Y, textLegend("1"), app::makeCharacterInput('1', "1"), textLegend("!", kAccentBlue), {"!"});
    addKeyboardButton(buttons, key_x(1), kKeyboardRow1Y, textLegend("2"), app::makeCharacterInput('2', "2"), textLegend("@", kAccentBlue), {"@"});
    addKeyboardButton(buttons, key_x(2), kKeyboardRow1Y, textLegend("3"), app::makeCharacterInput('3', "3"), textLegend("#", kAccentBlue), {"#"});
    addKeyboardButton(buttons, key_x(3), kKeyboardRow1Y, textLegend("4"), app::makeCharacterInput('4', "4"), textLegend("$", kAccentBlue), {"$"});
    addKeyboardButton(buttons, key_x(4), kKeyboardRow1Y, textLegend("5"), app::makeCharacterInput('5', "5"), textLegend("%", kAccentBlue), {"%"});
    addKeyboardButton(buttons, key_x(5), kKeyboardRow1Y, textLegend("6"), app::makeCharacterInput('6', "6"), textLegend("^", kAccentBlue), {"^"});
    addKeyboardButton(buttons, key_x(6), kKeyboardRow1Y, textLegend("7"), app::makeCharacterInput('7', "7"), textLegend("&", kAccentBlue), {"&"});
    addKeyboardButton(buttons, key_x(7), kKeyboardRow1Y, textLegend("8"), app::makeCharacterInput('8', "8"), textLegend("*", kAccentBlue), {"*"});
    addKeyboardButton(buttons, key_x(8), kKeyboardRow1Y, textLegend("9"), app::makeCharacterInput('9', "9"), textLegend("(", kAccentBlue), {"("});
    addKeyboardButton(buttons, key_x(9), kKeyboardRow1Y, textLegend("0"), app::makeCharacterInput('0', "0"), textLegend(")", kAccentBlue), {")"});
    addKeyboardButton(
        buttons,
        key_x(10),
        kKeyboardRow1Y,
        symbolLegend(LegendSymbol::BackArrow),
        app::InputEvent{app::InputKey::Backspace, "DEL", '\0'},
        textLegend("del", kAccentOrange),
        {"BACKSPACE"});

    const int row2Y = kKeyboardRow1Y + kKeyboardRowGapY;
    addKeyboardButton(
        buttons,
        key_x(0),
        row2Y,
        textLegend("tab"),
        app::InputEvent{app::InputKey::Tab, "TAB", '\0'},
        textLegend("alt", kAccentOrange),
        {"ALT"});
    addKeyboardButton(buttons, key_x(1), row2Y, textLegend("Q"), app::makeCharacterInput('q', "Q"), textLegend("~", kAccentBlue), {"~"});
    addKeyboardButton(buttons, key_x(2), row2Y, textLegend("W"), app::makeCharacterInput('w', "W"), textLegend("`", kAccentBlue), {"`"});
    addKeyboardButton(buttons, key_x(3), row2Y, textLegend("E"), app::makeCharacterInput('e', "E"), textLegend("+", kAccentBlue), {"+"});
    addKeyboardButton(buttons, key_x(4), row2Y, textLegend("R"), app::makeCharacterInput('r', "R"), textLegend("-", kAccentBlue));
    addKeyboardButton(buttons, key_x(5), row2Y, textLegend("T"), app::makeCharacterInput('t', "T"), textLegend("/", kAccentBlue), {"/"});
    addKeyboardButton(buttons, key_x(6), row2Y, textLegend("Y"), app::makeCharacterInput('y', "Y"), textLegend("\\", kAccentBlue), {"\\"});
    addKeyboardButton(buttons, key_x(7), row2Y, textLegend("U"), app::makeCharacterInput('u', "U"), textLegend("{", kAccentBlue), {"{"});
    addKeyboardButton(buttons, key_x(8), row2Y, textLegend("I"), app::makeCharacterInput('i', "I"), textLegend("}", kAccentBlue), {"}"});
    addKeyboardButton(buttons, key_x(9), row2Y, textLegend("O"), app::makeCharacterInput('o', "O"), textLegend("[", kAccentBlue), {"["});
    addKeyboardButton(buttons, key_x(10), row2Y, textLegend("P"), app::makeCharacterInput('p', "P"), textLegend("]", kAccentBlue), {"]"});

    const int row3Y = row2Y + kKeyboardRowGapY;
    addKeyboardButton(
        buttons,
        key_x(0),
        row3Y,
        badgeLegend("Aa", rgba(255, 255, 255), kAccentBlue),
        app::InputEvent{app::InputKey::Shift, "AA", '\0'},
        {},
        {"SHIFT"});
    addKeyboardButton(buttons, key_x(1), row3Y, textLegend("A"), app::makeCharacterInput('a', "A"));
    addKeyboardButton(buttons, key_x(2), row3Y, textLegend("S"), app::makeCharacterInput('s', "S"));
    addKeyboardButton(
        buttons,
        key_x(3),
        row3Y,
        textLegend("D"),
        app::makeCharacterInput('d', "D"),
        symbolLegend(LegendSymbol::ArrowUp, kAccentOrange),
        {"UP"});
    addKeyboardButton(buttons, key_x(4), row3Y, textLegend("F"), app::makeCharacterInput('f', "F"), textLegend("|", kAccentBlue), {"|"});
    addKeyboardButton(buttons, key_x(5), row3Y, textLegend("G"), app::makeCharacterInput('g', "G"), textLegend("=", kAccentBlue), {"="});
    addKeyboardButton(buttons, key_x(6), row3Y, textLegend("H"), app::makeCharacterInput('h', "H"), textLegend(":", kAccentBlue), {":"});
    addKeyboardButton(buttons, key_x(7), row3Y, textLegend("J"), app::makeCharacterInput('j', "J"), textLegend(";", kAccentBlue), {";"});
    addKeyboardButton(buttons, key_x(8), row3Y, textLegend("K"), app::makeCharacterInput('k', "K"), textLegend("_", kAccentBlue), {"_"});
    addKeyboardButton(buttons, key_x(9), row3Y, textLegend("L"), app::makeCharacterInput('l', "L"), textLegend("?", kAccentBlue), {"?"});
    addKeyboardButton(
        buttons,
        key_x(10),
        row3Y,
        symbolLegend(LegendSymbol::BackArrow),
        app::InputEvent{app::InputKey::Enter, "OK", '\0'},
        textLegend("ok"),
        {"ENTER"});

    const int row4Y = row3Y + kKeyboardRowGapY;
    addKeyboardButton(
        buttons,
        key_x(0),
        row4Y,
        badgeLegend("fn", rgba(255, 255, 255), kAccentOrange),
        app::InputEvent{app::InputKey::Fn, "FN", '\0'});
    addKeyboardButton(
        buttons,
        key_x(1),
        row4Y,
        badgeLegend("ctrl", kLabelDark, kAccentYellow),
        app::InputEvent{app::InputKey::Ctrl, "CTRL", '\0'});
    addKeyboardButton(
        buttons,
        key_x(2),
        row4Y,
        textLegend("Z"),
        app::makeCharacterInput('z', "Z"),
        symbolLegend(LegendSymbol::ArrowLeft, kAccentOrange),
        {"LEFT"});
    addKeyboardButton(
        buttons,
        key_x(3),
        row4Y,
        textLegend("X"),
        app::makeCharacterInput('x', "X"),
        symbolLegend(LegendSymbol::ArrowDown, kAccentOrange),
        {"DOWN"});
    addKeyboardButton(
        buttons,
        key_x(4),
        row4Y,
        textLegend("C"),
        app::makeCharacterInput('c', "C"),
        symbolLegend(LegendSymbol::ArrowRight, kAccentOrange),
        {"RIGHT"});
    addKeyboardButton(buttons, key_x(5), row4Y, textLegend("V"), app::makeCharacterInput('v', "V"), textLegend("<", kAccentBlue), {"<"});
    addKeyboardButton(buttons, key_x(6), row4Y, textLegend("B"), app::makeCharacterInput('b', "B"), textLegend(">", kAccentBlue), {">"});
    addKeyboardButton(buttons, key_x(7), row4Y, textLegend("N"), app::makeCharacterInput('n', "N"), textLegend("'", kAccentBlue), {"'"});
    addKeyboardButton(buttons, key_x(8), row4Y, textLegend("M"), app::makeCharacterInput('m', "M"), textLegend("\"", kAccentBlue), {"\""});
    addKeyboardButton(buttons, key_x(9), row4Y, textLegend(","), app::makeCharacterInput(',', ","), textLegend(".", kAccentBlue), {"."});
    addKeyboardButton(
        buttons,
        key_x(10),
        row4Y,
        symbolLegend(LegendSymbol::SpaceBar),
        app::makeCharacterInput(' ', "SPACE"));
}

void buildPanelButtons(std::vector<VirtualButton>& buttons)
{
    addPanelButton(
        buttons,
        Rect{kPowerSwitchX, kPowerSwitchY, kPowerSwitchWidth, kPowerSwitchHeight},
        "ON",
        app::InputEvent{app::InputKey::Power, "POWER", '\0'},
        ButtonStyle::Switch,
        {"POWER"});
    addPanelButton(
        buttons,
        Rect{kHomeButtonX, kPanelButtonsY, kPanelButtonWidth, kPanelButtonHeight},
        "HOME",
        app::InputEvent{app::InputKey::Home, "HOME", '\0'},
        ButtonStyle::Panel,
        {"HOME"});
    addPanelButton(
        buttons,
        Rect{kNextButtonX, kPanelButtonsY, kPanelButtonWidth, kPanelButtonHeight},
        "NEXT",
        app::InputEvent{app::InputKey::Next, "NEXT", '\0'},
        ButtonStyle::Panel,
        {"NEXT"});
}

[[nodiscard]] std::vector<VirtualButton> buildButtons()
{
    std::vector<VirtualButton> buttons{};
    buttons.reserve(49);
    buildKeyboardButtons(buttons);
    buildPanelButtons(buttons);
    return buttons;
}

void markPressed(VirtualButton& button)
{
    button.pressed_until = std::chrono::steady_clock::now() + kPressFlash;
}

void clearLatch(VirtualButton& button) noexcept
{
    button.latch_mode = LatchMode::None;
}

void setOneShotLatch(VirtualButton& button) noexcept
{
    button.latch_mode = LatchMode::OneShot;
}

void toggleStickyLatch(VirtualButton& button) noexcept
{
    button.latch_mode = button.latch_mode == LatchMode::Sticky ? LatchMode::None : LatchMode::Sticky;
}

[[nodiscard]] bool isLatched(const VirtualButton& button) noexcept
{
    return button.latch_mode != LatchMode::None;
}

[[nodiscard]] bool isPressed(const VirtualButton& button) noexcept
{
    return isLatched(button) || std::chrono::steady_clock::now() < button.pressed_until;
}

[[nodiscard]] int centeredTextX(std::string_view text, int scale, int left, int width) noexcept
{
    return left + ((width - core::bitmap_font::measureText(text, scale)) / 2);
}

void drawLabelBlock(core::Canvas& canvas, int x, int y, std::string_view text, core::Color color, int scale = 1)
{
    core::bitmap_font::drawText(canvas, text, x, y, color, scale);
}

[[nodiscard]] bool hasLegend(const KeyboardLegend& legend) noexcept
{
    return legend.symbol != LegendSymbol::None || !legend.text.empty();
}

[[nodiscard]] int symbolWidth(LegendSymbol symbol) noexcept
{
    switch (symbol) {
    case LegendSymbol::ArrowLeft:
    case LegendSymbol::ArrowRight:
    case LegendSymbol::ArrowUp:
    case LegendSymbol::ArrowDown:
        return 11;
    case LegendSymbol::BackArrow:
        return 14;
    case LegendSymbol::SpaceBar:
        return 16;
    case LegendSymbol::None:
        return 0;
    }

    return 0;
}

[[nodiscard]] int legendWidth(const KeyboardLegend& legend) noexcept
{
    int width = 0;
    if (!legend.text.empty()) {
        width = core::bitmap_font::measureText(legend.text, 1);
    } else if (legend.symbol != LegendSymbol::None) {
        width = symbolWidth(legend.symbol);
    }

    if (legend.badge) {
        width += 8;
    }

    return width;
}

void drawLegendSymbol(core::Canvas& canvas, int x, int y, LegendSymbol symbol, core::Color color)
{
    switch (symbol) {
    case LegendSymbol::ArrowLeft:
        canvas.fillRect(x + 3, y + 3, 8, 1, color);
        canvas.fillRect(x + 2, y + 2, 1, 3, color);
        canvas.fillRect(x + 1, y + 1, 1, 5, color);
        canvas.fillRect(x + 0, y + 0, 1, 7, color);
        break;
    case LegendSymbol::ArrowRight:
        canvas.fillRect(x, y + 3, 8, 1, color);
        canvas.fillRect(x + 8, y + 2, 1, 3, color);
        canvas.fillRect(x + 9, y + 1, 1, 5, color);
        canvas.fillRect(x + 10, y + 0, 1, 7, color);
        break;
    case LegendSymbol::ArrowUp:
        canvas.fillRect(x + 5, y, 1, 7, color);
        canvas.fillRect(x + 4, y + 1, 3, 1, color);
        canvas.fillRect(x + 3, y + 2, 5, 1, color);
        canvas.fillRect(x + 2, y + 3, 7, 1, color);
        break;
    case LegendSymbol::ArrowDown:
        canvas.fillRect(x + 5, y, 1, 7, color);
        canvas.fillRect(x + 2, y + 3, 7, 1, color);
        canvas.fillRect(x + 3, y + 4, 5, 1, color);
        canvas.fillRect(x + 4, y + 5, 3, 1, color);
        break;
    case LegendSymbol::BackArrow:
        canvas.fillRect(x + 5, y + 3, 8, 1, color);
        canvas.fillRect(x + 5, y + 4, 8, 1, color);
        canvas.fillRect(x + 4, y + 2, 1, 3, color);
        canvas.fillRect(x + 3, y + 1, 1, 5, color);
        canvas.fillRect(x + 2, y + 0, 1, 7, color);
        break;
    case LegendSymbol::SpaceBar:
        canvas.fillRect(x + 2, y + 6, 12, 1, color);
        canvas.fillRect(x + 2, y + 2, 1, 5, color);
        canvas.fillRect(x + 13, y + 2, 1, 5, color);
        break;
    case LegendSymbol::None:
        break;
    }
}

void drawKeyboardLegend(core::Canvas& canvas, int x, int y, const KeyboardLegend& legend)
{
    if (!hasLegend(legend)) {
        return;
    }

    int content_x = x;
    if (legend.badge) {
        const int badge_width = legendWidth(legend);
        canvas.fillRect(x, y - 2, badge_width, 15, legend.badge_fill);
        content_x += 4;
    }

    if (legend.symbol != LegendSymbol::None) {
        drawLegendSymbol(canvas, content_x, y + 1, legend.symbol, legend.color);
        return;
    }

    drawLabelBlock(canvas, content_x, y, legend.text, legend.color, 1);
}

void drawKeyboardButton(core::Canvas& canvas, const VirtualButton& button)
{
    const bool pressed = isPressed(button);
    const auto pill_color = pressed ? kKeyPressed : kKeyBody;
    const int pill_y = button.bounds.y + 18;
    const int pill_height = button.bounds.height - 18;

    const bool has_secondary = hasLegend(button.secondary_legend);
    const int primary_width = legendWidth(button.primary_legend);
    const int secondary_width = has_secondary ? legendWidth(button.secondary_legend) : 0;
    const int legend_gap = has_secondary ? 2 : 0;
    const int legend_total_width = primary_width + secondary_width + legend_gap;
    int legend_x = button.bounds.x + ((button.bounds.width - legend_total_width) / 2);

    drawKeyboardLegend(canvas, legend_x, button.bounds.y, button.primary_legend);
    if (has_secondary) {
        legend_x += primary_width + legend_gap;
        drawKeyboardLegend(canvas, legend_x, button.bounds.y, button.secondary_legend);
    }

    canvas.fillRoundedRect(button.bounds.x + 2, pill_y + 2, button.bounds.width - 4, pill_height - 2, 10, rgba(9, 11, 14));
    canvas.fillRoundedRect(button.bounds.x, pill_y, button.bounds.width - 2, pill_height - 4, 10, pill_color);
}

void drawPanelButton(core::Canvas& canvas, const VirtualButton& button)
{
    const bool pressed = isPressed(button);
    auto fill = kPanelDark;
    auto text = rgba(247, 247, 247);

    if (button.style == ButtonStyle::Switch) {
        fill = pressed ? kPowerRedPressed : kPowerRed;
        text = rgba(255, 255, 255);
    } else if (pressed) {
        fill = rgba(90, 98, 110);
    }

    canvas.fillRoundedRect(button.bounds.x, button.bounds.y, button.bounds.width, button.bounds.height, 10, fill);
    drawLabelBlock(
        canvas,
        centeredTextX(button.primary_legend.text, 1, button.bounds.x, button.bounds.width),
        button.bounds.y + ((button.bounds.height - core::bitmap_font::lineHeight(1)) / 2) + 1,
        button.primary_legend.text,
        text,
        1);
}

void drawButtons(core::Canvas& canvas, const std::vector<VirtualButton>& buttons)
{
    for (const auto& button : buttons) {
        if (button.style == ButtonStyle::Keyboard) {
            drawKeyboardButton(canvas, button);
        } else {
            drawPanelButton(canvas, button);
        }
    }
}

void drawLeftPanel(core::Canvas& canvas)
{
    canvas.fillRoundedRect(24, 24, 144, 226, 20, kInset);
    drawLabelBlock(canvas, 38, 40, "TM", rgba(198, 201, 204), 6);

    canvas.fillRoundedRect(24, 110, 132, 88, 10, kStickerWhite);
    drawLabelBlock(canvas, 34, 120, "CARDPUTER", rgba(74, 74, 76), 1);
    drawLabelBlock(canvas, 37, 142, "ZERO", rgba(34, 34, 36), 3);
    drawLabelBlock(canvas, 34, 174, "PI OS LINUX", rgba(78, 78, 82), 1);

    canvas.fillRoundedRect(40, 202, 38, 16, 8, rgba(38, 40, 43));
    canvas.fillRoundedRect(106, 202, 16, 16, 8, rgba(202, 204, 206));
}

void drawRightPanel(core::Canvas& canvas)
{
    canvas.fillRoundedRect(kRightPanelX, kRightPanelY, kRightPanelWidth, kRightPanelHeight, 18, kInset);
    drawLabelBlock(canvas, centeredTextX("POWER", 2, kRightPanelX, kRightPanelWidth), 100, "POWER", kLabelDark, 2);
    drawLabelBlock(canvas, centeredTextX("SWITCH", 1, kRightPanelX, kRightPanelWidth), 120, "SWITCH", kLabelDark, 1);
    drawLabelBlock(canvas, centeredTextX("USB-C", 1, kRightPanelX, kRightPanelWidth), 130, "USB-C", kAccentOrange, 1);
    drawLabelBlock(canvas, centeredTextX("LINUX", 1, kRightPanelX, kRightPanelWidth), 144, "LINUX", kAccentBlue, 1);
    drawLabelBlock(canvas, centeredTextX("CARDPUTER ZERO", 1, kRightPanelX, kRightPanelWidth), 159, "CARDPUTER ZERO", kAccentPink, 1);
    drawLabelBlock(canvas, centeredTextX("HOME", 1, kHomeButtonX, kPanelButtonWidth), 214, "HOME", kLabelDark, 1);
    drawLabelBlock(canvas, centeredTextX("NEXT", 1, kNextButtonX, kPanelButtonWidth), 214, "NEXT", kLabelDark, 1);
}

void drawScreenFrame(core::Canvas& canvas)
{
    canvas.fillRoundedRect(kScreenFrameRectX, kScreenFrameRectY, kScreenFrameRectWidth, kScreenFrameRectHeight, 18, kScreenFrame);
    canvas.fillRoundedRect(
        kScreenFrameRectX + kScreenLipInsetX,
        kScreenFrameRectY + kScreenLipInsetY,
        kScreenLipWidth,
        kScreenLipHeight,
        12,
        kScreenLip);
    canvas.fillRect(kScreenOpeningX, kScreenOpeningY, kScreenOpeningWidth, kScreenOpeningHeight, rgba(0, 0, 0));
}

void drawKeyboardDeck(core::Canvas& canvas)
{
    canvas.fillRoundedRect(kKeyboardDeckX, kKeyboardDeckY, kKeyboardDeckWidth, kKeyboardDeckHeight, 18, rgba(230, 232, 234));
    canvas.fillRect(kKeyboardDeckSeparatorX, 306, kKeyboardDeckSeparatorWidth, 2, rgba(205, 207, 209));
    canvas.fillRect(kKeyboardDeckSeparatorX, 360, kKeyboardDeckSeparatorWidth, 2, rgba(205, 207, 209));
    canvas.fillRect(kKeyboardDeckSeparatorX, 414, kKeyboardDeckSeparatorWidth, 2, rgba(205, 207, 209));
}

void drawShell(core::Canvas& canvas, const core::Canvas& screen_canvas, const std::vector<VirtualButton>& buttons)
{
    canvas.clear(kBackdrop);
    canvas.fillRoundedRect(12, 16, kDeviceWidth - 20, kDeviceHeight - 28, 34, kShellShadow);
    canvas.fillRoundedRect(0, 0, kDeviceWidth - 20, kDeviceHeight - 28, 34, kShell);
    canvas.fillRoundedRect(6, 6, kDeviceWidth - 32, 14, 7, rgba(255, 255, 255));
    canvas.fillRoundedRect(8, 8, kDeviceWidth - 36, kDeviceHeight - 44, 30, kShell);
    canvas.strokeRect(12, 12, kDeviceWidth - 44, kDeviceHeight - 52, kShellBorder, 1);

    drawLeftPanel(canvas);
    drawRightPanel(canvas);
    drawScreenFrame(canvas);
    drawKeyboardDeck(canvas);
    drawButtons(canvas, buttons);
    canvas.blit(screen_canvas, kScreenX, kScreenY);
}

void highlightByLabel(std::vector<VirtualButton>& buttons, std::string_view label)
{
    const auto normalized = upperCopy(label);
    for (auto& button : buttons) {
        for (const auto& alias : button.highlight_aliases) {
            if (upperCopy(alias) == normalized) {
                markPressed(button);
                return;
            }
        }
    }
}

[[nodiscard]] int toDeviceCoordinate(float coordinate, int scale) noexcept
{
    if (scale <= 1) {
        return static_cast<int>(coordinate);
    }

    return static_cast<int>(coordinate / static_cast<float>(scale));
}

[[nodiscard]] std::uint32_t packPixel(core::Color color) noexcept
{
    return (static_cast<std::uint32_t>(color.a) << 24U)
        | (static_cast<std::uint32_t>(color.b) << 16U)
        | (static_cast<std::uint32_t>(color.g) << 8U)
        | static_cast<std::uint32_t>(color.r);
}

[[nodiscard]] std::string characterLabel(char ch)
{
    if (ch == ' ') {
        return "SPACE";
    }

    std::string label{};
    label.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    return label;
}

[[nodiscard]] std::optional<app::InputEvent> eventFromLegend(const KeyboardLegend& legend)
{
    switch (legend.symbol) {
    case LegendSymbol::ArrowLeft:
        return app::InputEvent{app::InputKey::Left, "LEFT", '\0'};
    case LegendSymbol::ArrowRight:
        return app::InputEvent{app::InputKey::Right, "RIGHT", '\0'};
    case LegendSymbol::ArrowUp:
        return app::InputEvent{app::InputKey::Up, "UP", '\0'};
    case LegendSymbol::ArrowDown:
        return app::InputEvent{app::InputKey::Down, "DOWN", '\0'};
    case LegendSymbol::None:
    case LegendSymbol::BackArrow:
    case LegendSymbol::SpaceBar:
        break;
    }

    if (legend.text.empty()) {
        return std::nullopt;
    }

    const auto normalized = upperCopy(legend.text);
    if (normalized == "ALT") {
        return app::InputEvent{app::InputKey::Alt, "ALT", '\0'};
    }
    if (normalized == "DEL") {
        return app::InputEvent{app::InputKey::Backspace, "DEL", '\0'};
    }
    if (normalized == "OK") {
        return app::InputEvent{app::InputKey::Enter, "OK", '\0'};
    }
    if (legend.text.size() == 1U) {
        return app::makeCharacterInput(legend.text.front(), characterLabel(legend.text.front()));
    }

    return std::nullopt;
}

[[nodiscard]] app::InputEvent resolveButtonEvent(const VirtualButton& button, const std::vector<VirtualButton>& buttons)
{
    bool shift_active = false;
    bool fn_active = false;

    for (const auto& candidate : buttons) {
        if (!isLatched(candidate)) {
            continue;
        }

        if (candidate.event.key == app::InputKey::Shift) {
            shift_active = true;
        } else if (candidate.event.key == app::InputKey::Fn) {
            fn_active = true;
        }
    }

    if (fn_active) {
        if (const auto alternate = eventFromLegend(button.secondary_legend)) {
            return *alternate;
        }
    }

    if (shift_active && button.event.key == app::InputKey::Character && button.event.text != '\0') {
        const unsigned char value = static_cast<unsigned char>(button.event.text);
        if (std::isalpha(value)) {
            const char shifted = static_cast<char>(std::toupper(value));
            return app::makeCharacterInput(shifted, characterLabel(shifted));
        }

        if (const auto alternate = eventFromLegend(button.secondary_legend)) {
            return *alternate;
        }
    }

    return button.event;
}

void consumeOneShotModifiers(std::vector<VirtualButton>& buttons) noexcept
{
    for (auto& button : buttons) {
        if (button.latch_mode == LatchMode::OneShot) {
            clearLatch(button);
        }
    }
}

void enqueueSpecial(
    std::vector<app::InputEvent>& queue,
    std::vector<VirtualButton>& buttons,
    app::InputKey key,
    std::string label)
{
    queue.push_back(app::InputEvent{key, label, '\0'});
    highlightByLabel(buttons, label);
}

void handleKeyboardEvent(
    std::vector<app::InputEvent>& queue,
    std::vector<VirtualButton>& buttons,
    const SDL_KeyboardEvent& event)
{
    if (event.repeat) {
        return;
    }

    switch (event.key) {
    case SDLK_BACKSPACE:
        enqueueSpecial(queue, buttons, app::InputKey::Backspace, "DEL");
        break;
    case SDLK_RETURN:
        enqueueSpecial(queue, buttons, app::InputKey::Enter, "OK");
        break;
    case SDLK_TAB:
        enqueueSpecial(queue, buttons, app::InputKey::Tab, "TAB");
        break;
    case SDLK_HOME:
        enqueueSpecial(queue, buttons, app::InputKey::Home, "HOME");
        break;
    case SDLK_END:
        enqueueSpecial(queue, buttons, app::InputKey::Next, "NEXT");
        break;
    case SDLK_ESCAPE:
        enqueueSpecial(queue, buttons, app::InputKey::Power, "POWER");
        break;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
        enqueueSpecial(queue, buttons, app::InputKey::Shift, "AA");
        break;
    case SDLK_LCTRL:
    case SDLK_RCTRL:
        enqueueSpecial(queue, buttons, app::InputKey::Ctrl, "CTRL");
        break;
    case SDLK_LALT:
    case SDLK_RALT:
        enqueueSpecial(queue, buttons, app::InputKey::Alt, "ALT");
        break;
    case SDLK_LEFT:
        enqueueSpecial(queue, buttons, app::InputKey::Left, "LEFT");
        break;
    case SDLK_RIGHT:
        enqueueSpecial(queue, buttons, app::InputKey::Right, "RIGHT");
        break;
    case SDLK_UP:
        enqueueSpecial(queue, buttons, app::InputKey::Up, "UP");
        break;
    case SDLK_DOWN:
        enqueueSpecial(queue, buttons, app::InputKey::Down, "DOWN");
        break;
    default:
        break;
    }
}

void handleTextInput(
    std::vector<app::InputEvent>& queue,
    std::vector<VirtualButton>& buttons,
    const SDL_TextInputEvent& event)
{
    if (!event.text) {
        return;
    }

    for (const char* current = event.text; *current != '\0'; ++current) {
        const unsigned char value = static_cast<unsigned char>(*current);
        if (value > 127U || !std::isprint(value)) {
            continue;
        }

        const char ch = static_cast<char>(value);
        std::string label{};
        if (ch == ' ') {
            label = "SPACE";
        } else {
            label.push_back(static_cast<char>(std::toupper(value)));
        }

        queue.push_back(app::makeCharacterInput(ch, label));
        highlightByLabel(buttons, label);
    }
}

void handleMouseClick(
    std::vector<app::InputEvent>& queue,
    std::vector<VirtualButton>& buttons,
    const SDL_MouseButtonEvent& event,
    int scale)
{
    if (event.button != SDL_BUTTON_LEFT && event.button != SDL_BUTTON_RIGHT) {
        return;
    }

    const int device_x = toDeviceCoordinate(event.x, scale);
    const int device_y = toDeviceCoordinate(event.y, scale);

    for (auto& button : buttons) {
        if (!button.bounds.contains(device_x, device_y)) {
            continue;
        }

        markPressed(button);

        if (isModifierButton(button)) {
            if (event.button == SDL_BUTTON_RIGHT) {
                toggleStickyLatch(button);
                if (button.latch_mode == LatchMode::Sticky) {
                    markPressed(button);
                }
            } else {
                if (button.latch_mode != LatchMode::Sticky) {
                    setOneShotLatch(button);
                }
            }
            break;
        }

        queue.push_back(resolveButtonEvent(button, buttons));
        consumeOneShotModifiers(buttons);
        break;
    }
}

SdlSimulator::SdlSimulator(int scale)
    : impl_(std::make_unique<Impl>())
{
    const int clamped_scale = std::max(scale, 1);
    impl_->scale = clamped_scale;
    impl_->window_width = kDeviceWidth * clamped_scale;
    impl_->window_height = kDeviceHeight * clamped_scale;
    impl_->buttons = buildButtons();

    requireSdl(SDL_Init(SDL_INIT_VIDEO), "SDL_Init");

    impl_->window = requireSdl(
        SDL_CreateWindow("Trail Mate Cardputer Zero Front Simulator", impl_->window_width, impl_->window_height, 0),
        "SDL_CreateWindow");
    impl_->renderer = requireSdl(SDL_CreateRenderer(impl_->window, nullptr), "SDL_CreateRenderer");
    impl_->texture = requireSdl(
        SDL_CreateTexture(impl_->renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, kDeviceWidth, kDeviceHeight),
        "SDL_CreateTexture");

    requireSdl(SDL_SetTextureScaleMode(impl_->texture, SDL_SCALEMODE_NEAREST), "SDL_SetTextureScaleMode");
    requireSdl(SDL_StartTextInput(impl_->window), "SDL_StartTextInput");
}

SdlSimulator::~SdlSimulator()
{
    if (impl_->texture) {
        SDL_DestroyTexture(impl_->texture);
    }
    if (impl_->renderer) {
        SDL_DestroyRenderer(impl_->renderer);
    }
    if (impl_->window) {
        SDL_DestroyWindow(impl_->window);
    }

    SDL_Quit();
}

bool SdlSimulator::pump()
{
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            impl_->running = false;
            continue;
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            handleMouseClick(impl_->input_queue, impl_->buttons, event.button, impl_->scale);
            continue;
        }
        if (event.type == SDL_EVENT_KEY_DOWN) {
            handleKeyboardEvent(impl_->input_queue, impl_->buttons, event.key);
            continue;
        }
        if (event.type == SDL_EVENT_TEXT_INPUT) {
            handleTextInput(impl_->input_queue, impl_->buttons, event.text);
        }
    }

    return impl_->running;
}

std::vector<app::InputEvent> SdlSimulator::drainInput()
{
    auto drained = std::move(impl_->input_queue);
    impl_->input_queue.clear();
    return drained;
}

void SdlSimulator::present(const core::Canvas& canvas)
{
    drawShell(impl_->device_canvas, canvas, impl_->buttons);

    const auto& pixels = impl_->device_canvas.pixels();
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        impl_->staging[i] = packPixel(pixels[i]);
    }

    requireSdl(
        SDL_UpdateTexture(
            impl_->texture,
            nullptr,
            impl_->staging.data(),
            impl_->device_canvas.width() * static_cast<int>(sizeof(std::uint32_t))),
        "SDL_UpdateTexture");
    requireSdl(SDL_SetRenderDrawColor(impl_->renderer, 0, 0, 0, 255), "SDL_SetRenderDrawColor");
    requireSdl(SDL_RenderClear(impl_->renderer), "SDL_RenderClear");

    const SDL_FRect destination{0.0F, 0.0F, static_cast<float>(impl_->window_width), static_cast<float>(impl_->window_height)};
    requireSdl(SDL_RenderTexture(impl_->renderer, impl_->texture, nullptr, &destination), "SDL_RenderTexture");
    requireSdl(SDL_RenderPresent(impl_->renderer), "SDL_RenderPresent");
}

} // namespace trailmate::cardputer_zero::platform::simulator
