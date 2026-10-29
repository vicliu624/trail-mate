#include "ui/screens/calculator/calculator_page_runtime.h"

#include "ui/app_runtime.h"
#include "ui/screens/calculator/calculator_engine.h"
#include "ui/screens/calculator/calculator_page_layout.h"
#include "ui/ui_theme.h"
#include "ui/widgets/top_bar.h"

#include <array>
#include <cstdint>
#include <cstdio>

#if !defined(LV_FONT_MONTSERRAT_10) || !LV_FONT_MONTSERRAT_10
#define lv_font_montserrat_10 lv_font_montserrat_12
#endif
#if !defined(LV_FONT_MONTSERRAT_12) || !LV_FONT_MONTSERRAT_12
#define lv_font_montserrat_12 lv_font_montserrat_14
#endif
#if !defined(LV_FONT_MONTSERRAT_16) || !LV_FONT_MONTSERRAT_16
#define lv_font_montserrat_16 lv_font_montserrat_14
#endif
#if !defined(LV_FONT_MONTSERRAT_20) || !LV_FONT_MONTSERRAT_20
#define lv_font_montserrat_20 lv_font_montserrat_16
#endif

namespace
{

constexpr uint32_t kDisplayBg = 0x1C1812;
constexpr uint32_t kDisplayHistory = 0xE8D2AB;
constexpr uint32_t kDisplayValue = 0xF8E6C3;
constexpr uint32_t kFunctionBg = 0xFFF0D3;
constexpr uint32_t kKeyBg = 0xFFF7E9;
constexpr uint32_t kControlBg = 0xF8E6C3;
constexpr uint32_t kOperatorBg = 0xF3D193;
constexpr uint32_t kDangerBg = 0xE9B7A3;
constexpr uint32_t kLine = 0xD9B06A;
constexpr uint32_t kText = 0x3A2A1A;
constexpr uint32_t kTextDim = 0x6A5646;
constexpr uint32_t kAmber = 0xEBA341;

enum class KeyAction : uint8_t
{
    SecondLayer,
    ToggleAngle,
    ToggleSign,
    CursorLeft,
    CursorRight,
    Backspace,
    ClearEntry,
    AllClear,
    Digit0,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,
    Decimal,
    Pi,
    Answer,
    Percent,
    OpenParenthesis,
    CloseParenthesis,
    Equals,
    Add,
    Subtract,
    Multiply,
    Divide,
    Sin,
    Cos,
    Tan,
    Square,
    SquareRoot,
    Reciprocal,
    Asin,
    Acos,
    Atan,
    NaturalLog,
    CommonLog,
    Power,
};

struct KeySpec
{
    KeyAction action;
    const char* label;
    uint32_t color;
};

struct UiState
{
    const ::ui::page::Host* host = nullptr;
    lv_obj_t* root = nullptr;
    lv_obj_t* display = nullptr;
    lv_obj_t* history = nullptr;
    lv_obj_t* result = nullptr;
    lv_obj_t* footer = nullptr;
    ::ui::widgets::TopBar top_bar{};
    ::calculator::ui::layout::Geometry layout{};
    std::array<lv_obj_t*, 6> function_buttons{};
    std::array<lv_obj_t*, 25> keypad_buttons{};
    ::calculator::ui::model::Engine engine{};
};

UiState s_ui;

void requestExit()
{
    if (s_ui.host)
    {
        ::ui::page::request_exit(s_ui.host);
        return;
    }
    ui_request_exit_to_menu();
}

void updateUi();

void applyAction(KeyAction action)
{
    using namespace calculator::ui::model;
    switch (action)
    {
    case KeyAction::SecondLayer:
        s_ui.engine.toggleSecondLayer();
        break;
    case KeyAction::ToggleAngle:
        s_ui.engine.toggleAngleMode();
        break;
    case KeyAction::ToggleSign:
        s_ui.engine.toggleSign();
        break;
    case KeyAction::CursorLeft:
        s_ui.engine.moveCursorLeft();
        break;
    case KeyAction::CursorRight:
        s_ui.engine.moveCursorRight();
        break;
    case KeyAction::Backspace:
        s_ui.engine.backspace();
        break;
    case KeyAction::ClearEntry:
        s_ui.engine.clearEntry();
        break;
    case KeyAction::AllClear:
        s_ui.engine.allClear();
        break;
    case KeyAction::Digit0:
    case KeyAction::Digit1:
    case KeyAction::Digit2:
    case KeyAction::Digit3:
    case KeyAction::Digit4:
    case KeyAction::Digit5:
    case KeyAction::Digit6:
    case KeyAction::Digit7:
    case KeyAction::Digit8:
    case KeyAction::Digit9:
        s_ui.engine.inputDigit(static_cast<char>('0' + (static_cast<uint8_t>(action) -
                                                        static_cast<uint8_t>(KeyAction::Digit0))));
        break;
    case KeyAction::Decimal:
        s_ui.engine.inputDecimalPoint();
        break;
    case KeyAction::Pi:
        s_ui.engine.inputPi();
        break;
    case KeyAction::Answer:
        s_ui.engine.inputAnswer();
        break;
    case KeyAction::Percent:
        s_ui.engine.inputPercent();
        break;
    case KeyAction::OpenParenthesis:
        s_ui.engine.insertOpenParenthesis();
        break;
    case KeyAction::CloseParenthesis:
        s_ui.engine.insertCloseParenthesis();
        break;
    case KeyAction::Equals:
        s_ui.engine.equals();
        break;
    case KeyAction::Add:
        s_ui.engine.selectOperation(Operation::Add);
        break;
    case KeyAction::Subtract:
        s_ui.engine.selectOperation(Operation::Subtract);
        break;
    case KeyAction::Multiply:
        s_ui.engine.selectOperation(Operation::Multiply);
        break;
    case KeyAction::Divide:
        s_ui.engine.selectOperation(Operation::Divide);
        break;
    case KeyAction::Sin:
        s_ui.engine.beginFunction(Function::Sin);
        break;
    case KeyAction::Cos:
        s_ui.engine.beginFunction(Function::Cos);
        break;
    case KeyAction::Tan:
        s_ui.engine.beginFunction(Function::Tan);
        break;
    case KeyAction::Square:
        s_ui.engine.beginFunction(Function::Square);
        break;
    case KeyAction::SquareRoot:
        s_ui.engine.beginFunction(Function::SquareRoot);
        break;
    case KeyAction::Reciprocal:
        s_ui.engine.beginFunction(Function::Reciprocal);
        break;
    case KeyAction::Asin:
        s_ui.engine.beginFunction(Function::Asin);
        break;
    case KeyAction::Acos:
        s_ui.engine.beginFunction(Function::Acos);
        break;
    case KeyAction::Atan:
        s_ui.engine.beginFunction(Function::Atan);
        break;
    case KeyAction::NaturalLog:
        s_ui.engine.beginFunction(Function::NaturalLog);
        break;
    case KeyAction::CommonLog:
        s_ui.engine.beginFunction(Function::CommonLog);
        break;
    case KeyAction::Power:
        s_ui.engine.selectOperation(Operation::Power);
        break;
    }
    updateUi();
}

void clicked(lv_event_t* event)
{
    // Keypad Enter is handled as Equals by keyEvent. LVGL also synthesizes a
    // click on release; applying the focused digit again would replace it.
    const auto* input = lv_indev_active();
    if (input && lv_indev_get_type(input) == LV_INDEV_TYPE_KEYPAD) return;
    const auto action = static_cast<KeyAction>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    applyAction(action);
}

bool handleKeyboard(uint32_t key)
{
    if (key >= '0' && key <= '9')
    {
        applyAction(static_cast<KeyAction>(static_cast<uint8_t>(KeyAction::Digit0) + key - '0'));
        return true;
    }
    switch (key)
    {
    case '.':
    case ',':
        applyAction(KeyAction::Decimal);
        return true;
    case '+':
        applyAction(KeyAction::Add);
        return true;
    case '-':
        applyAction(KeyAction::Subtract);
        return true;
    case '*':
        applyAction(KeyAction::Multiply);
        return true;
    case '/':
        applyAction(KeyAction::Divide);
        return true;
    case '^':
        applyAction(KeyAction::Power);
        return true;
    case '%':
        applyAction(KeyAction::Percent);
        return true;
    case '(':
        applyAction(KeyAction::OpenParenthesis);
        return true;
    case ')':
        applyAction(KeyAction::CloseParenthesis);
        return true;
    case '=':
    case LV_KEY_ENTER:
        applyAction(KeyAction::Equals);
        return true;
    case LV_KEY_BACKSPACE:
        applyAction(KeyAction::Backspace);
        return true;
    case LV_KEY_LEFT:
        applyAction(KeyAction::CursorLeft);
        return true;
    case LV_KEY_RIGHT:
        applyAction(KeyAction::CursorRight);
        return true;
    case LV_KEY_ESC:
        requestExit();
        return true;
    case 's':
        applyAction(KeyAction::Sin);
        return true;
    case 'c':
        applyAction(KeyAction::Cos);
        return true;
    case 't':
        applyAction(KeyAction::Tan);
        return true;
    case 'S':
        applyAction(KeyAction::Asin);
        return true;
    case 'C':
        applyAction(KeyAction::Acos);
        return true;
    case 'T':
        applyAction(KeyAction::Atan);
        return true;
    case 'm':
    case 'M':
        applyAction(KeyAction::ToggleAngle);
        return true;
    case 'f':
    case 'F':
        applyAction(KeyAction::SecondLayer);
        return true;
    case 'p':
    case 'P':
        applyAction(KeyAction::Pi);
        return true;
    case 'a':
    case 'A':
        applyAction(KeyAction::Answer);
        return true;
    case 'l':
        applyAction(KeyAction::NaturalLog);
        return true;
    case 'g':
    case 'G':
        applyAction(KeyAction::CommonLog);
        return true;
    case 'q':
    case 'Q':
        applyAction(KeyAction::Square);
        return true;
    case 'r':
    case 'R':
        applyAction(KeyAction::SquareRoot);
        return true;
    case 'i':
    case 'I':
        applyAction(KeyAction::Reciprocal);
        return true;
    default:
        return false;
    }
}

void keyEvent(lv_event_t* event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_KEY)
    {
        return;
    }
    (void)handleKeyboard(lv_event_get_key(event));
}

