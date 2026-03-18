#include "ui/mono_128x64/runtime.h"

#include "app/app_config.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/infra/meshcore/mc_region_presets.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "chat/runtime/self_identity_policy.h"
#include "chat/usecase/chat_service.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace ui::mono_128x64
{
namespace
{

constexpr const char* kMainMenuItems[] = {
    "Chats",
    "New Message",
    "Settings",
    "Identity",
    "Radio",
    "Device",
    "GNSS",
    "Actions",
};

constexpr const char* kSettingsMenuItems[] = {
    "Identity",
    "Radio",
    "Device",
};

constexpr const char* kIdentityItems[] = {
    "User Name",
    "Short Name",
};

constexpr const char* kRadioItems[] = {
    "Protocol",
    "TX Power",
    "Region",
    "Preset",
    "Channel",
    "Encrypt",
    "PSK/Name",
};

constexpr const char* kDeviceItems[] = {
    "BLE",
    "Time Zone",
    "GPS",
    "GPS Interval",
    "Chat Channel",
};

constexpr const char* kActionItems[] = {
    "Broadcast ID",
    "Clear Nodes",
    "Clear Msgs",
    "Reset Radio",
};

constexpr const char* kWeekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
constexpr const char* kComposeCharset =
    " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.,!?-_/:#@+*=()[]";
constexpr const char* kHexCharset = "0123456789ABCDEF";
constexpr uint32_t kBootMinMs = 1800;
constexpr int kTimezoneMin = -12 * 60;
constexpr int kTimezoneMax = 14 * 60;
constexpr int kTimezoneStep = 60;

template <typename T, size_t N>
constexpr size_t arrayCount(const T (&)[N])
{
    return N;
}

template <typename T>
constexpr T clampValue(T value, T low, T high)
{
    return value < low ? low : (value > high ? high : value);
}

template <size_t N>
void copyText(char (&dst)[N], const char* src)
{
    if (!src)
    {
        dst[0] = '\0';
        return;
    }
    std::strncpy(dst, src, N - 1);
    dst[N - 1] = '\0';
}

void appendChar(char* buffer, size_t capacity, size_t& len, char ch)
{
    if (!buffer || capacity == 0 || len + 1 >= capacity)
    {
        return;
    }
    buffer[len++] = ch;
    buffer[len] = '\0';
}

void popChar(char* buffer, size_t& len)
{
    if (!buffer || len == 0)
    {
        return;
    }
    --len;
    buffer[len] = '\0';
}

const char* protocolShortLabel(chat::MeshProtocol protocol)
{
    return protocol == chat::MeshProtocol::MeshCore ? "mc" : "mt";
}

bool encryptEnabled(const app::AppConfig& config)
{
    return config.privacy_encrypt_mode != 0;
}

void setEncryptEnabled(app::AppConfig& config, bool enabled)
{
    config.privacy_encrypt_mode = enabled ? 1 : 0;
}

void bytesToHex(const uint8_t* data, size_t len, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }

    out[0] = '\0';
    if (!data || len == 0)
    {
        return;
    }

    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 2 < out_len; ++i)
    {
        const int written = std::snprintf(out + pos, out_len - pos, "%02X", static_cast<unsigned>(data[i]));
        if (written <= 0)
        {
            break;
        }
        pos += static_cast<size_t>(written);
    }
}

bool hexToBytes(const char* hex, uint8_t* out, size_t out_len)
{
    if (!hex || !out)
    {
        return false;
    }

    const size_t hex_len = std::strlen(hex);
    if (hex_len != out_len * 2U)
    {
        return false;
    }

    for (size_t i = 0; i < out_len; ++i)
    {
        char part[3] = {hex[i * 2U], hex[i * 2U + 1U], '\0'};
        char* end = nullptr;
        const long value = std::strtol(part, &end, 16);
        if (!end || *end != '\0' || value < 0 || value > 255)
        {
            return false;
        }
        out[i] = static_cast<uint8_t>(value);
    }
    return true;
}

} // namespace

Runtime::Runtime(MonoDisplay& display, const HostCallbacks& host)
    : display_(display),
      host_(host)
{
}

bool Runtime::begin()
{
    if (initialized_)
    {
        return true;
    }
    initialized_ = display_.begin();
    boot_started_ms_ = nowMs();
    page_entered_ms_ = boot_started_ms_;
    return initialized_;
}

void Runtime::appendBootLog(const char* line)
{
    if (!line || line[0] == '\0')
    {
        return;
    }

    if (boot_log_count_ < kBootLogLines)
    {
        copyText(boot_log_[boot_log_count_], line);
        ++boot_log_count_;
        return;
    }

    for (size_t i = 1; i < kBootLogLines; ++i)
    {
        std::memcpy(boot_log_[i - 1], boot_log_[i], sizeof(boot_log_[i - 1]));
    }
    copyText(boot_log_[kBootLogLines - 1], line);
}

