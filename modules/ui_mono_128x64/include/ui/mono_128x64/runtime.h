#pragma once

#include "app/app_facades.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/gps_runtime.h"

#include <cstddef>
#include <cstdint>
#include <ctime>

namespace ui::mono_128x64
{

enum class FontSize : uint8_t
{
    Small = 1,
    Large = 2,
};

class MonoDisplay
{
  public:
    virtual ~MonoDisplay() = default;

    virtual bool begin() = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual int charWidth(FontSize size) const = 0;
    virtual int lineHeight(FontSize size) const = 0;
    virtual void clear() = 0;
    virtual void drawText(int x, int y, const char* text, FontSize size, bool inverse = false) = 0;
    virtual void drawHLine(int x, int y, int w) = 0;
    virtual void fillRect(int x, int y, int w, int h, bool on) = 0;
    virtual void present() = 0;
};

enum class InputAction : uint8_t
{
    None = 0,
    Up,
    Down,
    Left,
    Right,
    Select,
    Back,
    Primary,
    Secondary,
};

struct HostCallbacks
{
    app::IAppFacade* app = nullptr;
    uint32_t (*millis_fn)() = nullptr;
    time_t (*utc_now_fn)() = nullptr;
    int (*timezone_offset_min_fn)() = nullptr;
    void (*set_timezone_offset_min_fn)(int offset_min) = nullptr;
    uint32_t (*active_lora_frequency_hz_fn)() = nullptr;
    bool (*format_frequency_fn)(uint32_t freq_hz, char* out, size_t out_len) = nullptr;
    platform::ui::device::BatteryInfo (*battery_info_fn)() = nullptr;
    platform::ui::gps::GpsState (*gps_data_fn)() = nullptr;
    bool (*gps_enabled_fn)() = nullptr;
    bool (*gps_powered_fn)() = nullptr;
};

class Runtime
{
  public:
    Runtime(MonoDisplay& display, const HostCallbacks& host);

    bool begin();
    void appendBootLog(const char* line);
    void tick(InputAction action);

  private:
    enum class Page : uint8_t
    {
        BootLog = 0,
        Screensaver,
        MainMenu,
        ChatList,
        Conversation,
        Compose,
        SettingsMenu,
        IdentitySettings,
        RadioSettings,
        DeviceSettings,
        GnssPage,
        ActionPage,
    };

    enum class EditTarget : uint8_t
    {
        None = 0,
        Message,
        UserName,
        ShortName,
        MeshtasticPsk,
        MeshCoreChannelName,
    };

    void handleInput(InputAction action);
    void render();

    void renderBootLog();
    void renderScreensaver();
    void renderMainMenu();
    void renderChatList();
    void renderConversation();
    void renderCompose();
    void renderSettingsMenu();
    void renderIdentitySettings();
    void renderRadioSettings();
    void renderDeviceSettings();
    void renderGnssPage();
    void renderActionPage();

    void enterPage(Page page);
    void openCompose(EditTarget target, const char* seed_text = nullptr);
    void finishTextEdit(bool accept);
    void rebuildConversationList();
    void rebuildMessages();
    void sendComposeMessage();
    void commitConfig();
    void ensureBootExit();
    void adjustRadioSetting(int delta);
    void adjustDeviceSetting(int delta);
    void adjustComposeSelection(int delta);
    void addComposeChar();
    void removeComposeChar();
    void saveEditedTextToConfig();
    void formatTime(char* out_time, size_t out_len, char* out_date, size_t date_len) const;
    void formatProtocol(char* out, size_t out_len) const;
    void formatNodeLabel(char* out, size_t out_len) const;
    void drawTitleBar(const char* left, const char* right);
    void drawMenuList(const char* title, const char* const* items, size_t count, size_t selected);
    void drawFooterHint(const char* hint);
    void drawTextClipped(int x, int y, int w, const char* text, FontSize size, bool inverse = false);
    bool editUsesHexCharset() const;

    uint32_t nowMs() const;
    app::IAppFacade* app() const;

    MonoDisplay& display_;
    HostCallbacks host_{};
    bool initialized_ = false;
    Page page_ = Page::BootLog;
    Page page_before_compose_ = Page::MainMenu;
    uint32_t boot_started_ms_ = 0;
    uint32_t page_entered_ms_ = 0;
    static constexpr size_t kBootLogLines = 8;
    static constexpr size_t kBootLogWidth = 32;
    char boot_log_[kBootLogLines][kBootLogWidth] = {};
    size_t boot_log_count_ = 0;

    size_t main_menu_index_ = 0;
    size_t settings_menu_index_ = 0;
    size_t identity_index_ = 0;
    size_t radio_index_ = 0;
    size_t device_index_ = 0;
    size_t action_index_ = 0;
    size_t chat_list_index_ = 0;
    size_t message_index_ = 0;

    static constexpr size_t kMaxConversationItems = 8;
    chat::ConversationMeta conversations_[kMaxConversationItems]{};
    size_t conversation_count_ = 0;
    size_t conversation_total_ = 0;

    static constexpr size_t kMaxMessageItems = 12;
    chat::ChatMessage messages_[kMaxMessageItems]{};
    size_t message_count_ = 0;
    chat::ConversationId active_conversation_{};

    EditTarget edit_target_ = EditTarget::None;
    static constexpr size_t kComposeMax = 64;
    char compose_buffer_[kComposeMax] = {};
    size_t compose_len_ = 0;
    size_t compose_charset_index_ = 0;
};

} // namespace ui::mono_128x64
