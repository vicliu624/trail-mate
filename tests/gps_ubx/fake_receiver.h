#pragma once
#include <Arduino.h>
#include <algorithm>
#include <deque>
#include <vector>

inline std::vector<uint8_t> ubxFrame(uint8_t cls, uint8_t id, const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> frame = {0xb5, 0x62, cls, id,
                                  static_cast<uint8_t>(payload.size()),
                                  static_cast<uint8_t>(payload.size() >> 8)};
    frame.insert(frame.end(), payload.begin(), payload.end());
    uint8_t a = 0, b = 0;
    for (size_t i = 2; i < frame.size(); ++i)
    {
        a += frame[i];
        b += a;
    }
    frame.push_back(a);
    frame.push_back(b);
    return frame;
}

inline std::string nmeaSentence(const std::string& body)
{
    uint8_t checksum = 0;
    for (char c : body) checksum ^= static_cast<uint8_t>(c);
    const char* hex = "0123456789ABCDEF";
    return "$" + body + "*" + hex[checksum >> 4] + hex[checksum & 15] + "\r\n";
}

class FakeReceiver : public Stream
{
  public:
    enum Reply
    {
        Ack,
        CorruptThenAck,
        UnrelatedThenAck,
        FirstNak
    };
    Reply reply = Ack;
    bool fragmented = false;
    std::string nmea_before_ack;
    std::vector<std::vector<uint8_t>> commands;
    std::deque<std::pair<uint32_t, uint8_t>> input;

    int available() override
    {
        return !input.empty() && input.front().first <= test_millis;
    }
    int read() override
    {
        if (!available()) return -1;
        const int value = input.front().second;
        input.pop_front();
        return value;
    }
    size_t write(uint8_t value) override
    {
        outgoing_.push_back(value);
        if (outgoing_.size() < 6) return 1;
        const size_t length = static_cast<size_t>(outgoing_[4]) +
                              (static_cast<size_t>(outgoing_[5]) << 8);
        if (outgoing_.size() != length + 8) return 1;
        commands.push_back(outgoing_);
        if (!nmea_before_ack.empty())
            enqueue(std::vector<uint8_t>(nmea_before_ack.begin(), nmea_before_ack.end()));
        auto ack = ubxFrame(0x05, reply == FirstNak && commands.size() == 1 ? 0x00 : 0x01,
                            {outgoing_[2], outgoing_[3]});
        if (reply == CorruptThenAck)
        {
            auto bad = ack;
            bad.back() ^= 0x80;
            enqueue(bad);
        }
        if (reply == UnrelatedThenAck) enqueue(ubxFrame(0x01, 0x07, {1, 2, 3, 4}));
        enqueue(ack);
        outgoing_.clear();
        return 1;
    }
    void enqueue(const std::vector<uint8_t>& bytes)
    {
        uint32_t time = input.empty() ? test_millis : input.back().first;
        for (uint8_t value : bytes)
        {
            time += fragmented ? 2 : 0;
            input.push_back({time, value});
        }
    }

  private:
    std::vector<uint8_t> outgoing_;
};
