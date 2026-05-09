#include "uconsole/uconsole_desktop_shell.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "app/input_event.h"
#include "app/linux_app_services.h"
#include "core/canvas.h"
#include "lvgl.h"
#include "uconsole/uconsole_chat_workspace_model.h"
#include "uconsole/uconsole_dashboard_model.h"

namespace trailmate::uconsole
{
namespace
{

using clock = std::chrono::steady_clock;
using InputEvent = cardputer_zero::app::InputEvent;
using InputKey = cardputer_zero::app::InputKey;
using Canvas = cardputer_zero::core::Canvas;

constexpr int kConversationRows = 6;
constexpr int kContactRows = 7;
constexpr int kMetricCount = 4;
constexpr int kCapabilityRows = 5;
constexpr int kChatConversationRows = 7;
constexpr int kChatMessageRows = 7;

constexpr std::array<const char*, 8> kNavLabels{
    "Overview",
    "Chat",
    "Contacts",
    "Map",
    "Team",
    "Data",
    "Diagnostics",
    "Settings",
};

std::chrono::steady_clock::time_point g_lvgl_start_time = clock::now();

enum class Section : std::uint8_t
{
    Overview = 0,
    Chat,
    Contacts,
    Map,
    Team,
    Data,
    Diagnostics,
    Settings,
};

struct QueuedKeyEvent
{
    std::uint32_t key = 0;
    lv_indev_state_t state = LV_INDEV_STATE_RELEASED;
};

[[nodiscard]] std::uint32_t tickNow() noexcept
{
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            clock::now() - g_lvgl_start_time)
            .count());
}

[[nodiscard]] std::uint32_t mapInputEvent(const InputEvent& event) noexcept
{
    switch (event.key)
    {
    case InputKey::Character:
        return event.text == '\0' ? 0U : static_cast<std::uint8_t>(event.text);
    case InputKey::Backspace:
        return LV_KEY_BACKSPACE;
    case InputKey::Enter:
        return LV_KEY_ENTER;
    case InputKey::Tab:
        return LV_KEY_NEXT;
    case InputKey::Home:
        return LV_KEY_ESC;
    case InputKey::Next:
        return LV_KEY_NEXT;
    case InputKey::Power:
        return LV_KEY_ESC;
    case InputKey::Left:
        return LV_KEY_LEFT;
    case InputKey::Right:
        return LV_KEY_RIGHT;
    case InputKey::Up:
        return LV_KEY_UP;
    case InputKey::Down:
        return LV_KEY_DOWN;
    case InputKey::Unknown:
    case InputKey::Fn:
    case InputKey::Ctrl:
    case InputKey::Alt:
    case InputKey::Shift:
        return 0U;
    }
    return 0U;
}

[[nodiscard]] std::uint8_t expand5(std::uint16_t value) noexcept
{
    return static_cast<std::uint8_t>((value * 255U) / 31U);
}

[[nodiscard]] std::uint8_t expand6(std::uint16_t value) noexcept
{
    return static_cast<std::uint8_t>((value * 255U) / 63U);
}

[[nodiscard]] cardputer_zero::core::Color rgb565ToColor(
    std::uint16_t pixel) noexcept
{
    const auto red = static_cast<std::uint16_t>((pixel >> 11U) & 0x1FU);
    const auto green = static_cast<std::uint16_t>((pixel >> 5U) & 0x3FU);
    const auto blue = static_cast<std::uint16_t>(pixel & 0x1FU);
    return cardputer_zero::core::rgba(expand5(red), expand6(green),
                                      expand5(blue));
}

[[nodiscard]] lv_color_t color(std::uint32_t hex) noexcept
{
    return lv_color_hex(hex);
}

