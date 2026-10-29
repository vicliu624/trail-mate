#include "platform/esp/arduino_common/chat/infra/reticulum/ifac_codec.h"
#if defined(ESP_PLATFORM)
#if __has_include(<esp_memory_utils.h>)
#include <esp_memory_utils.h>
#else
#include <soc/soc_memory_types.h>
#endif
#endif

namespace chat::reticulum
{
bool IfacCodec::configure(const char* name, const char* passphrase, size_t tag_bytes)
{
    codec_.clear();
#if defined(ESP_PLATFORM)
    if (!esp_ptr_external_ram(this)) return false;
#endif
    return codec_.configure(name, passphrase, tag_bytes);
}
bool IfacCodec::encode(uint8_t* frame, size_t& size, size_t capacity) { return codec_.encode(frame, size, capacity); }
bool IfacCodec::decode(uint8_t* frame, size_t& size, size_t capacity) { return codec_.decode(frame, size, capacity); }
void IfacCodec::clear() { codec_.clear(); }
} // namespace chat::reticulum