void Runtime::tick(InputAction action)
{
    if (!begin())
    {
        return;
    }

    ensureBootExit();
    handleInput(action);
    render();
}

void Runtime::handleInput(InputAction action)
{
    if (action == InputAction::None)
    {
        return;
    }

    if (page_ == Page::BootLog)
    {
        if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            enterPage(Page::MainMenu);
        }
        return;
    }

    if (page_ == Page::Screensaver)
    {
        if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            enterPage(Page::MainMenu);
        }
        return;
    }

    switch (page_)
    {
    case Page::MainMenu:
        if (action == InputAction::Up && main_menu_index_ > 0)
        {
            --main_menu_index_;
        }
        else if (action == InputAction::Down && main_menu_index_ + 1 < arrayCount(kMainMenuItems))
        {
            ++main_menu_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::Screensaver);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            switch (main_menu_index_)
            {
            case 0: enterPage(Page::ChatList); break;
            case 1:
                active_conversation_ = chat::ConversationId(chat::ChannelId::PRIMARY, 0, app()->getConfig().mesh_protocol);
                openCompose(EditTarget::Message);
                break;
            case 2: enterPage(Page::SettingsMenu); break;
            case 3: enterPage(Page::IdentitySettings); break;
            case 4: enterPage(Page::RadioSettings); break;
            case 5: enterPage(Page::DeviceSettings); break;
            case 6: enterPage(Page::GnssPage); break;
            case 7: enterPage(Page::ActionPage); break;
            default: break;
            }
        }
        break;

    case Page::ChatList:
        if (action == InputAction::Up && chat_list_index_ > 0)
        {
            --chat_list_index_;
        }
        else if (action == InputAction::Down && chat_list_index_ + 1 < conversation_count_)
        {
            ++chat_list_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::MainMenu);
        }
        else if ((action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary) &&
                 chat_list_index_ < conversation_count_)
        {
            active_conversation_ = conversations_[chat_list_index_].id;
            enterPage(Page::Conversation);
        }
        break;

    case Page::Conversation:
        if (action == InputAction::Up && message_index_ > 0)
        {
            --message_index_;
        }
        else if (action == InputAction::Down && message_index_ + 1 < message_count_)
        {
            ++message_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::ChatList);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            openCompose(EditTarget::Message);
        }
        break;

    case Page::Compose:
        if (action == InputAction::Up)
        {
            adjustComposeSelection(-1);
        }
        else if (action == InputAction::Down)
        {
            adjustComposeSelection(1);
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            if (compose_len_ > 0)
            {
                removeComposeChar();
            }
            else
            {
                finishTextEdit(false);
            }
        }
        else if (action == InputAction::Right || action == InputAction::Primary)
        {
            addComposeChar();
        }
        else if (action == InputAction::Secondary)
        {
            appendChar(compose_buffer_, sizeof(compose_buffer_), compose_len_, ' ');
        }
        else if (action == InputAction::Select)
        {
            if (edit_target_ == EditTarget::Message)
            {
                sendComposeMessage();
            }
            else
            {
                finishTextEdit(true);
            }
        }
        break;

    case Page::SettingsMenu:
        if (action == InputAction::Up && settings_menu_index_ > 0)
        {
            --settings_menu_index_;
        }
        else if (action == InputAction::Down && settings_menu_index_ + 1 < arrayCount(kSettingsMenuItems))
        {
            ++settings_menu_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::MainMenu);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            switch (settings_menu_index_)
            {
            case 0: enterPage(Page::IdentitySettings); break;
            case 1: enterPage(Page::RadioSettings); break;
            case 2: enterPage(Page::DeviceSettings); break;
            default: break;
            }
        }
        break;

    case Page::IdentitySettings:
        if (action == InputAction::Up && identity_index_ > 0)
        {
            --identity_index_;
        }
        else if (action == InputAction::Down && identity_index_ + 1 < arrayCount(kIdentityItems))
        {
            ++identity_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::MainMenu);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            if (identity_index_ == 0)
            {
                openCompose(EditTarget::UserName, app()->getConfig().node_name);
            }
            else
            {
                openCompose(EditTarget::ShortName, app()->getConfig().short_name);
            }
        }
        break;

    case Page::RadioSettings:
        if (action == InputAction::Up && radio_index_ > 0)
        {
            --radio_index_;
        }
        else if (action == InputAction::Down && radio_index_ + 1 < arrayCount(kRadioItems))
        {
            ++radio_index_;
        }
        else if (action == InputAction::Left)
        {
            adjustRadioSetting(-1);
        }
        else if (action == InputAction::Right)
        {
            adjustRadioSetting(1);
        }
        else if (action == InputAction::Back)
        {
            enterPage(Page::MainMenu);
        }
        else if (action == InputAction::Select || action == InputAction::Primary)
        {
            if (radio_index_ == 6)
            {
                const auto protocol = app()->getConfig().mesh_protocol;
                if (protocol == chat::MeshProtocol::Meshtastic)
                {
                    char hex[33] = {};
                    bytesToHex(app()->getConfig().meshtastic_config.secondary_key, 16, hex, sizeof(hex));
                    openCompose(EditTarget::MeshtasticPsk, hex);
                }
                else
                {
                    openCompose(EditTarget::MeshCoreChannelName, app()->getConfig().meshcore_config.meshcore_channel_name);
                }
            }
            else
            {
                adjustRadioSetting(1);
            }
        }
        break;

    case Page::DeviceSettings:
        if (action == InputAction::Up && device_index_ > 0)
        {
            --device_index_;
        }
        else if (action == InputAction::Down && device_index_ + 1 < arrayCount(kDeviceItems))
        {
            ++device_index_;
        }
        else if (action == InputAction::Left)
        {
            adjustDeviceSetting(-1);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            adjustDeviceSetting(1);
        }
        else if (action == InputAction::Back)
        {
            enterPage(Page::MainMenu);
        }
        break;

    case Page::GnssPage:
        if (action == InputAction::Left || action == InputAction::Back || action == InputAction::Select)
        {
            enterPage(Page::MainMenu);
        }
        break;

    case Page::ActionPage:
        if (action == InputAction::Up && action_index_ > 0)
        {
            --action_index_;
        }
        else if (action == InputAction::Down && action_index_ + 1 < arrayCount(kActionItems))
        {
            ++action_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::MainMenu);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            switch (action_index_)
            {
            case 0:
                app()->broadcastNodeInfo();
                appendBootLog("nodeinfo tx");
                break;
            case 1:
                app()->clearNodeDb();
                appendBootLog("nodes cleared");
                break;
            case 2:
                app()->clearMessageDb();
                appendBootLog("messages cleared");
                break;
            case 3:
                if (auto* ble_app = static_cast<app::IAppBleFacade*>(app()))
                {
                    ble_app->resetMeshConfig();
                    appendBootLog("radio reset");
                }
                break;
            default:
                break;
            }
        }
        break;

    default:
        break;
    }
}

