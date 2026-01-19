/**
 * @file app_context.cpp
 * @brief Application context implementation
 */

#include "app_context.h"
#include "../chat/infra/protocol_factory.h"
#include "../gps/usecase/gps_service.h"
#include "../sys/event_bus.h"
#include "../ui/widgets/system_notification.h"
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

    gps::GpsService::getInstance().begin(
        board,
        disable_hw_init,
        config_.gps_interval_ms,
        config_.motion_config);

    // Create domain model
    chat_model_ = std::make_unique<chat::ChatModel>();
    chat_model_->setPolicy(config_.chat_policy);

    // Create storage (flash-backed ring, 300 messages FIFO)
    auto flash_store = std::make_unique<chat::FlashStore>();
    if (flash_store->isReady())
    {
        flash_store_ = flash_store.get();
        chat_store_ = std::move(flash_store);
    }
    else
    {
        flash_store_ = nullptr;
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

    // Load persisted messages into model (no unread)
    if (flash_store_)
    {
        std::vector<chat::ChatMessage> all_msgs = flash_store_->loadAll();
        std::vector<chat::ConversationId> touched;
        touched.reserve(all_msgs.size());
        for (const auto& msg : all_msgs)
        {
            if (msg.from == 0)
            {
                chat_model_->onSendQueued(msg);
            }
            else
            {
                chat_model_->onIncoming(msg);
            }
            chat::ConversationId conv(msg.channel, msg.peer ? msg.peer : msg.from);
            touched.push_back(conv);
        }
        for (const auto& conv : touched)
        {
            chat_model_->markRead(conv);
        }
    }

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
            ui::SystemNotification::show(msg_event->text, 10000);
            break;
        }
        case sys::EventType::NodeInfoUpdate:
        {
            sys::NodeInfoUpdateEvent* node_event = (sys::NodeInfoUpdateEvent*)event;
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
            chat_store_->clearChannel(chat::ChannelId::PRIMARY);
            chat_store_->clearChannel(chat::ChannelId::SECONDARY);
            chat_store_->setUnread(chat::ChannelId::PRIMARY, 0);
            chat_store_->setUnread(chat::ChannelId::SECONDARY, 0);
        }
    }
}

} // namespace app
