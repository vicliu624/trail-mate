#pragma once

#include "app/app_facades.h"
#include "chat/domain/contact_types.h"
#include "chat/usecase/chat_service.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/gps_runtime.h"
#include "ui/mono_128x64/text_renderer.h"

#include <cstddef>
#include <cstdint>
#include <ctime>

namespace ui::mono_128x64
{

class MonoDisplay
{
  public:
    virtual ~MonoDisplay() = default;

    virtual bool begin() = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual void clear() = 0;
    virtual void drawPixel(int x, int y, bool on) = 0;
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
    // Platform-owned mono UI font. NRF is expected to provide Fusion Pixel 8px
    // here so ASCII and CJK share one renderer and one asset boundary.
    const MonoFont* ui_font = nullptr;
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
    void (*debug_log_fn)(const char* text) = nullptr;
};

class Runtime : public chat::ChatService::IncomingTextObserver
{
  public:
    Runtime(MonoDisplay& display, const HostCallbacks& host);

    bool begin();
    void appendBootLog(const char* line);
    void tick(InputAction action);
    void bindChatObservers();
    void onIncomingText(const chat::MeshIncomingText& msg) override;

    enum class ComposeMode : uint8_t
    {
        AbcLower = 0,
        AbcUpper,
        Num,
        Sym,
    };

    enum class ComposeFocus : uint8_t
    {
        Body = 0,
        Candidate,
        Action,
    };

    enum class ComposeAction : uint8_t
    {
        Mode = 0,
        Space,
        Back,
        Delete,
        Send,
    };

  private:
    enum class Page : uint8_t
    {
        BootLog = 0,
        Screensaver,
        Sleep,
        MainMenu,
        ChatList,
        NodeList,
        NodeInfo,
        Conversation,
        MessageMenu,
        MessageInfo,
        Compose,
        SettingsMenu,
        RadioSettings,
        DeviceSettings,
        InfoPage,
        GnssPage,
        ActionPage,
    };

    enum class EditTarget : uint8_t
    {
        None = 0,
        Message,
        MeshCoreChannelName,
    };

    void handleInput(InputAction action);
    void render();

    void renderBootLog();
    void renderScreensaver();
    void renderSleep();
    void renderMainMenu();
    void renderChatList();
    void renderNodeList();
    void renderNodeInfo();
    void renderConversation();
    void renderMessageMenu();
    void renderMessageInfo();
    void renderCompose();
    void renderSettingsMenu();
    void renderRadioSettings();
    void renderDeviceSettings();
    void renderInfoPage();
    void renderGnssPage();
    void renderActionPage();

    void enterPage(Page page);
    void openCompose(EditTarget target, const char* seed_text = nullptr);
    void finishTextEdit(bool accept);
    void rebuildConversationList();
    void rebuildNodeList();
    void buildNodeInfo();
    void rebuildMessages();
    void buildMessageInfo();
    void sendComposeMessage();
    void commitConfig();
    void ensureBootExit();
    void ensureSleepTimeout(InputAction action);
    void adjustRadioSetting(int delta);
    void adjustDeviceSetting(int delta);
    void adjustComposeSelection(int delta);
    void adjustComposeCandidate(int delta);
    void adjustComposeAction(int delta);
    void addComposeChar();
    void removeComposeChar();
    void cycleComposeMode();
    void rebuildComposeCandidates();
    bool commitComposeCandidate();
    bool commitComposePreedit(bool prefer_candidate);
    void appendComposeChar(char ch);
    void appendComposeWord(const char* text);
    void activateComposeAction();
    void saveEditedTextToConfig();
    void formatTime(char* out_time, size_t out_len, char* out_date, size_t date_len) const;
    void formatTimestamp(char* out, size_t out_len, uint32_t timestamp_s) const;
    void formatProtocol(char* out, size_t out_len) const;
    void formatNodeLabel(char* out, size_t out_len) const;
    void formatComposeTarget(char* out, size_t out_len) const;
    void drawTitleBar(const char* left, const char* right);
    void drawMenuList(const char* title, const char* const* items, size_t count, size_t selected);
    void drawFooterHint(const char* hint);
    void drawTextClipped(int x, int y, int w, const char* text, bool inverse = false);
    bool editUsesHexCharset() const;
    bool usesSmartCompose() const;