void Runtime::render()
{
    display_.clear();
    switch (page_)
    {
    case Page::BootLog: renderBootLog(); break;
    case Page::Screensaver: renderScreensaver(); break;
    case Page::MainMenu: renderMainMenu(); break;
    case Page::ChatList: renderChatList(); break;
    case Page::Conversation: renderConversation(); break;
    case Page::Compose: renderCompose(); break;
    case Page::SettingsMenu: renderSettingsMenu(); break;
    case Page::IdentitySettings: renderIdentitySettings(); break;
    case Page::RadioSettings: renderRadioSettings(); break;
    case Page::DeviceSettings: renderDeviceSettings(); break;
    case Page::GnssPage: renderGnssPage(); break;
    case Page::ActionPage: renderActionPage(); break;
    default: renderScreensaver(); break;
    }
    display_.present();
}

void Runtime::renderBootLog()
{
    drawTitleBar("boot", nullptr);
    const int line_h = display_.lineHeight(FontSize::Small);
    const int start_y = 10;
    const size_t visible = std::min(boot_log_count_, static_cast<size_t>(6));
    for (size_t i = 0; i < visible; ++i)
    {
        drawTextClipped(0, start_y + static_cast<int>(i * line_h), display_.width(), boot_log_[boot_log_count_ - visible + i], FontSize::Small);
    }
}

void Runtime::renderScreensaver()
{
    char protocol[8] = {};
    char freq[20] = {};
    char time_buf[16] = {};
    char date_buf[24] = {};
    char node_buf[12] = {};
    formatProtocol(protocol, sizeof(protocol));
    formatNodeLabel(node_buf, sizeof(node_buf));
    if (host_.format_frequency_fn)
    {
        host_.format_frequency_fn(host_.active_lora_frequency_hz_fn ? host_.active_lora_frequency_hz_fn() : 0U,
                                  freq,
                                  sizeof(freq));
    }
    formatTime(time_buf, sizeof(time_buf), date_buf, sizeof(date_buf));

    drawTitleBar(protocol, freq[0] != '\0' ? freq : nullptr);

    const int time_w = static_cast<int>(std::strlen(time_buf)) * display_.charWidth(FontSize::Large);
    const int time_x = std::max(0, (display_.width() - time_w) / 2);
    display_.drawText(time_x, 18, time_buf, FontSize::Large);

    const int date_w = static_cast<int>(std::strlen(date_buf)) * display_.charWidth(FontSize::Small);
    const int date_x = std::max(0, (display_.width() - date_w) / 2);
    display_.drawText(date_x, 40, date_buf, FontSize::Small);

    const int node_w = static_cast<int>(std::strlen(node_buf)) * display_.charWidth(FontSize::Small);
    const int node_x = std::max(0, (display_.width() - node_w) / 2);
    display_.drawText(node_x, 54, node_buf, FontSize::Small);
}

