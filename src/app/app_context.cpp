/**
 * @file app_context.cpp
 * @brief Application context implementation
 */

#include "app_context.h"
#include "../chat/infra/protocol_factory.h"
#include "../chat/infra/meshtastic/mt_region.h"
#include "../chat/infra/meshcore/mc_region_presets.h"
#include "../gps/usecase/gps_service.h"
#include "../gps/usecase/track_recorder.h"
#include "../hostlink/hostlink_bridge_radio.h"
#include "../hostlink/hostlink_service.h"
#include "../sys/event_bus.h"
#include "../team/protocol/team_chat.h"
#include "../ui/screens/team/team_ui_store.h"
#include "../ui/ui_common.h"
#include "../ui/ui_team.h"
#include "../ui/widgets/system_notification.h"
#ifdef USING_ST25R3916
#endif
#include "app_tasks.h"
#include <SD.h>
#include <cstdio>
#include <cstring>
#if __has_include("esp_efuse.h")
#include "esp_efuse.h"
#endif

namespace app
{

#ifndef APP_EVENT_LOG_ENABLE
#define APP_EVENT_LOG_ENABLE 0
#endif

#if APP_EVENT_LOG_ENABLE
#define APP_EVENT_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define APP_EVENT_LOG(...)
#endif

namespace
{
uint8_t load_message_tone_volume()
{
    Preferences prefs;
    if (!prefs.begin("settings", true))
    {
        return 45;
    }
    int value = prefs.getInt("speaker_volume", 45);
    prefs.end();
    if (value < 0)
    {
        value = 0;
    }
    if (value > 100)
    {
        value = 100;
    }
    return static_cast<uint8_t>(value);
}

bool apply_region_pref_fallback(app::AppConfig& config, Preferences& prefs)
{
    bool has_region = false;
    bool has_mc_preset = false;
    if (prefs.begin("chat", true))
    {
        has_region = prefs.isKey("region");
        has_mc_preset = prefs.isKey("mc_region_preset");
        prefs.end();
    }

    if (has_region && has_mc_preset)
    {
        return false;
    }

    int ui_region = -1;
    int ui_mc_preset = -1;
    if (prefs.begin("settings", true))
    {
        if (!has_region)
        {
            ui_region = prefs.getInt("chat_region", -1);
        }
        if (!has_mc_preset)
        {
            ui_mc_preset = prefs.getInt("mc_region_preset", -1);
        }
        prefs.end();
    }

    bool changed = false;
    if (!has_region && ui_region > 0)
    {
        const auto* region = chat::meshtastic::findRegion(
            static_cast<meshtastic_Config_LoRaConfig_RegionCode>(ui_region));
        if (region && region->code != meshtastic_Config_LoRaConfig_RegionCode_UNSET)
        {
            config.meshtastic_config.region = static_cast<uint8_t>(ui_region);
            changed = true;
        }
    }

    if (!has_mc_preset && ui_mc_preset >= 0)
    {
        const uint8_t preset_id = static_cast<uint8_t>(ui_mc_preset);
        if (chat::meshcore::isValidRegionPresetId(preset_id))
        {
            config.meshcore_config.meshcore_region_preset = preset_id;
            if (preset_id > 0)
            {
                if (const chat::meshcore::RegionPreset* preset =
                        chat::meshcore::findRegionPresetById(preset_id))
                {
                    config.meshcore_config.meshcore_freq_mhz = preset->freq_mhz;
                    config.meshcore_config.meshcore_bw_khz = preset->bw_khz;
                    config.meshcore_config.meshcore_sf = preset->sf;
                    config.meshcore_config.meshcore_cr = preset->cr;
                }
            }
            changed = true;
        }
    }

    return changed;
}
} // namespace

bool AppContext::init(BoardBase& board, LoraBoard* lora_board, GpsBoard* gps_board, MotionBoard* motion_board,
                      bool use_mock_adapter, uint32_t disable_hw_init)
{
    // Store board reference for hardware access
    board_ = &board;
    lora_board_ = lora_board;
    gps_board_ = gps_board;
    motion_board_ = motion_board;
    board_->setMessageToneVolume(load_message_tone_volume());

    // Initialize event bus
    if (!sys::EventBus::init())
    {
        return false;
    }

    // Load configuration
    config_.load(preferences_);
    if (apply_region_pref_fallback(config_, preferences_))
    {
        config_.save(preferences_);
    }
    (void)ui_get_timezone_offset_min();

    if (gps_board_ && motion_board_)
    {
        gps::GpsService::getInstance().begin(
            *gps_board_,
            *motion_board_,
            disable_hw_init,
            config_.gps_interval_ms,
            config_.motion_config);
        gps::GpsService::getInstance().setPowerStrategy(config_.gps_strategy);
        gps::GpsService::getInstance().setGnssConfig(config_.gps_mode, config_.gps_sat_mask);
        gps::GpsService::getInstance().setNmeaConfig(config_.privacy_nmea_output,
                                                     config_.privacy_nmea_sentence);
    }

    {
        auto& recorder = gps::TrackRecorder::getInstance();
        recorder.setFormat(static_cast<gps::TrackFormat>(config_.map_track_format));
        if (config_.map_track_interval == 99)
        {
            recorder.setDistanceOnly(true);
            recorder.setIntervalSeconds(0);
        }
        else
        {
            recorder.setDistanceOnly(false);
            recorder.setIntervalSeconds(static_cast<uint32_t>(config_.map_track_interval));
        }
        if (recorder.restoreActiveSession())
        {
            Serial.printf("[Tracker] active session restored path=%s\n",
                          recorder.currentPath().c_str());
        }
        recorder.setAutoRecording(config_.map_track_enabled);
    }

    // Create domain model
    chat_model_ = std::make_unique<chat::ChatModel>();
    chat_model_->setPolicy(config_.chat_policy);

    // Create storage (prefer SD log, fallback to RAM)
    bool sd_available = (SD.cardType() != CARD_NONE);
    if (sd_available)
    {
        auto log_store = std::make_unique<chat::LogStore>();
        if (log_store->begin(SD))
        {
            Serial.printf("[AppContext] chat store=LogStore (SD)\n");
            chat_store_ = std::move(log_store);
        }
    }
    if (!chat_store_)
    {
        Serial.printf("[AppContext] chat store=RamStore\n");
        chat_store_ = std::make_unique<chat::RamStore>();
    }

    // Create mesh adapter router + selected protocol backend
    (void)use_mock_adapter;
    mesh_router_ = std::make_unique<chat::MeshAdapterRouter>();
    std::unique_ptr<chat::IMeshAdapter> adapter;
    if (lora_board_)
    {
        adapter = chat::ProtocolFactory::createAdapter(config_.mesh_protocol, *lora_board_);
    }
    if (adapter)
    {
        adapter->applyConfig(config_.activeMeshConfig());
        if (!mesh_router_->installBackend(config_.mesh_protocol, std::move(adapter)))
        {
            Serial.printf("[APP] WARNING: Failed to install mesh adapter backend\n");
        }
    }
    chat::IMeshAdapter* adapter_raw = mesh_router_.get();
    applyUserInfo();
    applyNetworkLimits();
    applyPrivacyConfig();

    // Initialize tasks for real LoRa
    if (lora_board_)
    {
        if (!app::AppTasks::init(*lora_board_, adapter_raw))
        {
            Serial.printf("[APP] WARNING: Failed to start LoRa tasks\n");
        }
        else
        {
            Serial.printf("[APP] LoRa tasks started\n");
        }
    }
    else
    {
        Serial.printf("[APP] WARNING: Board type not supported for LoRa tasks\n");
    }

    // Create chat service
    chat_service_ = std::make_unique<chat::ChatService>(
        *chat_model_, *mesh_router_, *chat_store_, config_.mesh_protocol);
    applyChatDefaults();

    // Create team service (protocol-only for now)
    team_crypto_ = std::make_unique<team::infra::TeamCrypto>();
    team_event_sink_ = std::make_unique<team::infra::TeamEventBusSink>();
    team_service_ = std::make_unique<team::TeamService>(
        *team_crypto_, *mesh_router_, *team_event_sink_);
    team_controller_ = std::make_unique<team::TeamController>(*team_service_);
    team_track_sampler_ = std::make_unique<team::TeamTrackSampler>();
    team_pairing_service_ = std::make_unique<team::TeamPairingService>();

    // Restore team keys early so tracker can resume after reboot.
    {
        team::ui::TeamUiSnapshot snap;
        if (team_controller_ && team::ui::team_ui_get_store().load(snap) &&
            snap.has_team_id && snap.has_team_psk && snap.security_round > 0)
        {
            if (team_controller_->setKeysFromPsk(snap.team_id,
                                                 snap.security_round,
                                                 snap.team_psk.data(),
                                                 snap.team_psk.size()))
            {
                Serial.printf("[Team] keys restored from store key_id=%lu\n",
                              static_cast<unsigned long>(snap.security_round));
            }
            else
            {
                Serial.printf("[Team] keys restore failed key_id=%lu\n",
                              static_cast<unsigned long>(snap.security_round));
            }
        }
    }
    gps::GpsService::getInstance().setTeamModeActive(team_service_ && team_service_->hasKeys());

    // Create contact infrastructure
    node_store_ = std::make_unique<chat::meshtastic::NodeStore>();
    contact_store_ = std::make_unique<chat::contacts::ContactStore>();

    // Create contact service with dependency injection
    contact_service_ = std::make_unique<chat::contacts::ContactService>(
        *node_store_, *contact_store_);
    contact_service_->begin();

    // Start BLE services
    ble_manager_ = std::make_unique<ble::BleManager>(*this);
    {
        // Default BLE enable state comes from "settings" NVS namespace,
        // key "ble_enabled". If not present, default to enabled.
        Preferences prefs;
        prefs.begin("settings", true);
        bool ble_enabled = prefs.getBool("ble_enabled", true);
        prefs.end();
        if (ble_enabled)
        {
            ble_manager_->setEnabled(true);
        }
    }

    return true;
}

bool AppContext::switchMeshProtocol(chat::MeshProtocol protocol, bool persist)
{
    if (!mesh_router_ || !lora_board_)
    {
        return false;
    }

    if (protocol != chat::MeshProtocol::Meshtastic &&
        protocol != chat::MeshProtocol::MeshCore)
    {
        return false;
    }

    std::unique_ptr<chat::IMeshAdapter> backend =
        chat::ProtocolFactory::createAdapter(protocol, *lora_board_);
    if (!backend)
    {
        return false;
    }

    const chat::MeshProtocol previous_protocol = config_.mesh_protocol;
    config_.mesh_protocol = protocol;

    backend->applyConfig(config_.activeMeshConfig());

    char long_name[sizeof(config_.node_name)];
    char short_name[sizeof(config_.short_name)];
    getEffectiveUserInfo(long_name, sizeof(long_name),
                         short_name, sizeof(short_name));
    backend->setUserInfo(long_name, short_name);
    backend->setNetworkLimits(config_.net_duty_cycle, config_.net_channel_util);
    backend->setPrivacyConfig(config_.privacy_encrypt_mode, config_.privacy_pki);

    if (!mesh_router_->installBackend(protocol, std::move(backend)))
    {
        config_.mesh_protocol = previous_protocol;
        return false;
    }

    if (chat_service_)
    {
        chat_service_->setActiveProtocol(protocol);
    }

    if (persist)
    {
        saveConfig();
    }
    return true;
}

void AppContext::applyPositionConfig()
{
    gps::GpsService::getInstance().setCollectionInterval(config_.gps_interval_ms);
    gps::GpsService::getInstance().setGnssConfig(config_.gps_mode, config_.gps_sat_mask);
}

void AppContext::getEffectiveUserInfo(char* out_long, size_t long_len,
                                      char* out_short, size_t short_len) const
{
    if (!out_long || long_len == 0 || !out_short || short_len == 0)
    {
        return;
    }

    out_long[0] = '\0';
    out_short[0] = '\0';

    const char* cfg_long = config_.node_name;
    const char* cfg_short = config_.short_name;
    uint16_t suffix = static_cast<uint16_t>(getSelfNodeId() & 0x0ffff);

    if (cfg_long && cfg_long[0] != '\0')
    {
        strncpy(out_long, cfg_long, long_len - 1);
        out_long[long_len - 1] = '\0';
    }
    else
    {
        snprintf(out_long, long_len, "lilygo-%04X", suffix);
    }

    if (cfg_short && cfg_short[0] != '\0')
    {
        size_t copy_len = strlen(cfg_short);
        if (copy_len > 4)
        {
            copy_len = 4;
        }
        if (copy_len > short_len - 1)
        {
            copy_len = short_len - 1;
        }
        memcpy(out_short, cfg_short, copy_len);
        out_short[copy_len] = '\0';
    }
    else
    {
        snprintf(out_short, short_len, "%04X", suffix);
    }
}

void AppContext::update()
{
    constexpr size_t kMaxEventsPerUpdate = 32;

    hostlink::process_pending_commands();

    // Update chat service (process incoming messages)
    if (chat_service_)
    {
        chat_service_->processIncoming();
    }
    if (team_service_)
    {
        team_service_->processIncoming();
    }
    if (team_pairing_service_)
    {
        team_pairing_service_->update();
    }
    bool team_active = team_service_ && team_service_->hasKeys();
    gps::GpsService::getInstance().setTeamModeActive(team_active);
    if (team_track_sampler_)
    {
        team_track_sampler_->update(team_controller_.get(), team_active);
    }
    if (ble_manager_)
    {
        ble_manager_->update();
    }

    // Update UI controller
    if (ui_controller_)
    {
        ui_controller_->update();
    }

    // Process events with a budget to avoid starving UI under burst traffic.
    sys::Event* event = nullptr;
    for (size_t processed = 0;
         processed < kMaxEventsPerUpdate && sys::EventBus::subscribe(&event, 0);)
    {
        if (!event)
        {
            continue;
        }
        ++processed;

        // Handle global events (like haptic feedback) before UI-specific handling
        switch (event->type)
        {
        case sys::EventType::ChatNewMessage:
        {
            sys::ChatNewMessageEvent* msg_event = (sys::ChatNewMessageEvent*)event;
            APP_EVENT_LOG("[AppContext::update] ChatNewMessage received: channel=%d\n", msg_event->channel);

            // Global haptic feedback on incoming messages (works regardless of UI state)
            if (board_)
            {
                APP_EVENT_LOG("[AppContext::update] Triggering haptic feedback...\n");
                board_->vibrator();
                board_->playMessageTone();
                APP_EVENT_LOG("[AppContext::update] Haptic feedback triggered\n");
            }
            else
            {
                APP_EVENT_LOG("[AppContext::update] WARNING: board_ is nullptr, cannot trigger vibration\n");
            }

            // Show system notification
            ui::SystemNotification::show(msg_event->text, 3000);
            break;
        }
        case sys::EventType::TeamChat:
        {
            sys::TeamChatEvent* team_event = (sys::TeamChatEvent*)event;
            if (board_)
            {
                board_->vibrator();
                board_->playMessageTone();
            }
            std::string notice = "Team: ";
            const auto& msg = team_event->data.msg;
            if (msg.header.type == team::proto::TeamChatType::Text)
            {
                std::string text(msg.payload.begin(), msg.payload.end());
                if (text.size() > 48)
                {
                    text = text.substr(0, 45) + "...";
                }
                notice += text;
            }
            else if (msg.header.type == team::proto::TeamChatType::Location)
            {
                team::proto::TeamChatLocation loc;
                if (team::proto::decodeTeamChatLocation(msg.payload.data(), msg.payload.size(), &loc) &&
                    !loc.label.empty())
                {
                    notice += "Location: " + loc.label;
                }
                else
                {
                    notice += "Location";
                }
            }
            else if (msg.header.type == team::proto::TeamChatType::Command)
            {
                team::proto::TeamChatCommand cmd;
                if (team::proto::decodeTeamChatCommand(msg.payload.data(), msg.payload.size(), &cmd))
                {
                    const char* name = "Command";
                    switch (cmd.cmd_type)
                    {
                    case team::proto::TeamCommandType::RallyTo:
                        name = "RallyTo";
                        break;
                    case team::proto::TeamCommandType::MoveTo:
                        name = "MoveTo";
                        break;
                    case team::proto::TeamCommandType::Hold:
                        name = "Hold";
                        break;
                    default:
                        break;
                    }
                    notice += "Command: ";
                    notice += name;
                }
                else
                {
                    notice += "Command";
                }
            }
            else
            {
                notice += "Message";
            }
            ui::SystemNotification::show(notice.c_str(), 3000);
            break;
        }
        case sys::EventType::ChatSendResult:
        {
            sys::ChatSendResultEvent* result_event = (sys::ChatSendResultEvent*)event;
            if (chat_service_)
            {
                chat_service_->handleSendResult(result_event->msg_id, result_event->success);
            }
            break;
        }
        case sys::EventType::NodeInfoUpdate:
        {
            sys::NodeInfoUpdateEvent* node_event = (sys::NodeInfoUpdateEvent*)event;
            APP_EVENT_LOG("[AppContext] NodeInfo event consumed node=%08lX pending=%u\n",
                          static_cast<unsigned long>(node_event->node_id),
                          static_cast<unsigned>(sys::EventBus::pendingCount()));
            // Update ContactService with node info from event
            if (contact_service_)
            {
                contact_service_->updateNodeInfo(
                    node_event->node_id,
                    node_event->short_name,
                    node_event->long_name,
                    node_event->snr,
                    node_event->rssi,
                    node_event->timestamp,
                    node_event->protocol,
                    node_event->role,
                    node_event->hops_away,
                    node_event->hw_model);
            }
            // Don't forward to UI - this is handled by ContactService
            delete event;
            continue; // Skip UI forwarding
        }
        case sys::EventType::NodeProtocolUpdate:
        {
            sys::NodeProtocolUpdateEvent* node_event = (sys::NodeProtocolUpdateEvent*)event;
            APP_EVENT_LOG("[AppContext] NodeProtocol event consumed node=%08lX pending=%u\n",
                          static_cast<unsigned long>(node_event->node_id),
                          static_cast<unsigned>(sys::EventBus::pendingCount()));
            if (contact_service_)
            {
                contact_service_->updateNodeProtocol(
                    node_event->node_id,
                    node_event->protocol,
                    node_event->timestamp);
            }
            delete event;
            continue;
        }
        case sys::EventType::NodePositionUpdate:
        {
            auto* pos_event = (sys::NodePositionUpdateEvent*)event;
            APP_EVENT_LOG("[AppContext] NodePosition event consumed node=%08lX pending=%u\n",
                          static_cast<unsigned long>(pos_event->node_id),
                          static_cast<unsigned>(sys::EventBus::pendingCount()));
            if (contact_service_)
            {
                chat::contacts::NodePosition pos{};
                pos.valid = true;
                pos.latitude_i = pos_event->latitude_i;
                pos.longitude_i = pos_event->longitude_i;
                pos.has_altitude = pos_event->has_altitude;
                pos.altitude = pos_event->altitude;
                pos.timestamp = pos_event->timestamp;
                pos.precision_bits = pos_event->precision_bits;
                pos.pdop = pos_event->pdop;
                pos.hdop = pos_event->hdop;
                pos.vdop = pos_event->vdop;
                pos.gps_accuracy_mm = pos_event->gps_accuracy_mm;
                contact_service_->updateNodePosition(pos_event->node_id, pos);
            }
            delete event;
            continue;
        }
        case sys::EventType::KeyVerificationNumberRequest:
        {
            auto* kv_event = (sys::KeyVerificationNumberRequestEvent*)event;
            std::string name = contact_service_ ? contact_service_->getContactName(kv_event->node_id) : "";
            if (name.empty())
            {
                char fallback[16];
                snprintf(fallback, sizeof(fallback), "%08lX",
                         static_cast<unsigned long>(kv_event->node_id));
                name = fallback;
            }
            std::string msg = "Key verify: enter number for " + name;
            ui::SystemNotification::show(msg.c_str(), 4000);
            delete event;
            continue;
        }
        case sys::EventType::KeyVerificationNumberInform:
        {
            auto* kv_event = (sys::KeyVerificationNumberInformEvent*)event;
            std::string name = contact_service_ ? contact_service_->getContactName(kv_event->node_id) : "";
            if (name.empty())
            {
                char fallback[16];
                snprintf(fallback, sizeof(fallback), "%08lX",
                         static_cast<unsigned long>(kv_event->node_id));
                name = fallback;
            }
            uint32_t number = kv_event->security_number % 1000000;
            char number_buf[16];
            snprintf(number_buf, sizeof(number_buf), "%03u %03u",
                     number / 1000, number % 1000);
            std::string msg = "Key verify: " + name + " " + number_buf;
            ui::SystemNotification::show(msg.c_str(), 5000);
            delete event;
            continue;
        }
        case sys::EventType::KeyVerificationFinal:
        {
            auto* kv_event = (sys::KeyVerificationFinalEvent*)event;
            std::string name = contact_service_ ? contact_service_->getContactName(kv_event->node_id) : "";
            if (name.empty())
            {
                char fallback[16];
                snprintf(fallback, sizeof(fallback), "%08lX",
                         static_cast<unsigned long>(kv_event->node_id));
                name = fallback;
            }
            std::string msg = std::string("Key verify: ") + (kv_event->is_sender ? "send " : "confirm ") +
                              kv_event->verification_code + " " + name;
            ui::SystemNotification::show(msg.c_str(), 5000);
            delete event;
            continue;
        }
        default:
            break;
        }

        hostlink::bridge::on_event(*event);

        // Forward event to UI controller if it exists
        if (event->type == sys::EventType::TeamKick ||
            event->type == sys::EventType::TeamTransferLeader ||
            event->type == sys::EventType::TeamKeyDist ||
            event->type == sys::EventType::TeamStatus ||
            event->type == sys::EventType::TeamPosition ||
            event->type == sys::EventType::TeamWaypoint ||
            event->type == sys::EventType::TeamTrack ||
            event->type == sys::EventType::TeamChat ||
            event->type == sys::EventType::TeamPairing ||
            event->type == sys::EventType::TeamError ||
            event->type == sys::EventType::SystemTick)
        {
            ui_team_handle_event(event);
            delete event;
            continue;
        }

        if (ui_controller_)
        {
            ui_controller_->onChatEvent(event);
        }
        else
        {
            delete event;
        }
    }
}

void AppContext::setBleEnabled(bool enabled)
{
    if (ble_manager_)
    {
        ble_manager_->setEnabled(enabled);
    }
}

chat::NodeId AppContext::getSelfNodeId() const
{
    // Cache in static so we don't hit NVS or MAC every call
    static chat::NodeId cached_id = 0;
    if (cached_id != 0)
    {
        return cached_id;
    }

    // First try to load a persisted node_id so IDs are stable across firmware updates
    uint32_t stored_id = 0;
    {
        Preferences prefs;
        prefs.begin("chat", true);
        stored_id = prefs.getUInt("node_id", 0);
        prefs.end();
    }
    if (stored_id != 0)
    {
        cached_id = stored_id;
        return cached_id;
    }

    // Derive a new node id from the chip's unique MAC address.
    uint64_t mac = 0;
#if __has_include(<Arduino.h>)
    mac = ESP.getEfuseMac();
#else
    uint8_t mac_bytes[6] = {};
    if (esp_efuse_mac_get_default(mac_bytes) == ESP_OK)
    {
        mac = (static_cast<uint64_t>(mac_bytes[0]) << 40) |
              (static_cast<uint64_t>(mac_bytes[1]) << 32) |
              (static_cast<uint64_t>(mac_bytes[2]) << 24) |
              (static_cast<uint64_t>(mac_bytes[3]) << 16) |
              (static_cast<uint64_t>(mac_bytes[4]) << 8) |
              (static_cast<uint64_t>(mac_bytes[5]));
    }
#endif

    uint32_t node_id = static_cast<uint32_t>(mac & 0xFFFFFFFFu);
    if (node_id == 0)
    {
        node_id = 1;
    }

    {
        Preferences prefs;
        prefs.begin("chat", false);
        prefs.putUInt("node_id", node_id);
        prefs.end();
    }
    cached_id = node_id;
    return cached_id;
}

void AppContext::clearNodeDb()
{
    if (node_store_)
    {
        node_store_->clear();
    }
    if (contact_service_)
    {
        contact_service_->clearCache();
    }
}

void AppContext::clearMessageDb()
{
    if (chat_service_)
    {
        chat_service_->clearAllMessages();
    }
    else if (chat_model_)
    {
        chat_model_->clearAll();
        if (chat_store_)
        {
            chat_store_->clearAll();
        }
    }
}

} // namespace app