    uint32_t nowMs() const;
    app::IAppFacade* app() const;
    const chat::ChatMessage* selectedMessage() const;

    MonoDisplay& display_;
    TextRenderer text_renderer_;
    HostCallbacks host_{};
    bool initialized_ = false;
    Page page_ = Page::BootLog;
    Page page_before_compose_ = Page::MainMenu;
    Page page_before_sleep_ = Page::Screensaver;
    uint32_t boot_started_ms_ = 0;
    uint32_t page_entered_ms_ = 0;
    uint32_t last_interaction_ms_ = 0;
    static constexpr size_t kBootLogLines = 8;
    static constexpr size_t kBootLogWidth = 32;
    char boot_log_[kBootLogLines][kBootLogWidth] = {};
    size_t boot_log_count_ = 0;

    size_t main_menu_index_ = 0;
    size_t settings_menu_index_ = 0;
    size_t radio_index_ = 0;
    size_t device_index_ = 0;
    size_t info_scroll_ = 0;
    size_t action_index_ = 0;
    size_t chat_list_index_ = 0;
    size_t node_list_index_ = 0;
    size_t node_info_scroll_ = 0;
    size_t message_index_ = 0;
    size_t message_menu_index_ = 0;
    size_t message_info_scroll_ = 0;
    size_t gnss_page_index_ = 0;

    static constexpr size_t kMaxConversationItems = 8;
    chat::ConversationMeta conversations_[kMaxConversationItems]{};
    size_t conversation_count_ = 0;
    size_t conversation_total_ = 0;

    static constexpr size_t kMaxNodeItems = 16;
    chat::contacts::NodeInfo nodes_[kMaxNodeItems]{};
    size_t node_count_ = 0;
    static constexpr size_t kNodeInfoLines = 24;
    static constexpr size_t kNodeInfoWidth = 40;
    char node_info_lines_[kNodeInfoLines][kNodeInfoWidth] = {};
    size_t node_info_count_ = 0;

    static constexpr size_t kMaxMessageItems = 12;
    chat::ChatMessage messages_[kMaxMessageItems]{};
    size_t message_count_ = 0;
    chat::ConversationId active_conversation_{};

    struct MessageMetaEntry
    {
        bool used = false;
        chat::MeshProtocol protocol = chat::MeshProtocol::Meshtastic;
        chat::ChannelId channel = chat::ChannelId::PRIMARY;
        chat::NodeId from = 0;
        chat::MessageId msg_id = 0;
        chat::NodeId to = 0;
        uint8_t hop_limit = 0xFF;
        bool encrypted = false;
        chat::RxMeta rx_meta{};
    };
    static constexpr size_t kMessageMetaCapacity = 24;
    MessageMetaEntry message_meta_[kMessageMetaCapacity]{};
    size_t message_meta_cursor_ = 0;
    bool chat_observers_bound_ = false;

    static constexpr size_t kMessageInfoLines = 18;
    static constexpr size_t kMessageInfoWidth = 40;
    char message_info_lines_[kMessageInfoLines][kMessageInfoWidth] = {};
    size_t message_info_count_ = 0;

    EditTarget edit_target_ = EditTarget::None;
    static constexpr size_t kComposeMax = 64;
    char compose_buffer_[kComposeMax] = {};
    size_t compose_len_ = 0;
    size_t compose_charset_index_ = 0;
    ComposeMode compose_mode_ = ComposeMode::AbcLower;
    ComposeFocus compose_focus_ = ComposeFocus::Body;
    size_t compose_action_index_ = 0;
    size_t compose_abc_group_index_ = 0;
    size_t compose_abc_tap_index_ = 0;
    uint32_t compose_abc_last_tap_ms_ = 0;
    int compose_abc_last_group_index_ = -1;
    static constexpr size_t kComposePreeditMax = 24;
    char compose_preedit_[kComposePreeditMax] = {};
    size_t compose_preedit_len_ = 0;
    static constexpr size_t kComposeCandidateMax = 3;
    static constexpr size_t kComposeCandidateWidth = 20;
    char compose_candidates_[kComposeCandidateMax][kComposeCandidateWidth] = {};
    size_t compose_candidate_count_ = 0;
    size_t compose_candidate_index_ = 0;
};

} // namespace ui::mono_128x64