void Runtime::renderMainMenu()
{
    drawMenuList("menu", kMainMenuItems, arrayCount(kMainMenuItems), main_menu_index_);
    drawFooterHint("< saver   ok >");
}

void Runtime::renderChatList()
{
    rebuildConversationList();
    drawTitleBar("chats", nullptr);
    if (conversation_count_ == 0)
    {
        display_.drawText(0, 18, "No conversations", FontSize::Small);
        drawFooterHint("< back   new >");
        return;
    }

    const int line_h = display_.lineHeight(FontSize::Small);
    for (size_t i = 0; i < conversation_count_ && i < 6; ++i)
    {
        const bool selected = (i == chat_list_index_);
        char line[32] = {};
        const auto& conv = conversations_[i];
        std::snprintf(line, sizeof(line), "%s%s",
                      conv.unread > 0 ? "*" : "",
                      conv.name.c_str());
        drawTextClipped(0, 10 + static_cast<int>(i * line_h), display_.width(), line, FontSize::Small, selected);
    }
    drawFooterHint("< back   open >");
}

void Runtime::renderConversation()
{
    rebuildMessages();
    char title[20] = {};
    if (active_conversation_.peer == 0)
    {
        copyText(title, "broadcast");
    }
    else
    {
        std::snprintf(title, sizeof(title), "%08lX", static_cast<unsigned long>(active_conversation_.peer));
    }
    drawTitleBar(title, nullptr);

    if (message_count_ == 0)
    {
        display_.drawText(0, 18, "No messages", FontSize::Small);
        drawFooterHint("< back   reply >");
        return;
    }

    const int line_h = display_.lineHeight(FontSize::Small);
    const size_t visible = std::min(message_count_, static_cast<size_t>(5));
    for (size_t i = 0; i < visible; ++i)
    {
        const auto& msg = messages_[message_count_ - visible + i];
        char line[40] = {};
        const char prefix = (msg.from == 0) ? '>' : '<';
        std::snprintf(line, sizeof(line), "%c %s", prefix, msg.text.c_str());
        drawTextClipped(0, 10 + static_cast<int>(i * line_h), display_.width(), line, FontSize::Small, i == visible - 1);
    }
    drawFooterHint("< back   reply >");
}

void Runtime::renderCompose()
{
    drawTitleBar(edit_target_ == EditTarget::Message ? "compose" : "edit", nullptr);
    drawTextClipped(0, 12, display_.width(), compose_buffer_, FontSize::Small);

    const char* charset = editUsesHexCharset() ? kHexCharset : kComposeCharset;
    const size_t charset_len = std::strlen(charset);
    const char current = charset[compose_charset_index_ % charset_len];
    char pick[8] = {};
    std::snprintf(pick, sizeof(pick), "[%c]", current);
    display_.drawText(0, 34, pick, FontSize::Large);

    display_.drawText(40, 34, "U/D pick", FontSize::Small);
    display_.drawText(40, 44, "R add", FontSize::Small);
    display_.drawText(40, 54, "L del OK", FontSize::Small);
}

void Runtime::renderSettingsMenu()
{
    drawMenuList("settings", kSettingsMenuItems, arrayCount(kSettingsMenuItems), settings_menu_index_);
}

void Runtime::renderIdentitySettings()
{
    drawTitleBar("identity", nullptr);
    char value[40] = {};
    for (size_t i = 0; i < arrayCount(kIdentityItems); ++i)
    {
        if (i == 0)
        {
            copyText(value, app()->getConfig().node_name);
        }
        else
        {
            copyText(value, app()->getConfig().short_name);
        }

        char line[48] = {};
        std::snprintf(line, sizeof(line), "%s: %s", kIdentityItems[i], value[0] ? value : "-");
        drawTextClipped(0, 10 + static_cast<int>(i * display_.lineHeight(FontSize::Small)),
                        display_.width(), line, FontSize::Small, i == identity_index_);
    }
    drawFooterHint("< back   edit >");
}