void resetBox(lv_obj_t* obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void applyPanel(lv_obj_t* obj, std::uint32_t bg, std::uint32_t border = 0xD3D9D2)
{
    resetBox(obj);
    lv_obj_set_style_bg_color(obj, color(bg), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, color(border), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 6, 0);
    lv_obj_set_style_pad_all(obj, 14, 0);
}

void applyTransparent(lv_obj_t* obj)
{
    resetBox(obj);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

lv_obj_t* createLabel(lv_obj_t* parent,
                      const char* text,
                      const lv_font_t* font,
                      std::uint32_t text_color,
                      lv_label_long_mode_t long_mode = LV_LABEL_LONG_DOT)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_color(label, color(text_color), 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_label_set_long_mode(label, long_mode);
    lv_label_set_text(label, text);
    return label;
}

void setLabel(lv_obj_t* label, const std::string& text)
{
    if (label != nullptr)
    {
        lv_label_set_text(label, text.c_str());
    }
}

void setLabel(lv_obj_t* label, const char* text)
{
    if (label != nullptr)
    {
        lv_label_set_text(label, text ? text : "");
    }
}

[[nodiscard]] const char* sectionTitle(Section section) noexcept
{
    switch (section)
    {
    case Section::Overview:
        return "Operational workspace";
    case Section::Chat:
        return "Chat workspace";
    case Section::Contacts:
        return "Contacts workspace";
    case Section::Map:
        return "Map workspace";
    case Section::Team:
        return "Team workspace";
    case Section::Data:
        return "Data workspace";
    case Section::Diagnostics:
        return "Diagnostics workspace";
    case Section::Settings:
        return "Settings workspace";
    }
    return "Operational workspace";
}

[[nodiscard]] const char* sectionSubtitle(Section section) noexcept
{
    switch (section)
    {
    case Section::Overview:
        return "Live service snapshot for the Linux handheld target.";
    case Section::Chat:
        return "Conversation list and message activity preview.";
    case Section::Contacts:
        return "Known nodes, nearby peers and trust status preview.";
    case Section::Map:
        return "Map canvas slot reserved for Linux package workflows.";
    case Section::Team:
        return "Roster, pairing and activity slot for team operations.";
    case Section::Data:
        return "Import/export and local storage jobs will surface here.";
    case Section::Diagnostics:
        return "Runtime logs, capability state and hardware checks.";
    case Section::Settings:
        return "Grouped configuration entrypoint for Linux targets.";
    }
    return "";
}

[[nodiscard]] std::string formatCount(std::size_t value)
{
    char buffer[24] = {};
    std::snprintf(buffer, sizeof(buffer), "%lu",
                  static_cast<unsigned long>(value));
    return buffer;
}

[[nodiscard]] std::string formatCount(int value)
{
    char buffer[24] = {};
    std::snprintf(buffer, sizeof(buffer), "%d", value);
    return buffer;
}

[[nodiscard]] UConsoleShellOptions validateOptions(UConsoleShellOptions options)
{
    if (options.width <= 0 || options.height <= 0)
    {
        throw std::runtime_error("uConsole shell dimensions must be positive.");
    }
    if (options.frame_time_ms <= 0)
    {
        options.frame_time_ms = 16;
    }
    return options;
}

class UConsoleDesktopShell
{
  public:
    UConsoleDesktopShell()
        : services_(),
          dashboard_model_(services_),
          chat_model_(services_)
    {
    }

    ~UConsoleDesktopShell()
    {
        services_.shutdown();
    }

    bool begin()
    {
        if (initialized_) return true;
        if (!services_.initialize()) return false;

        group_ = lv_group_create();
        if (group_ == nullptr)
        {
            return false;
        }
        lv_group_set_default(group_);

        buildUi();
        initialized_ = true;
        refreshDashboard(true);
        return true;
    }

    void releaseLvglObjects() noexcept
    {
        if (group_ != nullptr)
        {
            lv_group_del(group_);
            group_ = nullptr;
        }
    }

    void tick()
    {
        if (!initialized_) return;

        services_.tick();
        const auto now = clock::now();
        if ((now - last_refresh_) >= std::chrono::milliseconds(500))
        {
            refreshDashboard(false);
        }
    }

    void enqueueInputs(const std::vector<InputEvent>& events)
    {
        for (const auto& event : events)
        {
            const std::uint32_t mapped = mapInputEvent(event);
            if (mapped == 0U) continue;
            key_events_.push_back({mapped, LV_INDEV_STATE_PRESSED});
            key_events_.push_back({mapped, LV_INDEV_STATE_RELEASED});
        }
    }

    [[nodiscard]] lv_group_t* inputGroup() const noexcept
    {
        return group_;
    }

    bool dequeueKeyEvent(std::uint32_t* key, lv_indev_state_t* state)
    {
        if (key_events_.empty()) return false;
        const QueuedKeyEvent event = key_events_.front();
        key_events_.pop_front();
        *key = event.key;
        *state = event.state;
        return true;
    }

    [[nodiscard]] bool hasPendingKeyEvent() const noexcept
    {
        return !key_events_.empty();
    }

  private:
    struct NavBinding
    {
        UConsoleDesktopShell* shell = nullptr;
        Section section = Section::Overview;
    };

    struct ChatConversationBinding
    {
        UConsoleDesktopShell* shell = nullptr;
        std::size_t index = 0;
    };

    static void navEventCb(lv_event_t* event)
    {
        const lv_event_code_t code = lv_event_get_code(event);
        if (code != LV_EVENT_CLICKED && code != LV_EVENT_KEY)
        {
            return;
        }

        if (code == LV_EVENT_KEY)
        {
            const std::uint32_t key = lv_event_get_key(event);
            if (key != LV_KEY_ENTER && key != LV_KEY_RIGHT)
            {
                return;
            }
        }

        auto* binding =
            static_cast<NavBinding*>(lv_event_get_user_data(event));
        if (binding == nullptr || binding->shell == nullptr)
        {
            return;
        }
        binding->shell->selectSection(binding->section);
    }

    static void chatConversationEventCb(lv_event_t* event)
    {
        const lv_event_code_t code = lv_event_get_code(event);
        if (code != LV_EVENT_CLICKED && code != LV_EVENT_KEY)
        {
            return;
        }

        if (code == LV_EVENT_KEY)
        {
            const std::uint32_t key = lv_event_get_key(event);
            if (key != LV_KEY_ENTER && key != LV_KEY_RIGHT)
            {
                return;
            }
        }

        auto* binding =
            static_cast<ChatConversationBinding*>(lv_event_get_user_data(event));
        if (binding == nullptr || binding->shell == nullptr)
        {
            return;
        }

        if (binding->shell->chat_model_.selectConversationAt(
                binding->index, kChatConversationRows))
        {
            binding->shell->refreshDashboard(true);
        }
    }

    static void chatSendEventCb(lv_event_t* event)
    {
        const lv_event_code_t code = lv_event_get_code(event);
        if (code != LV_EVENT_CLICKED && code != LV_EVENT_KEY)
        {
            return;
        }

        if (code == LV_EVENT_KEY)
        {
            const std::uint32_t key = lv_event_get_key(event);
            if (key != LV_KEY_ENTER)
            {
                return;
            }
        }

        auto* shell =
            static_cast<UConsoleDesktopShell*>(lv_event_get_user_data(event));
        if (shell == nullptr || shell->chat_input_ == nullptr)
        {
            return;
        }

        const char* text = lv_textarea_get_text(shell->chat_input_);
        const bool sent = shell->chat_model_.sendText(text == nullptr ? "" : text);
        if (sent)
        {
            lv_textarea_set_text(shell->chat_input_, "");
        }
        shell->refreshDashboard(true);
        shell->refreshChatWorkspace(true);
    }

    void selectSection(Section section)
    {
        active_section_ = section;
        refreshNavStyles();
        setLabel(workspace_title_, sectionTitle(active_section_));
        setLabel(workspace_subtitle_, sectionSubtitle(active_section_));
        refreshSectionVisibility();
        if (active_section_ == Section::Chat)
        {
            refreshChatWorkspace(true);
        }
    }

    void buildUi()
    {
        lv_obj_t* root = lv_scr_act();
        lv_obj_clean(root);
        resetBox(root);
        lv_obj_set_style_bg_color(root, color(0xECEFEA), 0);
        lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(root, 0, 0);
        lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);

        buildTopBar(root);

        lv_obj_t* body = lv_obj_create(root);
        applyTransparent(body);
        lv_obj_set_width(body, LV_PCT(100));
        lv_obj_set_flex_grow(body, 1);
        lv_obj_set_style_pad_all(body, 14, 0);
        lv_obj_set_style_pad_column(body, 14, 0);
        lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);

        buildSidebar(body);
        buildWorkspace(body);
        buildStatusPanel(body);

        selectSection(Section::Overview);
    }

    void buildTopBar(lv_obj_t* root)
    {
        lv_obj_t* bar = lv_obj_create(root);
        resetBox(bar);
        lv_obj_set_width(bar, LV_PCT(100));
        lv_obj_set_height(bar, 58);
        lv_obj_set_style_bg_color(bar, color(0x202426), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_left(bar, 18, 0);
        lv_obj_set_style_pad_right(bar, 18, 0);
        lv_obj_set_style_pad_top(bar, 8, 0);
        lv_obj_set_style_pad_bottom(bar, 8, 0);
        lv_obj_set_style_pad_column(bar, 12, 0);
        lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        lv_obj_t* title_wrap = lv_obj_create(bar);
        applyTransparent(title_wrap);
        lv_obj_set_height(title_wrap, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(title_wrap, 1);
        lv_obj_set_flex_flow(title_wrap, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(title_wrap, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        createLabel(title_wrap, "Trail Mate uConsole", &lv_font_montserrat_20,
                    0xF7F8F4);
        createLabel(title_wrap, "Linux desktop-class target",
                    &lv_font_montserrat_12, 0xAAB2AF);

        top_mesh_label_ = createChip(bar, "Mesh: -", 0x2F5D62, 0xDDF8F0);
        top_node_label_ = createChip(bar, "Node: -", 0x584A1F, 0xFFF1BF);
        top_unread_label_ = createChip(bar, "Unread: 0", 0x4F3B4D, 0xF8DDF2);
    }

    lv_obj_t* createChip(lv_obj_t* parent,
                         const char* text,
                         std::uint32_t bg,
                         std::uint32_t fg)
    {
        lv_obj_t* chip = lv_obj_create(parent);
        resetBox(chip);
        lv_obj_set_size(chip, LV_SIZE_CONTENT, 32);
        lv_obj_set_style_bg_color(chip, color(bg), 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(chip, 6, 0);
        lv_obj_set_style_pad_left(chip, 10, 0);
        lv_obj_set_style_pad_right(chip, 10, 0);
        lv_obj_t* label =
            createLabel(chip, text, &lv_font_montserrat_12, fg);
        lv_obj_center(label);
        return label;
    }

    void buildSidebar(lv_obj_t* parent)
    {
        sidebar_ = lv_obj_create(parent);
        applyPanel(sidebar_, 0x252A29, 0x252A29);
        lv_obj_set_width(sidebar_, 220);
        lv_obj_set_height(sidebar_, LV_PCT(100));
        lv_obj_set_style_pad_all(sidebar_, 12, 0);
        lv_obj_set_style_pad_row(sidebar_, 8, 0);
        lv_obj_set_flex_flow(sidebar_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(sidebar_, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        createLabel(sidebar_, "Workspace", &lv_font_montserrat_14, 0xE8EDE8);

        for (std::size_t index = 0; index < kNavLabels.size(); ++index)
        {
            nav_bindings_[index] = {this, static_cast<Section>(index)};
            nav_buttons_[index] =
                createNavButton(sidebar_, kNavLabels[index],
                                &nav_bindings_[index]);
        }
    }

    lv_obj_t* createNavButton(lv_obj_t* parent,
                              const char* label_text,
                              NavBinding* binding)
    {
        lv_obj_t* button = lv_btn_create(parent);
        resetBox(button);
        lv_obj_set_width(button, LV_PCT(100));
        lv_obj_set_height(button, 42);
        lv_obj_set_style_radius(button, 6, 0);
        lv_obj_set_style_pad_left(button, 10, 0);
        lv_obj_set_style_pad_right(button, 10, 0);
        lv_obj_set_style_bg_color(button, color(0x303635), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_add_event_cb(button, navEventCb, LV_EVENT_CLICKED, binding);
        lv_obj_add_event_cb(button, navEventCb, LV_EVENT_KEY, binding);
        lv_group_add_obj(group_, button);

        lv_obj_t* label =
            createLabel(button, label_text, &lv_font_montserrat_14, 0xD7DED9);
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_center(label);
        return button;
    }

    void buildWorkspace(lv_obj_t* parent)
    {
        lv_obj_t* workspace = lv_obj_create(parent);
        applyTransparent(workspace);
        lv_obj_set_height(workspace, LV_PCT(100));
        lv_obj_set_flex_grow(workspace, 1);
        lv_obj_set_style_pad_row(workspace, 12, 0);
        lv_obj_set_flex_flow(workspace, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(workspace, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        lv_obj_t* header = lv_obj_create(workspace);
        applyTransparent(header);
        lv_obj_set_width(header, LV_PCT(100));
        lv_obj_set_height(header, 54);
        lv_obj_set_flex_flow(header, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(header, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        workspace_title_ = createLabel(header, sectionTitle(active_section_),
                                       &lv_font_montserrat_20, 0x1F2523);
        workspace_subtitle_ =
            createLabel(header, sectionSubtitle(active_section_),
                        &lv_font_montserrat_12, 0x61706A);

        metrics_panel_ = lv_obj_create(workspace);
        applyTransparent(metrics_panel_);
        lv_obj_set_width(metrics_panel_, LV_PCT(100));
        lv_obj_set_height(metrics_panel_, 96);
        lv_obj_set_style_pad_column(metrics_panel_, 10, 0);
        lv_obj_set_flex_flow(metrics_panel_, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(metrics_panel_, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        createMetric(metrics_panel_, 0, "Threads");
        createMetric(metrics_panel_, 1, "Unread");
        createMetric(metrics_panel_, 2, "Contacts");
        createMetric(metrics_panel_, 3, "Nearby");

        conversation_panel_ = lv_obj_create(workspace);
        applyPanel(conversation_panel_, 0xFFFFFF);
        lv_obj_set_width(conversation_panel_, LV_PCT(100));
        lv_obj_set_flex_grow(conversation_panel_, 1);
        lv_obj_set_style_pad_row(conversation_panel_, 8, 0);
        lv_obj_set_flex_flow(conversation_panel_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(conversation_panel_, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        createLabel(conversation_panel_, "Recent conversations",
                    &lv_font_montserrat_16, 0x24302C);
        for (int index = 0; index < kConversationRows; ++index)
        {
            buildConversationRow(index);
        }

        buildChatWorkspace(workspace);
    }

    void createMetric(lv_obj_t* parent, int index, const char* title)
    {
        lv_obj_t* box = lv_obj_create(parent);
        applyPanel(box, 0xFFFFFF);
        lv_obj_set_height(box, LV_PCT(100));
        lv_obj_set_flex_grow(box, 1);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        createLabel(box, title, &lv_font_montserrat_12, 0x66716E);
        metric_value_labels_[index] =
            createLabel(box, "0", &lv_font_montserrat_24, 0x1E2B27);
    }

    void buildConversationRow(int index)
    {
        lv_obj_t* row = lv_obj_create(conversation_panel_);
        applyTransparent(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 62);
        lv_obj_set_style_pad_top(row, 4, 0);
        lv_obj_set_style_pad_bottom(row, 4, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);

        conversation_rows_[index] = row;
        conversation_title_labels_[index] =
            createLabel(row, "-", &lv_font_montserrat_14, 0x22302B);
        lv_obj_set_width(conversation_title_labels_[index], LV_PCT(100));
        conversation_preview_labels_[index] =
            createLabel(row, "", &lv_font_montserrat_12, 0x596760);
        lv_obj_set_width(conversation_preview_labels_[index], LV_PCT(100));
        conversation_meta_labels_[index] =
            createLabel(row, "", &lv_font_montserrat_12, 0x8A6A20);
        lv_obj_set_width(conversation_meta_labels_[index], LV_PCT(100));
    }

    void buildChatWorkspace(lv_obj_t* parent)
    {
        chat_panel_ = lv_obj_create(parent);
        applyPanel(chat_panel_, 0xFFFFFF);
        lv_obj_set_width(chat_panel_, LV_PCT(100));
        lv_obj_set_flex_grow(chat_panel_, 1);
        lv_obj_set_style_pad_column(chat_panel_, 12, 0);
        lv_obj_set_flex_flow(chat_panel_, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(chat_panel_, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        lv_obj_t* conversation_list = lv_obj_create(chat_panel_);
        applyTransparent(conversation_list);
        lv_obj_set_width(conversation_list, 310);
        lv_obj_set_height(conversation_list, LV_PCT(100));
        lv_obj_set_style_pad_row(conversation_list, 8, 0);
        lv_obj_set_flex_flow(conversation_list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(conversation_list, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        createLabel(conversation_list, "Conversations", &lv_font_montserrat_16,
                    0x24302C);
        for (int index = 0; index < kChatConversationRows; ++index)
        {
            buildChatConversationRow(conversation_list, index);
        }

        lv_obj_t* thread = lv_obj_create(chat_panel_);
        applyTransparent(thread);
        lv_obj_set_height(thread, LV_PCT(100));
        lv_obj_set_flex_grow(thread, 1);
        lv_obj_set_style_pad_row(thread, 10, 0);
        lv_obj_set_flex_flow(thread, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(thread, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        lv_obj_t* thread_header = lv_obj_create(thread);
        applyTransparent(thread_header);
        lv_obj_set_width(thread_header, LV_PCT(100));
        lv_obj_set_height(thread_header, 48);
        lv_obj_set_flex_flow(thread_header, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(thread_header, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        chat_title_label_ =
            createLabel(thread_header, "-", &lv_font_montserrat_16, 0x1F2523);
        chat_meta_label_ =
            createLabel(thread_header, "-", &lv_font_montserrat_12, 0x61706A);
        lv_obj_set_width(chat_title_label_, LV_PCT(100));
        lv_obj_set_width(chat_meta_label_, LV_PCT(100));

        chat_messages_panel_ = lv_obj_create(thread);
        applyPanel(chat_messages_panel_, 0xF8FAF6);
        lv_obj_set_width(chat_messages_panel_, LV_PCT(100));
        lv_obj_set_flex_grow(chat_messages_panel_, 1);
        lv_obj_set_style_pad_row(chat_messages_panel_, 8, 0);
        lv_obj_set_flex_flow(chat_messages_panel_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(chat_messages_panel_, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        chat_empty_label_ =
            createLabel(chat_messages_panel_, "No messages yet.",
                        &lv_font_montserrat_14, 0x65716B);
        lv_obj_set_width(chat_empty_label_, LV_PCT(100));
        for (int index = 0; index < kChatMessageRows; ++index)
        {
            buildChatMessageRow(index);
        }

        lv_obj_t* input_row = lv_obj_create(thread);
        applyTransparent(input_row);
        lv_obj_set_width(input_row, LV_PCT(100));
        lv_obj_set_height(input_row, 72);
        lv_obj_set_style_pad_column(input_row, 10, 0);
        lv_obj_set_flex_flow(input_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(input_row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        chat_input_ = lv_textarea_create(input_row);
        lv_obj_set_height(chat_input_, 54);
        lv_obj_set_flex_grow(chat_input_, 1);
        lv_textarea_set_one_line(chat_input_, true);
        lv_textarea_set_placeholder_text(chat_input_, "Type a message");
        lv_obj_set_style_text_font(chat_input_, &lv_font_montserrat_14, 0);
        lv_obj_set_style_radius(chat_input_, 6, 0);
        lv_group_add_obj(group_, chat_input_);

        chat_send_button_ = lv_btn_create(input_row);
        resetBox(chat_send_button_);
        lv_obj_set_size(chat_send_button_, 92, 54);
        lv_obj_set_style_radius(chat_send_button_, 6, 0);
        lv_obj_set_style_bg_color(chat_send_button_, color(0x2F5D62), 0);
        lv_obj_set_style_bg_opa(chat_send_button_, LV_OPA_COVER, 0);
        lv_obj_add_event_cb(chat_send_button_, chatSendEventCb,
                            LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(chat_send_button_, chatSendEventCb, LV_EVENT_KEY,
                            this);
        lv_group_add_obj(group_, chat_send_button_);
        lv_obj_t* send_label =
            createLabel(chat_send_button_, "Send", &lv_font_montserrat_14,
                        0xF7F8F4);
        lv_obj_center(send_label);

        chat_status_label_ =
            createLabel(thread, "Ready.", &lv_font_montserrat_12, 0x66716E);
        lv_obj_set_width(chat_status_label_, LV_PCT(100));
    }

    void buildChatConversationRow(lv_obj_t* parent, int index)
    {
        chat_conversation_bindings_[index] = {this,
                                              static_cast<std::size_t>(index)};
        lv_obj_t* button = lv_btn_create(parent);
        resetBox(button);
        lv_obj_set_width(button, LV_PCT(100));
        lv_obj_set_height(button, 74);
        lv_obj_set_style_radius(button, 6, 0);
        lv_obj_set_style_pad_all(button, 8, 0);
        lv_obj_set_style_bg_color(button, color(0xF3F6F2), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_flex_flow(button, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_add_event_cb(button, chatConversationEventCb, LV_EVENT_CLICKED,
                            &chat_conversation_bindings_[index]);
        lv_obj_add_event_cb(button, chatConversationEventCb, LV_EVENT_KEY,
                            &chat_conversation_bindings_[index]);
        lv_group_add_obj(group_, button);

        chat_conversation_buttons_[index] = button;
        chat_conversation_title_labels_[index] =
            createLabel(button, "-", &lv_font_montserrat_14, 0x22302B);
        chat_conversation_preview_labels_[index] =
            createLabel(button, "", &lv_font_montserrat_12, 0x596760);
        chat_conversation_meta_labels_[index] =
            createLabel(button, "", &lv_font_montserrat_12, 0x8A6A20);
        lv_obj_set_width(chat_conversation_title_labels_[index], LV_PCT(100));
        lv_obj_set_width(chat_conversation_preview_labels_[index], LV_PCT(100));
        lv_obj_set_width(chat_conversation_meta_labels_[index], LV_PCT(100));
    }

    void buildChatMessageRow(int index)
    {
        lv_obj_t* row = lv_obj_create(chat_messages_panel_);
        applyPanel(row, 0xFFFFFF);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 72);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);

        chat_message_rows_[index] = row;
        chat_message_sender_labels_[index] =
            createLabel(row, "-", &lv_font_montserrat_12, 0x34413D);
        chat_message_text_labels_[index] =
            createLabel(row, "", &lv_font_montserrat_14, 0x17211E,
                        LV_LABEL_LONG_WRAP);
        chat_message_meta_labels_[index] =
            createLabel(row, "", &lv_font_montserrat_12, 0x66716E);
        lv_obj_set_width(chat_message_sender_labels_[index], LV_PCT(100));
        lv_obj_set_width(chat_message_text_labels_[index], LV_PCT(100));
        lv_obj_set_width(chat_message_meta_labels_[index], LV_PCT(100));
    }

    void buildStatusPanel(lv_obj_t* parent)
    {
        lv_obj_t* panel = lv_obj_create(parent);
        applyPanel(panel, 0xFFFFFF);
        lv_obj_set_width(panel, 330);
        lv_obj_set_height(panel, LV_PCT(100));
        lv_obj_set_style_pad_row(panel, 12, 0);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);

        createLabel(panel, "Runtime and capabilities", &lv_font_montserrat_16,
                    0x24302C);
        for (int index = 0; index < kCapabilityRows; ++index)
        {
            capability_labels_[index] =
                createLabel(panel, "-", &lv_font_montserrat_12, 0x4F5C57,
                            LV_LABEL_LONG_WRAP);
            lv_obj_set_width(capability_labels_[index], LV_PCT(100));
        }

        lv_obj_t* divider = lv_obj_create(panel);
        resetBox(divider);
        lv_obj_set_width(divider, LV_PCT(100));
        lv_obj_set_height(divider, 1);
        lv_obj_set_style_bg_color(divider, color(0xD9DEDA), 0);
        lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);

        createLabel(panel, "Contacts and nearby nodes", &lv_font_montserrat_16,
                    0x24302C);
        for (int index = 0; index < kContactRows; ++index)
        {
            buildContactRow(panel, index);
        }
    }

    void buildContactRow(lv_obj_t* parent, int index)
    {
        lv_obj_t* row = lv_obj_create(parent);
        applyTransparent(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 48);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);

        contact_rows_[index] = row;
        contact_name_labels_[index] =
            createLabel(row, "-", &lv_font_montserrat_14, 0x22302B);
        lv_obj_set_width(contact_name_labels_[index], LV_PCT(100));
        contact_meta_labels_[index] =
            createLabel(row, "", &lv_font_montserrat_12, 0x66716E);
        lv_obj_set_width(contact_meta_labels_[index], LV_PCT(100));
    }

    void refreshNavStyles()
    {
        const auto active_index = static_cast<std::size_t>(active_section_);
        for (std::size_t index = 0; index < nav_buttons_.size(); ++index)
        {
            lv_obj_t* button = nav_buttons_[index];
            if (button == nullptr) continue;
            const bool active = index == active_index;
            lv_obj_set_style_bg_color(button,
                                      color(active ? 0xD7E8DF : 0x303635), 0);
            lv_obj_set_style_border_width(button, active ? 1 : 0, 0);
            lv_obj_set_style_border_color(button, color(0x7EA48F), 0);
            lv_obj_t* label = lv_obj_get_child(button, 0);
            if (label != nullptr)
            {
                lv_obj_set_style_text_color(
                    label, color(active ? 0x15251F : 0xD7DED9), 0);
            }
        }
    }

    void refreshSectionVisibility()
    {
        const bool chat_active = active_section_ == Section::Chat;
        if (metrics_panel_ != nullptr)
        {
            if (chat_active)
                lv_obj_add_flag(metrics_panel_, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_clear_flag(metrics_panel_, LV_OBJ_FLAG_HIDDEN);
        }
        if (conversation_panel_ != nullptr)
        {
            if (chat_active)
                lv_obj_add_flag(conversation_panel_, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_clear_flag(conversation_panel_, LV_OBJ_FLAG_HIDDEN);
        }
        if (chat_panel_ != nullptr)
        {
            if (chat_active)
                lv_obj_clear_flag(chat_panel_, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(chat_panel_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void refreshDashboard(bool force)
    {
        const auto now = clock::now();
        if (!force && (now - last_refresh_) < std::chrono::milliseconds(500))
        {
            return;
        }
        last_refresh_ = now;

        const UConsoleDashboardSnapshot snapshot = dashboard_model_.snapshot();

        setLabel(top_mesh_label_, "Mesh: " + snapshot.mesh_protocol);
        setLabel(top_node_label_, "Node: " + snapshot.self_node);
        setLabel(top_unread_label_, "Unread: " + formatCount(snapshot.unread_count));

        setLabel(metric_value_labels_[0],
                 formatCount(snapshot.conversation_count));
        setLabel(metric_value_labels_[1], formatCount(snapshot.unread_count));
        setLabel(metric_value_labels_[2], formatCount(snapshot.contact_count));
        setLabel(metric_value_labels_[3], formatCount(snapshot.nearby_count));

        for (int index = 0; index < kConversationRows; ++index)
        {
            const bool visible =
                index < static_cast<int>(snapshot.conversations.size());
            if (visible)
            {
                lv_obj_clear_flag(conversation_rows_[index],
                                  LV_OBJ_FLAG_HIDDEN);
                const auto& item = snapshot.conversations[index];
                setLabel(conversation_title_labels_[index], item.title);
                setLabel(conversation_preview_labels_[index], item.preview);
                setLabel(conversation_meta_labels_[index], item.meta);
                lv_obj_set_style_text_color(
                    conversation_meta_labels_[index],
                    color(item.unread > 0 ? 0xA06316 : 0x78827D), 0);
            }
            else
            {
                lv_obj_add_flag(conversation_rows_[index],
                                LV_OBJ_FLAG_HIDDEN);
            }
        }

        for (int index = 0; index < kCapabilityRows; ++index)
        {
            if (index < static_cast<int>(snapshot.capability_lines.size()))
            {
                setLabel(capability_labels_[index],
                         snapshot.capability_lines[index]);
            }
            else
            {
                setLabel(capability_labels_[index], "");
            }
        }

        for (int index = 0; index < kContactRows; ++index)
        {
            const bool visible = index < static_cast<int>(snapshot.contacts.size());
            if (visible)
            {
                lv_obj_clear_flag(contact_rows_[index], LV_OBJ_FLAG_HIDDEN);
                const auto& contact = snapshot.contacts[index];
                setLabel(contact_name_labels_[index], contact.name);
                setLabel(contact_meta_labels_[index],
                         contact.node_id + " / " + contact.protocol + " / " +
                             contact.status);
            }
            else
            {
                lv_obj_add_flag(contact_rows_[index], LV_OBJ_FLAG_HIDDEN);
            }
        }

        refreshChatWorkspace(force);
    }

    void refreshChatWorkspace(bool force)
    {
        if (chat_panel_ == nullptr) return;
        if (!force && active_section_ != Section::Chat) return;

        const ChatWorkspaceSnapshot snapshot =
            chat_model_.snapshot(kChatConversationRows, kChatMessageRows);

        setLabel(chat_title_label_, snapshot.active_title);
        setLabel(chat_meta_label_, snapshot.active_meta);
        setLabel(chat_status_label_,
                 snapshot.action_status.empty() ? "Ready."
                                                : snapshot.action_status);

        for (int index = 0; index < kChatConversationRows; ++index)
        {
            const bool visible =
                index < static_cast<int>(snapshot.conversations.size());
            if (!visible)
            {
                lv_obj_add_flag(chat_conversation_buttons_[index],
                                LV_OBJ_FLAG_HIDDEN);
                continue;
            }

            lv_obj_clear_flag(chat_conversation_buttons_[index],
                              LV_OBJ_FLAG_HIDDEN);
            const auto& item = snapshot.conversations[index];
            setLabel(chat_conversation_title_labels_[index], item.title);
            setLabel(chat_conversation_preview_labels_[index], item.preview);
            setLabel(chat_conversation_meta_labels_[index], item.meta);
            lv_obj_set_style_bg_color(
                chat_conversation_buttons_[index],
                color(item.active ? 0xD7E8DF : 0xF3F6F2), 0);
            lv_obj_set_style_border_width(chat_conversation_buttons_[index],
                                          item.active ? 1 : 0, 0);
            lv_obj_set_style_border_color(chat_conversation_buttons_[index],
                                          color(0x7EA48F), 0);
            lv_obj_set_style_text_color(
                chat_conversation_meta_labels_[index],
                color(item.unread > 0 ? 0xA06316 : 0x66716E), 0);
        }

        const bool has_messages = !snapshot.messages.empty();
        if (chat_empty_label_ != nullptr)
        {
            if (has_messages)
                lv_obj_add_flag(chat_empty_label_, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_clear_flag(chat_empty_label_, LV_OBJ_FLAG_HIDDEN);
        }

        for (int index = 0; index < kChatMessageRows; ++index)
        {
            const bool visible =
                index < static_cast<int>(snapshot.messages.size());
            if (!visible)
            {
                lv_obj_add_flag(chat_message_rows_[index], LV_OBJ_FLAG_HIDDEN);
                continue;
            }

            lv_obj_clear_flag(chat_message_rows_[index], LV_OBJ_FLAG_HIDDEN);
            const auto& item = snapshot.messages[index];
            setLabel(chat_message_sender_labels_[index], item.sender);
            setLabel(chat_message_text_labels_[index], item.text);
            setLabel(chat_message_meta_labels_[index], item.meta);
            lv_obj_set_style_bg_color(
                chat_message_rows_[index],
                color(item.failed ? 0xFFF0EE
                                  : (item.outgoing ? 0xEEF8F3 : 0xFFFFFF)),
                0);
            lv_obj_set_style_text_color(
                chat_message_meta_labels_[index],
                color(item.failed ? 0xA23B30 : 0x66716E), 0);
        }
    }

    linux_app::LinuxAppServices services_;
    UConsoleDashboardModel dashboard_model_;
    UConsoleChatWorkspaceModel chat_model_;
    bool initialized_ = false;
    Section active_section_ = Section::Overview;
    lv_group_t* group_ = nullptr;
    std::deque<QueuedKeyEvent> key_events_{};
    clock::time_point last_refresh_{};

    lv_obj_t* sidebar_ = nullptr;
    lv_obj_t* workspace_title_ = nullptr;
    lv_obj_t* workspace_subtitle_ = nullptr;
    lv_obj_t* top_mesh_label_ = nullptr;
    lv_obj_t* top_node_label_ = nullptr;
    lv_obj_t* top_unread_label_ = nullptr;
    lv_obj_t* metrics_panel_ = nullptr;
    lv_obj_t* conversation_panel_ = nullptr;
    lv_obj_t* chat_panel_ = nullptr;
    lv_obj_t* chat_messages_panel_ = nullptr;
    lv_obj_t* chat_title_label_ = nullptr;
    lv_obj_t* chat_meta_label_ = nullptr;
    lv_obj_t* chat_empty_label_ = nullptr;
    lv_obj_t* chat_input_ = nullptr;
    lv_obj_t* chat_send_button_ = nullptr;
    lv_obj_t* chat_status_label_ = nullptr;

    std::array<NavBinding, kNavLabels.size()> nav_bindings_{};
    std::array<lv_obj_t*, kNavLabels.size()> nav_buttons_{};
    std::array<lv_obj_t*, kMetricCount> metric_value_labels_{};
    std::array<lv_obj_t*, kConversationRows> conversation_rows_{};
    std::array<lv_obj_t*, kConversationRows> conversation_title_labels_{};
    std::array<lv_obj_t*, kConversationRows> conversation_preview_labels_{};
    std::array<lv_obj_t*, kConversationRows> conversation_meta_labels_{};
    std::array<lv_obj_t*, kCapabilityRows> capability_labels_{};
    std::array<lv_obj_t*, kContactRows> contact_rows_{};
    std::array<lv_obj_t*, kContactRows> contact_name_labels_{};
    std::array<lv_obj_t*, kContactRows> contact_meta_labels_{};
    std::array<ChatConversationBinding, kChatConversationRows>
        chat_conversation_bindings_{};
    std::array<lv_obj_t*, kChatConversationRows> chat_conversation_buttons_{};
    std::array<lv_obj_t*, kChatConversationRows>
        chat_conversation_title_labels_{};
    std::array<lv_obj_t*, kChatConversationRows>
        chat_conversation_preview_labels_{};
    std::array<lv_obj_t*, kChatConversationRows>
        chat_conversation_meta_labels_{};
    std::array<lv_obj_t*, kChatMessageRows> chat_message_rows_{};
    std::array<lv_obj_t*, kChatMessageRows> chat_message_sender_labels_{};
    std::array<lv_obj_t*, kChatMessageRows> chat_message_text_labels_{};
    std::array<lv_obj_t*, kChatMessageRows> chat_message_meta_labels_{};
};

class UConsoleLvglHost
{
  public:
    UConsoleLvglHost(UConsoleDesktopShell& shell, UConsoleShellOptions options)
        : shell_(shell),
          options_(validateOptions(options)),
          canvas_(options_.width, options_.height),
          frame_buffer_(static_cast<std::size_t>(options_.width) *
                            static_cast<std::size_t>(options_.height),
                        0)
    {
        g_lvgl_start_time = clock::now();
        lv_init();
        lv_tick_set_cb(tickNow);

        display_ = lv_display_create(options_.width, options_.height);
        if (display_ == nullptr)
        {
            throw std::runtime_error(
                "Failed to create LVGL display for uConsole shell.");
        }
        lv_display_set_default(display_);
        lv_display_set_user_data(display_, this);
        lv_display_set_color_format(display_, LV_COLOR_FORMAT_RGB565);
        lv_display_set_buffers(
            display_, frame_buffer_.data(), nullptr,
            static_cast<std::uint32_t>(frame_buffer_.size() *
                                       sizeof(std::uint16_t)),
            LV_DISPLAY_RENDER_MODE_FULL);
        lv_display_set_flush_cb(display_, flushCb);

        if (!shell_.begin())
        {
            throw std::runtime_error("Failed to begin uConsole desktop shell.");
        }

        keypad_ = lv_indev_create();
        if (keypad_ == nullptr)
        {
            throw std::runtime_error(
                "Failed to create LVGL keypad input for uConsole shell.");
        }
        lv_indev_set_type(keypad_, LV_INDEV_TYPE_KEYPAD);
        lv_indev_set_display(keypad_, display_);
        lv_indev_set_user_data(keypad_, this);
        lv_indev_set_read_cb(keypad_, readInputCb);
        if (shell_.inputGroup() != nullptr)
        {
            lv_indev_set_group(keypad_, shell_.inputGroup());
        }

        tick();
    }

    ~UConsoleLvglHost()
    {
        if (keypad_ != nullptr)
        {
            lv_indev_delete(keypad_);
            keypad_ = nullptr;
        }
        shell_.releaseLvglObjects();
        if (display_ != nullptr)
        {
            lv_display_delete(display_);
            display_ = nullptr;
        }
        lv_deinit();
    }

    void tick()
    {
        shell_.tick();
        lv_timer_handler();
        if (dirty_)
        {
            copyFrameBufferToCanvas();
            dirty_ = false;
        }
    }

    [[nodiscard]] const Canvas& canvas() const noexcept
    {
        return canvas_;
    }

    static void flushCb(lv_display_t* display,
                        const lv_area_t* /*area*/,
                        std::uint8_t* /*px_map*/)
    {
        auto* host =
            static_cast<UConsoleLvglHost*>(lv_display_get_user_data(display));
        if (host != nullptr) host->dirty_ = true;
        lv_display_flush_ready(display);
    }

    static void readInputCb(lv_indev_t* indev, lv_indev_data_t* data)
    {
        auto* host =
            static_cast<UConsoleLvglHost*>(lv_indev_get_user_data(indev));
        if (host == nullptr || data == nullptr) return;

        data->state = LV_INDEV_STATE_RELEASED;
        data->key = 0U;

        std::uint32_t key = 0U;
        lv_indev_state_t state = LV_INDEV_STATE_RELEASED;
        if (!host->shell_.dequeueKeyEvent(&key, &state)) return;

        data->state = state;
        data->key = key;
        data->continue_reading = host->shell_.hasPendingKeyEvent();
    }

  private:
    void copyFrameBufferToCanvas()
    {
        for (int y = 0; y < options_.height; ++y)
        {
            for (int x = 0; x < options_.width; ++x)
            {
                const auto index =
                    static_cast<std::size_t>((y * options_.width) + x);
                canvas_.setPixel(x, y, rgb565ToColor(frame_buffer_[index]));
            }
        }
    }

    UConsoleDesktopShell& shell_;
    UConsoleShellOptions options_{};
    lv_display_t* display_ = nullptr;
    lv_indev_t* keypad_ = nullptr;
    Canvas canvas_;
    std::vector<std::uint16_t> frame_buffer_{};
    bool dirty_ = true;
};

} // namespace

void runUConsoleShell(::trailmate::cardputer_zero::platform::SurfacePresenter& presenter,
                      UConsoleShellOptions options)
{
    options = validateOptions(options);
    UConsoleDesktopShell shell;
    UConsoleLvglHost host{shell, options};

    auto next_frame = clock::now();
    const auto frame_time = std::chrono::milliseconds(options.frame_time_ms);

    while (presenter.pump())
    {
        shell.enqueueInputs(presenter.drainInput());
        host.tick();
        presenter.present(host.canvas());

        next_frame += frame_time;
        std::this_thread::sleep_until(next_frame);

        if (clock::now() > next_frame + std::chrono::milliseconds(250))
        {
            next_frame = clock::now();
        }
    }
}

} // namespace trailmate::uconsole
