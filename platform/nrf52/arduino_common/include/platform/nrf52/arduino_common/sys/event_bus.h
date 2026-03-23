#pragma once

#include "sys/clock.h"

#include <cstdint>
#include <cstring>
#include <deque>

namespace sys
{

enum class EventType
{
    KeyVerificationNumberRequest,
    KeyVerificationNumberInform,
    KeyVerificationFinal,
};

struct Event
{
    EventType type;
    uint32_t timestamp;

    explicit Event(EventType t) : type(t), timestamp(sys::millis_now()) {}
    virtual ~Event() = default;
};

struct KeyVerificationNumberRequestEvent : public Event
{
    uint32_t node_id;
    uint64_t nonce;

    KeyVerificationNumberRequestEvent(uint32_t id, uint64_t n)
        : Event(EventType::KeyVerificationNumberRequest), node_id(id), nonce(n) {}
};

struct KeyVerificationNumberInformEvent : public Event
{
    uint32_t node_id;
    uint64_t nonce;
    uint32_t security_number;

    KeyVerificationNumberInformEvent(uint32_t id, uint64_t n, uint32_t number)
        : Event(EventType::KeyVerificationNumberInform), node_id(id), nonce(n), security_number(number) {}
};

struct KeyVerificationFinalEvent : public Event
{
    uint32_t node_id;
    uint64_t nonce;
    bool is_sender;
    char verification_code[12];

    KeyVerificationFinalEvent(uint32_t id, uint64_t n, bool sender, const char* code)
        : Event(EventType::KeyVerificationFinal), node_id(id), nonce(n), is_sender(sender)
    {
        if (code)
        {
            std::strncpy(verification_code, code, sizeof(verification_code) - 1);
            verification_code[sizeof(verification_code) - 1] = '\0';
        }
        else
        {
            verification_code[0] = '\0';
        }
    }
};

class EventBus
{
  public:
    static bool init(size_t queue_size = 32)
    {
        instance_.queue_size_ = queue_size;
        return true;
    }

    static bool publish(Event* event, uint32_t timeout_ms = 0)
    {
        (void)timeout_ms;
        if (!event)
        {
            return false;
        }
        if (instance_.queue_size_ == 0)
        {
            instance_.queue_size_ = 32;
        }
        while (instance_.events_.size() >= instance_.queue_size_)
        {
            delete instance_.events_.front();
            instance_.events_.pop_front();
        }
        instance_.events_.push_back(event);
        return true;
    }

    static bool subscribe(Event** event_out, uint32_t timeout_ms = 0)
    {
        (void)timeout_ms;
        if (!event_out || instance_.events_.empty())
        {
            return false;
        }
        *event_out = instance_.events_.front();
        instance_.events_.pop_front();
        return true;
    }

    static size_t pendingCount()
    {
        return instance_.events_.size();
    }

    static void clear()
    {
        while (!instance_.events_.empty())
        {
            delete instance_.events_.front();
            instance_.events_.pop_front();
        }
    }

  private:
    std::deque<Event*> events_{};
    size_t queue_size_ = 32;
    static EventBus instance_;
};

} // namespace sys