void Runtime::renderRadioSettings()
{
    drawTitleBar("radio", protocolShortLabel(app()->getConfig().mesh_protocol));
    char value[40] = {};
    auto& cfg = app()->getConfig();
    for (size_t i = 0; i < arrayCount(kRadioItems); ++i)
    {
        value[0] = '\0';
        switch (i)
        {
        case 0:
            copyText(value, cfg.mesh_protocol == chat::MeshProtocol::MeshCore ? "MeshCore" : "Meshtastic");
            break;
        case 1:
            std::snprintf(value, sizeof(value), "%ddBm", static_cast<int>(cfg.activeMeshConfig().tx_power));
            break;
        case 2:
            if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
            {
                if (const auto* region = chat::meshtastic::findRegion(
                        static_cast<meshtastic_Config_LoRaConfig_RegionCode>(cfg.meshtastic_config.region)))
                {
                    copyText(value, region->label);
                }
            }
            else if (const auto* preset = chat::meshcore::findRegionPresetById(cfg.meshcore_config.meshcore_region_preset))
            {
                copyText(value, preset->title);
            }
            else
            {
                copyText(value, "Custom");
            }
            break;
        case 3:
            if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
            {
                copyText(value,
                         chat::meshtastic::presetDisplayName(
                             static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(cfg.meshtastic_config.modem_preset)));
            }
            else
            {
                std::snprintf(value, sizeof(value), "%.3f/%.0f",
                              static_cast<double>(cfg.meshcore_config.meshcore_freq_mhz),
                              static_cast<double>(cfg.meshcore_config.meshcore_bw_khz));
            }
            break;
        case 4:
            if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
            {
                std::snprintf(value, sizeof(value), "Slot %u", static_cast<unsigned>(cfg.meshtastic_config.channel_num));
            }
            else
            {
                std::snprintf(value, sizeof(value), "%s/%u",
                              cfg.meshcore_config.meshcore_channel_name,
                              static_cast<unsigned>(cfg.meshcore_config.meshcore_channel_slot));
            }
            break;
        case 5:
            copyText(value, encryptEnabled(cfg) ? "On" : "Off");
            break;
        case 6:
            if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
            {
                char hex[33] = {};
                bytesToHex(cfg.meshtastic_config.secondary_key, 16, hex, sizeof(hex));
                copyText(value, hex[0] ? hex : "0000...");
            }
            else
            {
                copyText(value, cfg.meshcore_config.meshcore_channel_name);
            }
            break;
        }

        char line[48] = {};
        std::snprintf(line, sizeof(line), "%s: %s", kRadioItems[i], value);
        drawTextClipped(0, 10 + static_cast<int>(i * display_.lineHeight(FontSize::Small)),
                        display_.width(), line, FontSize::Small, i == radio_index_);
    }
    drawFooterHint("L/R adj  OK edit");
}

void Runtime::renderDeviceSettings()
{
    drawTitleBar("device", nullptr);
    char line[48] = {};
    for (size_t i = 0; i < arrayCount(kDeviceItems); ++i)
    {
        if (i == 0)
        {
            std::snprintf(line, sizeof(line), "BLE: %s", app()->isBleEnabled() ? "On" : "Off");
        }
        else if (i == 1)
        {
            const int tz = host_.timezone_offset_min_fn ? host_.timezone_offset_min_fn() : 0;
            std::snprintf(line, sizeof(line), "Time Zone: UTC%+d", tz / 60);
        }
        else if (i == 2)
        {
            std::snprintf(line, sizeof(line), "GPS: %s", app()->getConfig().gps_mode != 0 ? "On" : "Off");
        }
        else if (i == 3)
        {
            std::snprintf(line, sizeof(line), "GPS Int: %lus",
                          static_cast<unsigned long>(app()->getConfig().gps_interval_ms / 1000UL));
        }
        else
        {
            std::snprintf(line, sizeof(line), "Chat Ch: %s",
                          app()->getConfig().chat_channel == 0 ? "Primary" : "Secondary");
        }
        drawTextClipped(0, 10 + static_cast<int>(i * display_.lineHeight(FontSize::Small)),
                        display_.width(), line, FontSize::Small, i == device_index_);
    }
    drawFooterHint("L/R toggle");
}

void Runtime::renderGnssPage()
{
    drawTitleBar("gnss", nullptr);
    const auto state = host_.gps_data_fn ? host_.gps_data_fn() : platform::ui::gps::GpsState{};
    char line[40] = {};
    std::snprintf(line, sizeof(line), "Enabled: %s", (host_.gps_enabled_fn && host_.gps_enabled_fn()) ? "yes" : "no");
    display_.drawText(0, 12, line, FontSize::Small);
    std::snprintf(line, sizeof(line), "Powered: %s", (host_.gps_powered_fn && host_.gps_powered_fn()) ? "yes" : "no");
    display_.drawText(0, 22, line, FontSize::Small);
    std::snprintf(line, sizeof(line), "Fix: %s", state.valid ? "yes" : "no");
    display_.drawText(0, 32, line, FontSize::Small);
    if (state.valid)
    {
        std::snprintf(line, sizeof(line), "Lat %.4f", state.lat);
        display_.drawText(0, 42, line, FontSize::Small);
        std::snprintf(line, sizeof(line), "Lng %.4f", state.lng);
        display_.drawText(0, 52, line, FontSize::Small);
    }
}

void Runtime::renderActionPage()
{
    drawMenuList("actions", kActionItems, arrayCount(kActionItems), action_index_);
}

