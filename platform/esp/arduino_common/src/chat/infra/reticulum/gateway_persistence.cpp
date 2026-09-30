#include "platform/esp/arduino_common/chat/infra/reticulum/gateway_persistence.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "sys/crc32.h"
#include <cstring>

namespace chat::reticulum
{
namespace
{
namespace storage = ::platform::esp::arduino_common::storage;
constexpr const char* kPaths[] = {"/trailmate/reticulum/gateway.a", "/trailmate/reticulum/gateway.b"};
uint32_t read32(const uint8_t* p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
void write32(uint8_t* p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(value >> (i * 8));
}
bool same(const GatewayPersistence::Endpoint& a, const GatewayPersistence::Endpoint& b)
{
    return a.port == b.port && std::strcmp(a.host, b.host) == 0 &&
           std::memcmp(a.network_identity, b.network_identity, 16) == 0 &&
           std::memcmp(a.transport_identity, b.transport_identity, 16) == 0;
}
bool validHost(const uint8_t* host)
{
    if (!host[0] || !std::memchr(host, 0, 64)) return false;
    for (unsigned i = 0; host[i]; ++i)
    {
        const auto c = host[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-')) return false;
    }
    return true;
}
} // namespace

void GatewayPersistence::poll(uint32_t now, bool stable_connection)
{
    if (!storage::sd_card_ready() || storage::sd_external_block_owner_active()) return;
    const auto session = storage::sd_media_session();
    if (session != session_)
    {
        session_ = session;
        loaded_ = attempted_ = wrote_ = false;
        sequence_ = 0;
        saved_ = {};
        saved_slot_ = 0;
    }
    if (loaded_ && (!stable_connection || !installed_.port || same(saved_, installed_))) return;
    if (attempted_ && static_cast<uint32_t>(now - last_attempt_) < 5000) return;
    if (loaded_ && wrote_ && static_cast<uint32_t>(now - last_write_) < 600000) return;
    attempted_ = true;
    last_attempt_ = now;
    if (!loaded_)
    {
        loaded_ = load();
        return;
    }
    if (save())
    {
        last_write_ = now;
        wrote_ = true;
    }
}

bool GatewayPersistence::load()
{
    for (uint8_t slot = 0; slot < 2; ++slot)
    {
        const auto read = storage::sd_read_file(kPaths[slot], record_, sizeof(record_));
        if (read.status == storage::SdFileReadStatus::Missing || read.status == storage::SdFileReadStatus::Invalid) continue;
        if (read.status != storage::SdFileReadStatus::Ready) return false;
        if (read.bytes_read != kRecordSize || read.file_size != kRecordSize ||
            std::memcmp(record_, "RGW1\0\0\0\0", 8) != 0 ||
            sys::crc32(record_, 110) != read32(record_ + 110) || !validHost(record_ + 12)) continue;
        const uint16_t port = static_cast<uint16_t>(record_[76] | (uint16_t(record_[77]) << 8));
        if (!port) continue;
        const uint32_t sequence = read32(record_ + 8);
        if (saved_.port && static_cast<int32_t>(sequence - sequence_) <= 0) continue;
        sequence_ = sequence;
        saved_slot_ = slot;
        std::memcpy(saved_.host, record_ + 12, 64);
        saved_.port = port;
        std::memcpy(saved_.network_identity, record_ + 78, 16);
        std::memcpy(saved_.transport_identity, record_ + 94, 16);
    }
    return session_ == storage::sd_media_session() && !storage::sd_external_block_owner_active();
}

bool GatewayPersistence::save()
{
    if (!validHost(reinterpret_cast<const uint8_t*>(installed_.host)) || !installed_.port) return false;
    if (!storage::sd_is_directory("/trailmate") && !storage::sd_mkdir("/trailmate")) return false;
    if (!storage::sd_is_directory("/trailmate/reticulum") && !storage::sd_mkdir("/trailmate/reticulum")) return false;
    std::memset(record_, 0, sizeof(record_));
    std::memcpy(record_, "RGW1", 4);
    write32(record_ + 8, sequence_ + 1);
    std::memcpy(record_ + 12, installed_.host, 64);
    record_[76] = static_cast<uint8_t>(installed_.port);
    record_[77] = static_cast<uint8_t>(installed_.port >> 8);
    std::memcpy(record_ + 78, installed_.network_identity, 16);
    std::memcpy(record_ + 94, installed_.transport_identity, 16);
    write32(record_ + 110, sys::crc32(record_, 110));
    const auto next_slot = static_cast<uint8_t>(1 - saved_slot_);
    storage::SdRuntimeFile file;
    if (!file.open(kPaths[next_slot], "w", session_) ||
        file.write(record_, sizeof(record_)) != sizeof(record_) || !file.flush()) return false;
    file.close();
    if (session_ != storage::sd_media_session() || storage::sd_external_block_owner_active()) return false;
    ++sequence_;
    saved_slot_ = next_slot;
    saved_ = installed_;
    return true;
}
} // namespace chat::reticulum
