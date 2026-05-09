#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

#include "chat/domain/chat_types.h"
#include "chat/domain/contact_types.h"
#include "sys/clock.h"
#include "team/domain/team_events.h"

namespace sys
{

enum class EventType
{
    ChatNewMessage,
    ChatSendResult,
    ChatUnreadChanged,
    ChatChannelSwitched,
    NodeInfoUpdate,
    NodeProtocolUpdate,
    NodePositionUpdate,
    KeyVerificationNumberRequest,
    KeyVerificationNumberInform,
    KeyVerificationFinal,
    AppData,
    TeamKick,
    TeamTransferLeader,
    TeamKeyDist,
    TeamStatus,
    TeamPosition,
    TeamWaypoint,
    TeamTrack,
    TeamChat,
    TeamPairing,
    TeamError,
    InputEvent,
    SystemTick
};

struct Event
{
    EventType type;
    uint32_t timestamp;

    explicit Event(EventType t) : type(t), timestamp(sys::millis_now()) {}
    virtual ~Event() = default;
};

struct ChatNewMessageEvent : public Event
{
    uint8_t channel;
    uint32_t msg_id;
    char text[64];
    chat::RxMeta rx_meta;

    ChatNewMessageEvent(uint8_t ch, uint32_t id, const char* msg_text = "",
                        const chat::RxMeta* meta = nullptr)
        : Event(EventType::ChatNewMessage), channel(ch), msg_id(id)
    {
        if (msg_text)
        {
            std::strncpy(text, msg_text, sizeof(text) - 1);
            text[sizeof(text) - 1] = '\0';
        }
        else
        {
            text[0] = '\0';
        }
        if (meta)
        {
            rx_meta = *meta;
        }
    }
};

struct ChatSendResultEvent : public Event
{
    uint32_t msg_id;
    bool success;

    ChatSendResultEvent(uint32_t id, bool ok)
        : Event(EventType::ChatSendResult), msg_id(id), success(ok) {}
};

struct ChatUnreadChangedEvent : public Event
{
    uint8_t channel;
    int unread_count;

    ChatUnreadChangedEvent(uint8_t ch, int count)
        : Event(EventType::ChatUnreadChanged), channel(ch), unread_count(count) {}
};

struct NodeInfoUpdateEvent : public Event
{
    uint32_t node_id;
    char short_name[10];
    char long_name[32];
    float snr;
    float rssi;
    uint32_t event_timestamp;
    uint8_t protocol;
    uint8_t role;
    uint8_t hops_away;
    uint8_t hw_model;
    uint8_t channel;
    bool has_macaddr;
    uint8_t macaddr[6];
    bool via_mqtt;
    bool is_ignored;
    bool has_public_key;
    bool key_manually_verified;
    bool has_device_metrics;
    chat::contacts::NodeDeviceMetrics device_metrics;

    NodeInfoUpdateEvent(uint32_t id, const char* sname, const char* lname, float s, float rssi_val,
                        uint32_t ts, uint8_t proto, uint8_t r, uint8_t hops = 0xFF, uint8_t hw = 0,
                        uint8_t ch = 0xFF, bool has_mac = false, const uint8_t* mac = nullptr,
                        bool via_mqtt_value = false, bool is_ignored_value = false,
                        bool has_pubkey = false, bool key_verified = false,
                        bool has_metrics = false,
                        const chat::contacts::NodeDeviceMetrics* metrics = nullptr)
        : Event(EventType::NodeInfoUpdate),
          node_id(id),
          short_name{},
          long_name{},
          snr(s),
          rssi(rssi_val),
          event_timestamp(ts),
          protocol(proto),
          role(r),
          hops_away(hops),
          hw_model(hw),
          channel(ch),
          has_macaddr(has_mac),
          macaddr{},
          via_mqtt(via_mqtt_value),
          is_ignored(is_ignored_value),
          has_public_key(has_pubkey),
          key_manually_verified(key_verified),
          has_device_metrics(has_metrics),
          device_metrics{}
    {
        if (sname)
        {
            std::strncpy(short_name, sname, sizeof(short_name) - 1);
            short_name[sizeof(short_name) - 1] = '\0';
        }
        if (lname)
        {
            std::strncpy(long_name, lname, sizeof(long_name) - 1);
            long_name[sizeof(long_name) - 1] = '\0';
        }
        if (has_macaddr && mac)
        {
            std::memcpy(macaddr, mac, sizeof(macaddr));
        }
        if (has_device_metrics && metrics)
        {
            device_metrics = *metrics;
        }
    }
};

struct NodeProtocolUpdateEvent : public Event
{
    uint32_t node_id;
    uint32_t event_timestamp;
    uint8_t protocol;

    NodeProtocolUpdateEvent(uint32_t id, uint32_t ts, uint8_t proto)
        : Event(EventType::NodeProtocolUpdate), node_id(id), event_timestamp(ts), protocol(proto) {}
};

struct NodePositionUpdateEvent : public Event
{
    uint32_t node_id;
    int32_t latitude_i;
    int32_t longitude_i;
    bool has_altitude;
    int32_t altitude;
    uint32_t event_timestamp;
    uint32_t precision_bits;
    uint32_t pdop;
    uint32_t hdop;
    uint32_t vdop;
    uint32_t gps_accuracy_mm;