void Runtime::enterPage(Page page)
{
    page_ = page;
    page_entered_ms_ = nowMs();
    if (page == Page::ChatList)
    {
        rebuildConversationList();
        chat_list_index_ = std::min(chat_list_index_, conversation_count_ == 0 ? 0U : conversation_count_ - 1U);
    }
    else if (page == Page::Conversation)
    {
        if (app())
        {
            app()->getChatService().markConversationRead(active_conversation_);
        }
        rebuildMessages();
    }
}

void Runtime::openCompose(EditTarget target, const char* seed_text)
{
    edit_target_ = target;
    page_before_compose_ = page_;
    compose_buffer_[0] = '\0';
    compose_len_ = 0;
    compose_charset_index_ = 0;
    if (seed_text)
    {
        copyText(compose_buffer_, seed_text);
        compose_len_ = std::strlen(compose_buffer_);
    }
    page_ = Page::Compose;
    page_entered_ms_ = nowMs();
}

void Runtime::finishTextEdit(bool accept)
{
    if (accept)
    {
        saveEditedTextToConfig();
    }
    edit_target_ = EditTarget::None;
    enterPage(page_before_compose_);
}

void Runtime::rebuildConversationList()
{
    conversation_count_ = 0;
    conversation_total_ = 0;
    if (!app())
    {
        return;
    }

    size_t total = 0;
    const auto list = app()->getChatService().getConversations(0, kMaxConversationItems, &total);
    conversation_total_ = total;
    conversation_count_ = std::min(list.size(), static_cast<size_t>(kMaxConversationItems));
    for (size_t i = 0; i < conversation_count_; ++i)
    {
        conversations_[i] = list[i];
    }
}

void Runtime::rebuildMessages()
{
    message_count_ = 0;
    if (!app())
    {
        return;
    }

    const auto list = app()->getChatService().getRecentMessages(active_conversation_, kMaxMessageItems);
    message_count_ = std::min(list.size(), static_cast<size_t>(kMaxMessageItems));
    for (size_t i = 0; i < message_count_; ++i)
    {
        messages_[i] = list[i];
    }
}

void Runtime::sendComposeMessage()
{
    if (!app() || compose_len_ == 0)
    {
        finishTextEdit(false);
        return;
    }

    app()->getChatService().sendText(active_conversation_.channel, compose_buffer_, active_conversation_.peer);
    finishTextEdit(false);
    enterPage(Page::Conversation);
}

void Runtime::commitConfig()
{
    if (!app())
    {
        return;
    }
    app()->saveConfig();
}

void Runtime::ensureBootExit()
{
    if (page_ == Page::BootLog && (nowMs() - boot_started_ms_) >= kBootMinMs)
    {
        enterPage(Page::Screensaver);
    }
}

void Runtime::adjustRadioSetting(int delta)
{
    if (!app())
    {
        return;
    }

    auto& cfg = app()->getConfig();
    switch (radio_index_)
    {
    case 0:
        app()->switchMeshProtocol(cfg.mesh_protocol == chat::MeshProtocol::Meshtastic
                                      ? chat::MeshProtocol::MeshCore
                                      : chat::MeshProtocol::Meshtastic,
                                  false);
        break;
    case 1:
        cfg.activeMeshConfig().tx_power = static_cast<int8_t>(clampValue<int>(
            static_cast<int>(cfg.activeMeshConfig().tx_power) + delta,
            static_cast<int>(app::AppConfig::kTxPowerMinDbm),
            static_cast<int>(app::AppConfig::kTxPowerMaxDbm)));
        break;
    case 2:
        if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
        {
            size_t count = 0;
            const auto* table = chat::meshtastic::getRegionTable(&count);
            if (count > 0)
            {
                size_t index = 0;
                for (size_t i = 0; i < count; ++i)
                {
                    if (table[i].code ==
                        static_cast<meshtastic_Config_LoRaConfig_RegionCode>(cfg.meshtastic_config.region))
                    {
                        index = i;
                        break;
                    }
                }
                index = static_cast<size_t>(clampValue<int>(static_cast<int>(index) + delta, 0, static_cast<int>(count) - 1));
                cfg.meshtastic_config.region = static_cast<uint8_t>(table[index].code);
            }
        }
        else
        {
            size_t count = 0;
            const auto* table = chat::meshcore::getRegionPresetTable(&count);
            if (count > 0)
            {
                int index = -1;
                for (size_t i = 0; i < count; ++i)
                {
                    if (table[i].id == cfg.meshcore_config.meshcore_region_preset)
                    {
                        index = static_cast<int>(i);
                        break;
                    }
                }
                index = clampValue(index + delta, 0, static_cast<int>(count) - 1);
                cfg.meshcore_config.meshcore_region_preset = table[index].id;
            }
        }
        break;
    case 3:
        if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
        {
            constexpr int kPresetMin = static_cast<int>(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST);
            constexpr int kPresetMax = static_cast<int>(meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO);
            cfg.meshtastic_config.modem_preset = static_cast<uint8_t>(clampValue(
                static_cast<int>(cfg.meshtastic_config.modem_preset) + delta, kPresetMin, kPresetMax));
            cfg.meshtastic_config.use_preset = true;
        }
        else
        {
            size_t count = 0;
            const auto* table = chat::meshcore::getRegionPresetTable(&count);
            if (count > 0)
            {
                int index = -1;
                for (size_t i = 0; i < count; ++i)
                {
                    if (table[i].id == cfg.meshcore_config.meshcore_region_preset)
                    {
                        index = static_cast<int>(i);
                        break;
                    }
                }
                index = clampValue(index + delta, 0, static_cast<int>(count) - 1);
                cfg.meshcore_config.meshcore_region_preset = table[index].id;
                cfg.meshcore_config.meshcore_freq_mhz = table[index].freq_mhz;
                cfg.meshcore_config.meshcore_bw_khz = table[index].bw_khz;
                cfg.meshcore_config.meshcore_sf = table[index].sf;
                cfg.meshcore_config.meshcore_cr = table[index].cr;
            }
        }
        break;
    case 4:
        if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
        {
            cfg.meshtastic_config.channel_num = static_cast<uint16_t>(clampValue<int>(
                static_cast<int>(cfg.meshtastic_config.channel_num) + delta, 0, 255));
        }
        else
        {
            cfg.meshcore_config.meshcore_channel_slot = static_cast<uint8_t>(clampValue<int>(
                static_cast<int>(cfg.meshcore_config.meshcore_channel_slot) + delta, 0, 15));
        }
        break;
    case 5:
        setEncryptEnabled(cfg, !encryptEnabled(cfg));
        break;
    default:
        break;
    }

    commitConfig();
}