void backRequested(void*)
{
    requestExit();
}

void styleButton(lv_obj_t* button, uint32_t color)
{
    lv_obj_set_style_bg_color(button, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_radius(button, 3, LV_PART_MAIN);
    lv_obj_set_style_outline_width(button, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(button, lv_color_hex(kAmber), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_opa(button, LV_OPA_COVER, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(button, lv_color_hex(kAmber), LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
}

lv_obj_t* createButton(lv_obj_t* parent,
                       KeyAction action,
                       const char* label,
                       uint32_t color,
                       lv_coord_t x,
                       lv_coord_t y,
                       lv_coord_t width,
                       lv_coord_t height)
{
    lv_obj_t* button = lv_btn_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    styleButton(button, color);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(button,
                        clicked,
                        LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(action)));
    lv_obj_add_event_cb(button, keyEvent, LV_EVENT_KEY, nullptr);

    lv_obj_t* text = lv_label_create(button);
    lv_label_set_text(text, label);
    lv_obj_set_style_text_font(text,
                               height <= 18 ? &lv_font_montserrat_10
                                            : (height <= 23 ? &lv_font_montserrat_12
                                                            : (height <= 44 ? &lv_font_montserrat_14
                                                                            : &lv_font_montserrat_16)),
                               0);
    lv_obj_set_style_text_color(text, lv_color_hex(kText), 0);
    lv_obj_center(text);
    return button;
}

void updateFunctionRow()
{
    const bool second = s_ui.engine.secondLayer();
    const std::array<KeySpec, 6> primary = {{{KeyAction::Sin, "sin", kFunctionBg},
                                             {KeyAction::Cos, "cos", kFunctionBg},
                                             {KeyAction::Tan, "tan", kFunctionBg},
                                             {KeyAction::Square, "x^2", kFunctionBg},
                                             {KeyAction::SquareRoot, "sqrt", kFunctionBg},
                                             {KeyAction::Reciprocal, "1/x", kFunctionBg}}};
    const std::array<KeySpec, 6> secondary = {{{KeyAction::Asin, "sin-1", kFunctionBg},
                                               {KeyAction::Acos, "cos-1", kFunctionBg},
                                               {KeyAction::Atan, "tan-1", kFunctionBg},
                                               {KeyAction::NaturalLog, "ln", kFunctionBg},
                                               {KeyAction::CommonLog, "log", kFunctionBg},
                                               {KeyAction::Power, "x^y", kFunctionBg}}};
    const auto& specs = second ? secondary : primary;
    for (size_t index = 0; index < specs.size(); ++index)
    {
        if (!s_ui.function_buttons[index])
        {
            continue;
        }
        lv_obj_remove_event_cb(s_ui.function_buttons[index], clicked);
        lv_obj_add_event_cb(s_ui.function_buttons[index],
                            clicked,
                            LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(static_cast<uintptr_t>(specs[index].action)));
        lv_obj_t* label = lv_obj_get_child(s_ui.function_buttons[index], 0);
        if (label)
        {
            lv_label_set_text(label, specs[index].label);
        }
    }
}

void updateUi()
{
    if (!s_ui.root || !lv_obj_is_valid(s_ui.root))
    {
        return;
    }
    ::ui::widgets::top_bar_set_right_text_ascii(s_ui.top_bar, s_ui.engine.angleModeText());
    lv_label_set_text(s_ui.history, s_ui.engine.historyText());
    lv_label_set_text(s_ui.result, s_ui.engine.displayText());
    lv_obj_set_style_text_color(s_ui.result,
                                lv_color_hex(s_ui.engine.hasError() ? kDangerBg : kDisplayValue),
                                0);
    lv_label_set_text(s_ui.footer,
                      s_ui.engine.secondLayer()
                          ? "< >: cursor  |  A: ANS  |  Fn: primary"
                          : "< >: cursor  |  ( ) group  |  =: calculate");
    updateFunctionRow();
}

void buildPage(lv_obj_t* parent)
{
    s_ui.layout = ::calculator::ui::layout::resolve(parent);
    const auto& layout = s_ui.layout;
    const lv_coord_t width = layout.width;
    const lv_coord_t height = layout.height;
    s_ui.root = lv_obj_create(parent);
    lv_obj_set_size(s_ui.root, width, height);
    lv_obj_set_style_bg_color(s_ui.root, ::ui::theme::page_bg(), 0);
    lv_obj_set_style_bg_opa(s_ui.root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.root, 0, 0);
    lv_obj_set_style_radius(s_ui.root, 0, 0);
    lv_obj_set_style_pad_all(s_ui.root, 0, 0);
    lv_obj_clear_flag(s_ui.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_ui.root, keyEvent, LV_EVENT_KEY, nullptr);

    ::ui::widgets::TopBarConfig topbar_config{};
    topbar_config.height = layout.top_bar_height;
    topbar_config.power_indicator = false;
    ::ui::widgets::top_bar_init(s_ui.top_bar, s_ui.root, topbar_config);
    ::ui::widgets::top_bar_set_title(s_ui.top_bar, "CALCULATOR");
    ::ui::widgets::top_bar_set_back_callback(s_ui.top_bar, backRequested, nullptr);
    ::ui::widgets::top_bar_set_right_text_ascii(s_ui.top_bar, "DEG");
    if (s_ui.top_bar.back_btn)
    {
        lv_obj_add_event_cb(s_ui.top_bar.back_btn, keyEvent, LV_EVENT_KEY, nullptr);
    }

    s_ui.display = lv_obj_create(s_ui.root);
    lv_obj_set_pos(s_ui.display, layout.content_x + layout.outer_margin, layout.display_y);
    lv_obj_set_size(s_ui.display, layout.content_width - layout.outer_margin * 2, layout.display_height);
    lv_obj_set_style_bg_color(s_ui.display, lv_color_hex(kDisplayBg), 0);
    lv_obj_set_style_bg_opa(s_ui.display, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_ui.display, 1, 0);
    lv_obj_set_style_border_color(s_ui.display, lv_color_hex(kLine), 0);
    lv_obj_set_style_radius(s_ui.display, 5, 0);
    lv_obj_set_style_pad_all(s_ui.display, 0, 0);
    lv_obj_clear_flag(s_ui.display, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.history = lv_label_create(s_ui.display);
    lv_obj_set_pos(s_ui.history, 9, 4);
    lv_obj_set_size(s_ui.history, layout.content_width - layout.outer_margin * 2 - 18, 14);
    lv_label_set_long_mode(s_ui.history, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_ui.history,
                               layout.large_touch ? &lv_font_montserrat_14 : &lv_font_montserrat_10,
                               0);
    lv_obj_set_style_text_color(s_ui.history, lv_color_hex(kDisplayHistory), 0);

    s_ui.result = lv_label_create(s_ui.display);
    const lv_coord_t result_y = layout.large_touch ? 30 : 20;
    lv_obj_set_pos(s_ui.result, 8, result_y);
    lv_obj_set_size(s_ui.result,
                    layout.content_width - layout.outer_margin * 2 - 18,
                    layout.display_height - result_y - 2);
    lv_label_set_long_mode(s_ui.result, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_ui.result, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(s_ui.result,
                               layout.display_height < 40 ? &lv_font_montserrat_12 : &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ui.result, lv_color_hex(kDisplayValue), 0);

    const lv_coord_t function_margin = layout.outer_margin;
    const lv_coord_t function_gap = layout.function_gap;
    const lv_coord_t function_width =
        (layout.content_width - function_margin * 2 - function_gap * 5) / 6;
    const std::array<KeySpec, 6> initial_functions = {{{KeyAction::Sin, "sin", kFunctionBg},
                                                       {KeyAction::Cos, "cos", kFunctionBg},
                                                       {KeyAction::Tan, "tan", kFunctionBg},
                                                       {KeyAction::Square, "x^2", kFunctionBg},
                                                       {KeyAction::SquareRoot, "sqrt", kFunctionBg},
                                                       {KeyAction::Reciprocal, "1/x", kFunctionBg}}};
    for (size_t index = 0; index < initial_functions.size(); ++index)
    {
        s_ui.function_buttons[index] = createButton(
            s_ui.root,
            initial_functions[index].action,
            initial_functions[index].label,
            initial_functions[index].color,
            layout.content_x + function_margin +
                static_cast<lv_coord_t>(index) * (function_width + function_gap),
            layout.function_y,
            function_width,
            layout.function_height);
    }

    const std::array<KeySpec, 25> keypad = {{{KeyAction::SecondLayer, "2ND", kAmber},
                                             {KeyAction::ToggleAngle, "MODE", kControlBg},
                                             {KeyAction::CursorLeft, "<", kControlBg},
                                             {KeyAction::CursorRight, ">", kControlBg},
                                             {KeyAction::Backspace, "DEL", kControlBg},
                                             {KeyAction::Digit7, "7", kKeyBg},
                                             {KeyAction::Digit8, "8", kKeyBg},
                                             {KeyAction::Digit9, "9", kKeyBg},
                                             {KeyAction::Divide, "/", kOperatorBg},
                                             {KeyAction::Multiply, "x", kOperatorBg},
                                             {KeyAction::Digit4, "4", kKeyBg},
                                             {KeyAction::Digit5, "5", kKeyBg},
                                             {KeyAction::Digit6, "6", kKeyBg},
                                             {KeyAction::Subtract, "-", kOperatorBg},
                                             {KeyAction::Add, "+", kOperatorBg},
                                             {KeyAction::Digit1, "1", kKeyBg},
                                             {KeyAction::Digit2, "2", kKeyBg},
                                             {KeyAction::Digit3, "3", kKeyBg},
                                             {KeyAction::OpenParenthesis, "(", kControlBg},
                                             {KeyAction::CloseParenthesis, ")", kControlBg},
                                             {KeyAction::Digit0, "0", kKeyBg},
                                             {KeyAction::Pi, "PI", kControlBg},
                                             {KeyAction::Decimal, ".", kKeyBg},
                                             {KeyAction::AllClear, "AC", kDangerBg},
                                             {KeyAction::Equals, "=", kAmber}}};
    const lv_coord_t keypad_margin = layout.outer_margin;
    const lv_coord_t keypad_width =
        (layout.content_width - keypad_margin * 2 - layout.keypad_gap * 4) / 5;
    for (size_t index = 0; index < keypad.size(); ++index)
    {
        const lv_coord_t row = static_cast<lv_coord_t>(index / 5);
        const lv_coord_t column = static_cast<lv_coord_t>(index % 5);
        s_ui.keypad_buttons[index] = createButton(s_ui.root,
                                                  keypad[index].action,
                                                  keypad[index].label,
                                                  keypad[index].color,
                                                  layout.content_x + keypad_margin +
                                                      column * (keypad_width + layout.keypad_gap),
                                                  layout.keypad_y + row * (layout.keypad_button_height + layout.keypad_gap),
                                                  keypad_width,
                                                  layout.keypad_button_height);
    }

    s_ui.footer = lv_label_create(s_ui.root);
    lv_obj_set_pos(s_ui.footer, layout.content_x + layout.outer_margin + 2, layout.footer_y);
    lv_obj_set_size(s_ui.footer,
                    layout.content_width - (layout.outer_margin + 2) * 2,
                    layout.footer_height);
    lv_label_set_long_mode(s_ui.footer, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_ui.footer,
                               layout.large_touch ? &lv_font_montserrat_14 : &lv_font_montserrat_10,
                               0);
    lv_obj_set_style_text_color(s_ui.footer, lv_color_hex(kTextDim), 0);
}

void restoreFocus()
{
    if (!::app_g)
    {
        return;
    }
    lv_group_remove_all_objs(::app_g);
    if (s_ui.top_bar.back_btn)
    {
        lv_group_add_obj(::app_g, s_ui.top_bar.back_btn);
    }
    for (lv_obj_t* button : s_ui.function_buttons)
    {
        if (button)
        {
            lv_group_add_obj(::app_g, button);
        }
    }
    for (lv_obj_t* button : s_ui.keypad_buttons)
    {
        if (button)
        {
            lv_group_add_obj(::app_g, button);
        }
    }
    if (s_ui.keypad_buttons[5])
    {
        lv_group_focus_obj(s_ui.keypad_buttons[5]);
    }
    set_default_group(::app_g);
    lv_group_set_editing(::app_g, false);
}

} // namespace

namespace calculator::ui::runtime
{

void enter(const ::ui::page::Host* host, lv_obj_t* parent)
{
    if (!parent)
    {
        return;
    }
    if (s_ui.root && lv_obj_is_valid(s_ui.root))
    {
        lv_obj_del(s_ui.root);
    }
    s_ui = {};
    s_ui.host = host;
    s_ui.engine.allClear();
    lv_group_t* previous_group = lv_group_get_default();
    set_default_group(nullptr);
    buildPage(parent);
    updateUi();
    restoreFocus();
    if (!::app_g)
    {
        set_default_group(previous_group);
    }
}

void exit(lv_obj_t* parent)
{
    (void)parent;
    if (s_ui.root && lv_obj_is_valid(s_ui.root))
    {
        lv_obj_del(s_ui.root);
    }
    s_ui = {};
}

} // namespace calculator::ui::runtime
