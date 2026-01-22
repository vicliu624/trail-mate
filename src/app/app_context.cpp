/**
 * @file app_context.cpp
 * @brief Application context implementation
 */

#include "app_context.h"
#include "../chat/infra/protocol_factory.h"
#include "../gps/usecase/gps_service.h"
#include "../sys/event_bus.h"
#include "../ui/widgets/system_notification.h"
#include "../ui/ui_common.h"
#include "app_tasks.h"
#include <SD.h>

namespace app
{

bool AppContext::init(TLoRaPagerBoard& board, bool use_mock_adapter, uint32_t disable_hw_init)
{
    // Store board reference for hardware access
    board_ = &board;

    // Initialize event bus
    if (!sys::EventBus::init())
    {
        return false;
    }

    // Load configuration
    config_.load(preferences_);
    (void)ui_get_timezone_offset_min();

    gps::GpsService::getInstance().begin(
        board,
        disable_hw_init,
        config_.gps_interval_ms,
        config_.motion_config);

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

    // Create mesh adapter (selected by config)
    (void)use_mock_adapter;
    auto adapter = chat::ProtocolFactory::createAdapter(config_.mesh_protocol, board);
    if (adapter)
    {
        adapter->applyConfig(config_.mesh_config);
        mesh_adapter_ = std::move(adapter);
    }
    chat::IMeshAdapter* adapter_raw = mesh_adapter_.get();

    // Initialize tasks for real LoRa
    if (!app::AppTasks::init(board, adapter_raw))
    {
        Serial.printf("[APP] WARNING: Failed to start LoRa tasks\n");
    }
    else
    {
        Serial.printf("[APP] LoRa tasks started\n");
    }

    // Create chat service
    chat_service_ = std::make_unique<chat::ChatService>(
        *chat_model_, *mesh_adapter_, *chat_store_);

    // Create team service (protocol-only for now)
    team_crypto_ = std::make_unique<team::infra::TeamCrypto>();
    team_event_sink_ = std::make_unique<team::infra::TeamEventBusSink>();
    team_service_ = std::make_unique<team::TeamService>(
        *team_crypto_, *mesh_adapter_, *team_event_sink_);
    team_controller_ = std::make_unique<team::TeamController>(*team_service_);

    // Create contact infrastructure
    node_store_ = std::make_unique<chat::meshtastic::NodeStore>();
    contact_store_ = std::make_unique<chat::contacts::ContactStore>();

    // Create contact service with dependency injection
    contact_service_ = std::make_unique<chat::contacts::ContactService>(
        *node_store_, *contact_store_);
    contact_service_->begin();

    return true;
}

void AppContext::update()
{
    // Update chat service (process incoming messages)
    if (chat_service_)
    {
        chat_service_->processIncoming();
    }
    if (team_service_)
    {
        team_service_->processIncoming();
    }

    // Update UI controller
    if (ui_controller_)
    {
        ui_controller_->update();
    }

    // Process events
    sys::Event* event = nullptr;
    while (sys::EventBus::subscribe(&event, 0))
    {
        if (!event)
        {
            continue;
        }

        // Handle global events (like haptic feedback) before UI-specific handling
        switch (event->type)
        {
        case sys::EventType::ChatNewMessage:
        {
            sys::ChatNewMessageEvent* msg_event = (sys::ChatNewMessageEvent*)event;
            Serial.printf("[AppContext::update] ChatNewMessage received: channel=%d\n", msg_event->channel);

            // Global haptic feedback on incoming messages (works regardless of UI state)
            if (board_)
            {
                Serial.printf("[AppContext::update] Triggering haptic feedback...\n");
                board_->vibrator();
                Serial.printf("[AppContext::update] Haptic feedback triggered\n");
            }
            else
            {
                Serial.printf("[AppContext::update] WARNING: board_ is nullptr, cannot trigger vibration\n");
            }

            // Show system notification
            ui::SystemNotification::show(msg_event->text, 3000);
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
            Serial.printf("[AppContext] NodeInfo event consumed node=%08lX pending=%u\n",
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
                    node_event->timestamp,
                    node_event->protocol);
            }
            // Don't forward to UI - this is handled by ContactService
            delete event;
            continue; // Skip UI forwarding
        }
        case sys::EventType::NodeProtocolUpdate:
        {
            sys::NodeProtocolUpdateEvent* node_event = (sys::NodeProtocolUpdateEvent*)event;
            Serial.printf("[AppContext] NodeProtocol event consumed node=%08lX pending=%u\n",
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

        // Forward event to UI controller if it exists
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