void Runtime::adjustDeviceSetting(int delta)
{
    if (!app())
    {
        return;
    }

    if (device_index_ == 0)
    {
        app()->setBleEnabled(!app()->isBleEnabled());
    }
    else if (device_index_ == 1 && host_.timezone_offset_min_fn && host_.set_timezone_offset_min_fn)
    {
        const int current = host_.timezone_offset_min_fn();
        const int next = clampValue(current + delta * kTimezoneStep, kTimezoneMin, kTimezoneMax);
        host_.set_timezone_offset_min_fn(next);
    }
    else if (device_index_ == 2)
    {
        app()->getConfig().gps_mode = (app()->getConfig().gps_mode == 0) ? 1 : 0;
        commitConfig();
    }
    else if (device_index_ == 3)
    {
        static constexpr uint32_t kGpsIntervals[] = {15000UL, 30000UL, 60000UL, 300000UL, 600000UL};
        size_t index = 0;
        while (index + 1 < arrayCount(kGpsIntervals) &&
               kGpsIntervals[index] < app()->getConfig().gps_interval_ms)
        {
            ++index;
        }
        const int next = clampValue<int>(static_cast<int>(index) + delta, 0, static_cast<int>(arrayCount(kGpsIntervals)) - 1);
        app()->getConfig().gps_interval_ms = kGpsIntervals[next];
        commitConfig();
    }
    else if (device_index_ == 4)
    {
        app()->getConfig().chat_channel = app()->getConfig().chat_channel == 0 ? 1 : 0;
        commitConfig();
    }
}

void Runtime::adjustComposeSelection(int delta)
{
    const char* charset = editUsesHexCharset() ? kHexCharset : kComposeCharset;
    const size_t len = std::strlen(charset);
    if (len == 0)
    {
        compose_charset_index_ = 0;
        return;
    }
    const int next = static_cast<int>(compose_charset_index_) + delta;
    if (next < 0)
    {
        compose_charset_index_ = len - 1;
    }
    else
    {
        compose_charset_index_ = static_cast<size_t>(next) % len;
    }
}

void Runtime::addComposeChar()
{
    const char* charset = editUsesHexCharset() ? kHexCharset : kComposeCharset;
    const size_t len = std::strlen(charset);
    if (len == 0)
    {
        return;
    }
    appendChar(compose_buffer_, sizeof(compose_buffer_), compose_len_, charset[compose_charset_index_ % len]);
}

void Runtime::removeComposeChar()
{
    popChar(compose_buffer_, compose_len_);
}

void Runtime::saveEditedTextToConfig()
{
    if (!app())
    {
        return;
    }

    auto& cfg = app()->getConfig();
    switch (edit_target_)
    {
    case EditTarget::UserName:
        copyText(cfg.node_name, compose_buffer_);
        break;
    case EditTarget::ShortName:
        copyText(cfg.short_name, compose_buffer_);
        break;
    case EditTarget::MeshtasticPsk:
        (void)hexToBytes(compose_buffer_, cfg.meshtastic_config.secondary_key, 16);
        break;
    case EditTarget::MeshCoreChannelName:
        copyText(cfg.meshcore_config.meshcore_channel_name, compose_buffer_);
        break;
    default:
        break;
    }
    commitConfig();
}

