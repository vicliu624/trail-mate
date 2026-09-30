#pragma once
#include "chat/domain/reticulum_network_config.h"
#include "platform/esp/arduino_common/chat/infra/reticulum/ifac_codec.h"
#include "platform/memory/psram_ptr.h"
#include <Crypto.h>

namespace chat::reticulum
{
// Lives with its interface in PSRAM. Public interfaces allocate no codec.
class InterfaceAccess
{
  public:
    ~InterfaceAccess() { clean(config_); }
    bool matches(const InterfaceAccessConfig& config) const { return ready_ && config_ == config; }
    bool configure(const InterfaceAccessConfig& config)
    {
        if (matches(config)) return true;
        codec_.reset();
        ready_ = false;
        clean(config_);
        config_ = config;
        if (!config_.valid()) return false;
        if (!config_.enabled()) return ready_ = true;
        codec_.reset(platform::memory::createPsram<IfacCodec>());
        return ready_ = codec_ && codec_->configure(config_.network_name, config_.passphrase, config_.ifac_size_bits / 8);
    }
    bool encode(uint8_t* frame, size_t& length, size_t capacity)
    {
        if (ready_ && codec_) return codec_->encode(frame, length, capacity);
        return publicFrame(frame, length, capacity);
    }
    bool decode(uint8_t* frame, size_t& length, size_t capacity)
    {
        if (ready_ && codec_) return codec_->decode(frame, length, capacity);
        return publicFrame(frame, length, capacity);
    }

  private:
    bool publicFrame(uint8_t* frame, size_t& length, size_t capacity) const
    {
        if (ready_ && !config_.enabled() && frame && length >= 3 && length <= IfacCodec::kMaxPacketSize && length <= capacity && !(frame[0] & 0x80)) return true;
        length = 0;
        return false;
    }
    InterfaceAccessConfig config_;
    platform::memory::PsramPtr<IfacCodec> codec_;
    bool ready_ = false;
};
static_assert(sizeof(InterfaceAccess) <= 160);
} // namespace chat::reticulum