    NodePositionUpdateEvent(uint32_t id,
                            int32_t lat_i,
                            int32_t lon_i,
                            bool has_alt,
                            int32_t alt_m,
                            uint32_t ts,
                            uint32_t prec_bits,
                            uint32_t p,
                            uint32_t h,
                            uint32_t v,
                            uint32_t gps_mm)
        : Event(EventType::NodePositionUpdate),
          node_id(id),
          latitude_i(lat_i),
          longitude_i(lon_i),
          has_altitude(has_alt),
          altitude(alt_m),
          event_timestamp(ts),
          precision_bits(prec_bits),
          pdop(p),
          hdop(h),
          vdop(v),
          gps_accuracy_mm(gps_mm)
    {
    }
};

struct AppDataEvent : public Event
{
    uint32_t portnum;
    uint32_t from;
    uint32_t to;
    uint32_t packet_id;
    uint8_t channel;
    uint8_t channel_hash;
    bool want_response;
    std::vector<uint8_t> payload;
    chat::RxMeta rx_meta;

    AppDataEvent(uint32_t port,
                 uint32_t src,
                 uint32_t dst,
                 uint32_t pkt,
                 uint8_t ch,
                 uint8_t hash,
                 bool want,
                 const std::vector<uint8_t>& data,
                 const chat::RxMeta* meta = nullptr)
        : Event(EventType::AppData),
          portnum(port),
          from(src),
          to(dst),
          packet_id(pkt),
          channel(ch),
          channel_hash(hash),
          want_response(want),
          payload(data)
    {
        if (meta)
        {
            rx_meta = *meta;
        }
    }
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
        : Event(EventType::KeyVerificationFinal), node_id(id), nonce(n), is_sender(sender), verification_code{}
    {
        if (code)
        {
            std::strncpy(verification_code, code, sizeof(verification_code) - 1);
            verification_code[sizeof(verification_code) - 1] = '\0';
        }
    }
};

struct TeamKickEvent : public Event
{
    team::TeamKickEvent data;
    explicit TeamKickEvent(const team::TeamKickEvent& evt) : Event(EventType::TeamKick), data(evt) {}
};

struct TeamTransferLeaderEvent : public Event
{
    team::TeamTransferLeaderEvent data;
    explicit TeamTransferLeaderEvent(const team::TeamTransferLeaderEvent& evt)
        : Event(EventType::TeamTransferLeader), data(evt) {}
};

struct TeamKeyDistEvent : public Event
{
    team::TeamKeyDistEvent data;
    explicit TeamKeyDistEvent(const team::TeamKeyDistEvent& evt) : Event(EventType::TeamKeyDist), data(evt) {}
};

struct TeamStatusEvent : public Event
{
    team::TeamStatusEvent data;
    explicit TeamStatusEvent(const team::TeamStatusEvent& evt) : Event(EventType::TeamStatus), data(evt) {}
};

struct TeamPositionEvent : public Event
{
    team::TeamPositionEvent data;
    explicit TeamPositionEvent(const team::TeamPositionEvent& evt) : Event(EventType::TeamPosition), data(evt) {}
};

struct TeamWaypointEvent : public Event
{
    team::TeamWaypointEvent data;
    explicit TeamWaypointEvent(const team::TeamWaypointEvent& evt) : Event(EventType::TeamWaypoint), data(evt) {}
};

struct TeamTrackEvent : public Event
{
    team::TeamTrackEvent data;
    explicit TeamTrackEvent(const team::TeamTrackEvent& evt) : Event(EventType::TeamTrack), data(evt) {}
};

struct TeamChatEvent : public Event
{
    team::TeamChatEvent data;
    explicit TeamChatEvent(const team::TeamChatEvent& evt) : Event(EventType::TeamChat), data(evt) {}
};

struct TeamPairingEvent : public Event
{
    team::TeamPairingEvent data;
    explicit TeamPairingEvent(const team::TeamPairingEvent& evt) : Event(EventType::TeamPairing), data(evt) {}
};

struct TeamErrorEvent : public Event
{
    team::TeamErrorEvent data;
    explicit TeamErrorEvent(const team::TeamErrorEvent& evt) : Event(EventType::TeamError), data(evt) {}
};

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
    uint32_t value;
    uint32_t timestamp;

    InputEvent(InputType it, uint32_t v)
        : input_type(it), value(v), timestamp(sys::millis_now()) {}
};

class EventBus
{
  public:
    static bool init(size_t queue_size = 32)
    {
        EventBus& bus = instance();
        std::lock_guard<std::mutex> lock(bus.mutex_);
        bus.queue_size_ = queue_size > 0 ? queue_size : 1;
        while (bus.queue_.size() > bus.queue_size_)
        {
            delete bus.queue_.front();
            bus.queue_.pop_front();
        }
        return true;
    }

    static bool publish(Event* event, uint32_t timeout_ms = 0)
    {
        (void)timeout_ms;
        if (event == nullptr)
        {
            return false;
        }

        EventBus& bus = instance();
        std::lock_guard<std::mutex> lock(bus.mutex_);
        if (bus.queue_.size() >= bus.queue_size_)
        {
            delete event;
            return false;
        }
        bus.queue_.push_back(event);
        return true;
    }

    static bool subscribe(Event** event_out, uint32_t timeout_ms = 0)
    {
        (void)timeout_ms;
        if (event_out == nullptr)
        {
            return false;
        }

        EventBus& bus = instance();
        std::lock_guard<std::mutex> lock(bus.mutex_);
        if (bus.queue_.empty())
        {
            *event_out = nullptr;
            return false;
        }

        *event_out = bus.queue_.front();
        bus.queue_.pop_front();
        return true;
    }

    static size_t pendingCount()
    {
        EventBus& bus = instance();
        std::lock_guard<std::mutex> lock(bus.mutex_);
        return bus.queue_.size();
    }

  private:
    static EventBus& instance()
    {
        static EventBus bus;
        return bus;
    }

    std::mutex mutex_{};
    std::deque<Event*> queue_{};
    size_t queue_size_ = 32;
};

} // namespace sys
