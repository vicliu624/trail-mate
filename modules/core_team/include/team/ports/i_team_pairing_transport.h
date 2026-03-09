#pragma once

#include <cstddef>
#include <cstdint>

namespace team
{

class ITeamPairingTransport
{
  public:
    class Receiver
    {
      public:
        virtual ~Receiver() = default;
        virtual void onPairingReceive(const uint8_t* mac, const uint8_t* data, size_t len) = 0;
    };

    virtual ~ITeamPairingTransport() = default;

    virtual bool begin(Receiver& receiver) = 0;
    virtual void end() = 0;
    virtual bool ensurePeer(const uint8_t* mac) = 0;
    virtual bool send(const uint8_t* mac, const uint8_t* data, size_t len) = 0;
};

} // namespace team