void Runtime::formatTime(char* out_time, size_t out_len, char* out_date, size_t date_len) const
{
    if (out_time && out_len > 0)
    {
        out_time[0] = '\0';
    }
    if (out_date && date_len > 0)
    {
        out_date[0] = '\0';
    }

    if (!host_.utc_now_fn && !host_.millis_fn)
    {
        return;
    }

    time_t now = host_.utc_now_fn ? host_.utc_now_fn() : 0;
    const bool has_valid_wall_clock = now >= static_cast<time_t>(1700000000);

    if (has_valid_wall_clock && host_.timezone_offset_min_fn)
    {
        now += static_cast<time_t>(host_.timezone_offset_min_fn()) * 60;
    }

    if (!has_valid_wall_clock)
    {
        const uint32_t uptime_s = host_.millis_fn ? (host_.millis_fn() / 1000U) : 0U;
        const uint32_t hours = uptime_s / 3600U;
        const uint32_t minutes = (uptime_s / 60U) % 60U;
        const uint32_t seconds = uptime_s % 60U;

        if (out_time && out_len > 0)
        {
            std::snprintf(out_time, out_len, "%02lu:%02lu:%02lu",
                          static_cast<unsigned long>(hours),
                          static_cast<unsigned long>(minutes),
                          static_cast<unsigned long>(seconds));
        }
        if (out_date && date_len > 0)
        {
            std::snprintf(out_date, date_len, "TIME UNSYNC");
        }
        return;
    }

    const tm* local = gmtime(&now);
    if (!local)
    {
        return;
    }

    if (out_time && out_len > 0)
    {
        std::snprintf(out_time, out_len, "%02d:%02d:%02d", local->tm_hour, local->tm_min, local->tm_sec);
    }
    if (out_date && date_len > 0)
    {
        const char* weekday = (local->tm_wday >= 0 && local->tm_wday < 7) ? kWeekdays[local->tm_wday] : "---";
        std::snprintf(out_date, date_len, "%04d-%02d-%02d %s",
                      local->tm_year + 1900, local->tm_mon + 1, local->tm_mday, weekday);
    }
}

void Runtime::formatProtocol(char* out, size_t out_len) const
{
    if (!out || out_len == 0 || !app())
    {
        return;
    }
    std::snprintf(out, out_len, "%s", protocolShortLabel(app()->getConfig().mesh_protocol));
}

void Runtime::formatNodeLabel(char* out, size_t out_len) const
{
    if (!out || out_len == 0 || !app())
    {
        return;
    }
    chat::runtime::formatScreenNodeLabel(app()->getSelfNodeId(), out, out_len);
}

void Runtime::drawTitleBar(const char* left, const char* right)
{
    if (left && left[0] != '\0')
    {
        display_.drawText(0, 0, left, FontSize::Small);
    }
    if (right && right[0] != '\0')
    {
        const int w = static_cast<int>(std::strlen(right)) * display_.charWidth(FontSize::Small);
        display_.drawText(std::max(0, display_.width() - w), 0, right, FontSize::Small);
    }
    display_.drawHLine(0, 8, display_.width());
}

void Runtime::drawMenuList(const char* title, const char* const* items, size_t count, size_t selected)
{
    drawTitleBar(title, nullptr);
    const int line_h = display_.lineHeight(FontSize::Small);
    for (size_t i = 0; i < count && i < 5; ++i)
    {
        drawTextClipped(0, 10 + static_cast<int>(i * line_h), display_.width(), items[i], FontSize::Small, i == selected);
    }
}

void Runtime::drawFooterHint(const char* hint)
{
    if (!hint)
    {
        return;
    }
    drawTextClipped(0, 56, display_.width(), hint, FontSize::Small);
}

void Runtime::drawTextClipped(int x, int y, int w, const char* text, FontSize size, bool inverse)
{
    if (!text || w <= 0)
    {
        return;
    }

    const int cw = std::max(1, display_.charWidth(size));
    const size_t max_chars = static_cast<size_t>(std::max(1, w / cw));
    char clipped[48] = {};
    if (std::strlen(text) <= max_chars)
    {
        copyText(clipped, text);
    }
    else if (max_chars > 3)
    {
        std::strncpy(clipped, text, max_chars - 3);
        std::strcpy(clipped + (max_chars - 3), "...");
    }
    else
    {
        std::strncpy(clipped, text, max_chars);
        clipped[max_chars] = '\0';
    }
    display_.drawText(x, y, clipped, size, inverse);
}

bool Runtime::editUsesHexCharset() const
{
    return edit_target_ == EditTarget::MeshtasticPsk;
}

uint32_t Runtime::nowMs() const
{
    return host_.millis_fn ? host_.millis_fn() : 0U;
}

app::IAppFacade* Runtime::app() const
{
    return host_.app;
}

} // namespace ui::mono_128x64
