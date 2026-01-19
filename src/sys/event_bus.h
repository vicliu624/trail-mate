/**
 * @file event_bus.h
 * @brief Event bus for inter-task communication
 */

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <Arduino.h>
#include <cstring>

namespace sys
{

/**
 * @brief Event types
 */
enum class EventType
{
    ChatNewMessage,      // New message received
    ChatSendResult,      // Message send result
    ChatUnreadChanged,   // Unread count changed
    ChatChannelSwitched, // Channel switched
    NodeInfoUpdate,      // Node info updated (from mesh network)
    NodeProtocolUpdate,  // Node protocol update (from message)
    InputEvent,          // Input event (keyboard/rotary)
    SystemTick           // System tick (for periodic tasks)
};

/**
 * @brief Base event structure
 */
struct Event
{
    EventType type;
    uint32_t timestamp;

    Event(EventType t) : type(t), timestamp(millis()) {}
    virtual ~Event() = default;
};

/**
 * @brief Chat new message event
 */
struct ChatNewMessageEvent : public Event
{
    uint8_t channel;
    uint32_t msg_id;
    char text[64]; // Message text (truncated if needed)

    ChatNewMessageEvent(uint8_t ch, uint32_t id, const char* msg_text = "")
        : Event(EventType::ChatNewMessage), channel(ch), msg_id(id)
    {
        if (msg_text)
        {
            strncpy(text, msg_text, sizeof(text) - 1);
            text[sizeof(text) - 1] = '\0';
        }
        else
        {
            text[0] = '\0';
        }
    }
};

/**
 * @brief Chat send result event
 */
struct ChatSendResultEvent : public Event
{
    uint32_t msg_id;
    bool success;

    ChatSendResultEvent(uint32_t id, bool ok)
        : Event(EventType::ChatSendResult), msg_id(id), success(ok) {}
};

/**
 * @brief Chat unread changed event
 */
struct ChatUnreadChangedEvent : public Event
{
    uint8_t channel;
    int unread_count;

    ChatUnreadChangedEvent(uint8_t ch, int count)
        : Event(EventType::ChatUnreadChanged), channel(ch), unread_count(count) {}
};

/**
 * @brief Node info update event
 */
struct NodeInfoUpdateEvent : public Event
{
    uint32_t node_id;
    char short_name[10];
    char long_name[32];
    float snr;
    uint32_t timestamp; // Unix timestamp (seconds)
    uint8_t protocol;

    NodeInfoUpdateEvent(uint32_t id, const char* sname, const char* lname, float s, uint32_t ts, uint8_t proto)
        : Event(EventType::NodeInfoUpdate), node_id(id), snr(s), timestamp(ts), protocol(proto)
    {
        if (sname)
        {
            strncpy(short_name, sname, sizeof(short_name) - 1);
            short_name[sizeof(short_name) - 1] = '\0';
        }
        else
        {
            short_name[0] = '\0';
        }
        if (lname)
        {
            strncpy(long_name, lname, sizeof(long_name) - 1);
            long_name[sizeof(long_name) - 1] = '\0';
        }
        else
        {
            long_name[0] = '\0';
        }
    }
};

/**
 * @brief Node protocol update event
 */
struct NodeProtocolUpdateEvent : public Event
{
    uint32_t node_id;
    uint32_t timestamp; // Unix timestamp (seconds)
    uint8_t protocol;

    NodeProtocolUpdateEvent(uint32_t id, uint32_t ts, uint8_t proto)
        : Event(EventType::NodeProtocolUpdate), node_id(id), timestamp(ts), protocol(proto) {}
};

/**
 * @brief Input event
 */
struct InputEvent
{
    enum InputType
    {
        KeyPress,
        KeyRelease,
        RotaryTurn,
        RotaryPress,
        RotaryLongPress
    };

    InputType input_type;
    uint32_t value; // Key code or rotary delta
    uint32_t timestamp;

    InputEvent(InputType it, uint32_t v)
        : input_type(it), value(v), timestamp(millis()) {}
};

/**
 * @brief Event bus for inter-task communication
 */
class EventBus
{
  public:
    /**
     * @brief Initialize event bus
     * @param queue_size Maximum queue size
     * @return true if successful
     */
    static bool init(size_t queue_size = 32)
    {
        if (instance_.queue_ != nullptr)
        {
            return true; // Already initialized
        }
        instance_.queue_ = xQueueCreate(queue_size, sizeof(Event*));
        return instance_.queue_ != nullptr;
    }

    /**
     * @brief Publish an event
     * @param event Event to publish (will be copied)
     * @param timeout_ms Timeout in milliseconds
     * @return true if successful
     */
    static bool publish(Event* event, uint32_t timeout_ms = portMAX_DELAY)
    {
        if (instance_.queue_ == nullptr)
        {
            delete event;
            return false;
        }

        BaseType_t result = xQueueSend(instance_.queue_, &event,
                                       pdMS_TO_TICKS(timeout_ms));
        if (result != pdPASS)
        {
            delete event;
            return false;
        }
        return true;
    }

    /**
     * @brief Subscribe to events (receive next event)
     * @param event_out Pointer to receive event pointer
     * @param timeout_ms Timeout in milliseconds
     * @return true if event received
     */
    static bool subscribe(Event** event_out, uint32_t timeout_ms = portMAX_DELAY)
    {
        if (instance_.queue_ == nullptr)
        {
            return false;
        }
        return xQueueReceive(instance_.queue_, event_out,
                             pdMS_TO_TICKS(timeout_ms)) == pdPASS;
    }

    /**
     * @brief Get number of pending events
     */
    static size_t pendingCount()
    {
        if (instance_.queue_ == nullptr)
        {
            return 0;
        }
        return uxQueueMessagesWaiting(instance_.queue_);
    }

    /**
     * @brief Clear all pending events
     */
    static void clear()
    {
        if (instance_.queue_ == nullptr)
        {
            return;
        }
        Event* event;
        while (xQueueReceive(instance_.queue_, &event, 0) == pdPASS)
        {
            delete event;
        }
    }

  private:
    QueueHandle_t queue_;
    static EventBus instance_; // Declaration only

    EventBus() : queue_(nullptr) {}
    ~EventBus()
    {
        if (queue_ != nullptr)
        {
            clear();
            vQueueDelete(queue_);
        }
    }
};

} // namespace sys
