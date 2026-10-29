#pragma once
#include "app/tms_config_codec.h"
#include "phone/meshtastic/meshtastic_phone_config_bridge.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <pb_decode.h>
#include <pb_encode.h>

namespace app::tms
{
// Configuration compatibility data only. This object has no protocol, device,
// storage or service ownership. ESP owners must allocate it outside task stacks.
struct CompatibilityRecords
{
    static constexpr std::size_t kModuleChunkBytes = 240U;
    static constexpr std::size_t kModuleChunkCount =
        (meshtastic_LocalModuleConfig_size + kModuleChunkBytes - 1U) / kModuleChunkBytes;
    static_assert(kModuleChunkCount == 3U,
                  "Update the strict TMS module chunk records when the protobuf limit changes.");

    enum Seen : uint32_t
    {
        SeenMtEnabled = 1UL << 0U,
        SeenMtMode = 1UL << 1U,
        SeenMtPin = 1UL << 2U,
        SeenMcPin = 1UL << 3U,
        SeenMcManualAdd = 1UL << 4U,
        SeenMcTelemetryBase = 1UL << 5U,
        SeenMcTelemetryLoc = 1UL << 6U,
        SeenMcTelemetryEnv = 1UL << 7U,
        SeenMcAdvertLocation = 1UL << 8U,
        SeenModuleFormat = 1UL << 9U,
        SeenModuleSize = 1UL << 10U,
        SeenModuleChunkCount = 1UL << 11U,
        SeenModuleChunk0 = 1UL << 12U,
        SeenModuleChunk1 = 1UL << 13U,
        SeenModuleChunk2 = 1UL << 14U,
    };

    static constexpr uint32_t kRequired = SeenMtEnabled | SeenMtMode | SeenMtPin | SeenMcPin |
                                          SeenMcManualAdd | SeenMcTelemetryBase | SeenMcTelemetryLoc |
                                          SeenMcTelemetryEnv | SeenMcAdvertLocation | SeenModuleFormat |
                                          SeenModuleSize | SeenModuleChunkCount | SeenModuleChunk0 |
                                          SeenModuleChunk1 | SeenModuleChunk2;

    uint32_t seen = 0U;
    meshtastic_Config_BluetoothConfig mt_ble = meshtastic_Config_BluetoothConfig_init_zero;
    uint32_t mc_pin = 0U;
    bool mc_manual_add = false;
    uint8_t mc_telemetry_base = 0U;
    uint8_t mc_telemetry_loc = 0U;
    uint8_t mc_telemetry_env = 0U;
    uint8_t mc_advert_location = 0U;
    char module_format[8] = {};
    uint16_t module_size = 0U;
    uint8_t module_chunk_count = 0U;
    std::size_t module_chunk_lengths[kModuleChunkCount] = {};
    uint8_t module_bytes[meshtastic_LocalModuleConfig_size] = {};
    meshtastic_LocalModuleConfig module = meshtastic_LocalModuleConfig_init_zero;

    bool mark_once(uint32_t bit)
    {
        if ((seen & bit) != 0U)
        {
            return false;
        }
        seen |= bit;
        return true;
    }

    static bool key_equals(const tms::RecordReader& reader, const char* key)
    {
        return std::strcmp(reader.key(), key) == 0;
    }

    static bool valid_mc_pin(uint32_t pin)
    {
        return pin == 0U || (pin >= 100000U && pin <= 999999U);
    }

    bool read_mt_mode(const tms::RecordReader& reader)
    {
        uint8_t mode = 0U;
        if (!reader.u8(&mode) ||
            !phone::meshtastic::config_bridge::isValidBluetoothMode(mode))
        {
            return false;
        }
        mt_ble.mode = static_cast<meshtastic_Config_BluetoothConfig_PairingMode>(mode);
        return true;
    }

    bool decode_module()
    {
        if (std::strcmp(module_format, "pb-v1") != 0 ||
            module_size == 0U ||
            module_size > sizeof(module_bytes) ||
            module_chunk_count != kModuleChunkCount)
        {
            return false;
        }
        std::size_t expected_offset = 0U;
        for (std::size_t index = 0U; index < kModuleChunkCount; ++index)
        {
            const std::size_t expected =
                module_size > expected_offset
                    ? std::min(kModuleChunkBytes, static_cast<std::size_t>(module_size) - expected_offset)
                    : 0U;
            if (module_chunk_lengths[index] != expected)
            {
                return false;
            }
            expected_offset += expected;
        }
        std::memset(&module, 0, sizeof(module));
        pb_istream_t stream =
            pb_istream_from_buffer(module_bytes, module_size);
        if (!pb_decode(&stream, meshtastic_LocalModuleConfig_fields, &module) ||
            module.version != phone::meshtastic::defaults::kModuleConfigVersion)
        {
            return false;
        }
        phone::meshtastic::config_bridge::normalizeModuleConfig(&module);
        return true;
    }

    bool valid_state()
    {
        if ((seen & kRequired) != kRequired ||
            !phone::meshtastic::config_bridge::isValidBluetoothMode(
                static_cast<uint8_t>(mt_ble.mode)) ||
            !phone::meshtastic::config_bridge::isValidBlePin(mt_ble.fixed_pin) ||
            !valid_mc_pin(mc_pin) || mc_telemetry_base > 3U ||
            mc_telemetry_loc > 3U || mc_telemetry_env > 3U)
        {
            return false;
        }
        if (mt_ble.mode == meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN &&
            mt_ble.fixed_pin != 0U)
        {
            return false;
        }
        return decode_module();
    }

    bool encode_module()
    {
        pb_ostream_t stream = pb_ostream_from_buffer(module_bytes,
                                                     sizeof(module_bytes));
        if (!pb_encode(&stream, meshtastic_LocalModuleConfig_fields, &module) ||
            stream.bytes_written == 0U || stream.bytes_written > sizeof(module_bytes))
        {
            return false;
        }
        module_size = static_cast<uint16_t>(stream.bytes_written);
        module_chunk_count = kModuleChunkCount;
        std::snprintf(module_format, sizeof(module_format), "%s", "pb-v1");
        for (std::size_t index = 0U; index < kModuleChunkCount; ++index)
        {
            const std::size_t offset = index * kModuleChunkBytes;
            module_chunk_lengths[index] =
                module_size > offset
                    ? std::min(kModuleChunkBytes,
                               static_cast<std::size_t>(module_size) - offset)
                    : 0U;
        }
        return true;
    }

    tms::RecordConsumeResult consumeRecord(const tms::RecordReader& reader)
    {
        if (key_equals(reader, "mt_ble.enabled"))
        {
            return mark_once(SeenMtEnabled) && reader.boolean(&mt_ble.enabled)
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        if (key_equals(reader, "mt_ble.mode"))
        {
            return mark_once(SeenMtMode) && read_mt_mode(reader)
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        if (key_equals(reader, "mt_ble.fixed_pin"))
        {
            return mark_once(SeenMtPin) && reader.u32(&mt_ble.fixed_pin)
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        if (key_equals(reader, "mc_ble.pin"))
        {
            return mark_once(SeenMcPin) && reader.u32(&mc_pin)
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        if (key_equals(reader, "mc_ble.manual_add_contacts"))
        {
            return mark_once(SeenMcManualAdd) && reader.boolean(&mc_manual_add)
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        if (key_equals(reader, "mc_ble.telemetry_base"))
        {
            return mark_once(SeenMcTelemetryBase) && reader.u8(&mc_telemetry_base, 3U)
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        if (key_equals(reader, "mc_ble.telemetry_location"))
        {
            return mark_once(SeenMcTelemetryLoc) && reader.u8(&mc_telemetry_loc, 3U)
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        if (key_equals(reader, "mc_ble.telemetry_environment"))
        {
            return mark_once(SeenMcTelemetryEnv) && reader.u8(&mc_telemetry_env, 3U)
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        if (key_equals(reader, "mc_ble.advert_location_policy"))
        {
            return mark_once(SeenMcAdvertLocation) && reader.u8(&mc_advert_location)
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        if (key_equals(reader, "mt_module.format"))
        {
            return mark_once(SeenModuleFormat) &&
                           reader.text(module_format, sizeof(module_format))
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        if (key_equals(reader, "mt_module.size"))
        {
            return mark_once(SeenModuleSize) &&
                           reader.u16(&module_size,
                                      static_cast<uint16_t>(sizeof(module_bytes)))
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        if (key_equals(reader, "mt_module.chunk_count"))
        {
            return mark_once(SeenModuleChunkCount) &&
                           reader.u8(&module_chunk_count,
                                     static_cast<uint8_t>(kModuleChunkCount))
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        for (std::size_t index = 0U; index < kModuleChunkCount; ++index)
        {
            char key[32]{};
            std::snprintf(key, sizeof(key), "mt_module.chunk.%u", static_cast<unsigned>(index));
            if (key_equals(reader, key))
            {
                const uint32_t bit = SeenModuleChunk0 << index;
                return mark_once(bit) &&
                               reader.blob(module_bytes + index * kModuleChunkBytes,
                                           std::min(kModuleChunkBytes, sizeof(module_bytes) - index * kModuleChunkBytes),
                                           &module_chunk_lengths[index])
                           ? tms::RecordConsumeResult::Accepted
                           : tms::RecordConsumeResult::Invalid;
            }
        }
        return tms::RecordConsumeResult::Unhandled;
    }

    bool writeRecords(tms::RecordWriter& writer)
    {
        if (!encode_module()) return false;
        bool ok = writer.boolean("mt_ble.enabled", mt_ble.enabled) &&
                  writer.u8("mt_ble.mode", static_cast<uint8_t>(mt_ble.mode)) &&
                  writer.u32("mt_ble.fixed_pin", mt_ble.fixed_pin) &&
                  writer.u32("mc_ble.pin", mc_pin) &&
                  writer.boolean("mc_ble.manual_add_contacts", mc_manual_add) &&
                  writer.u8("mc_ble.telemetry_base", mc_telemetry_base) &&
                  writer.u8("mc_ble.telemetry_location", mc_telemetry_loc) &&
                  writer.u8("mc_ble.telemetry_environment", mc_telemetry_env) &&
                  writer.u8("mc_ble.advert_location_policy", mc_advert_location) &&
                  writer.text("mt_module.format", module_format) &&
                  writer.u16("mt_module.size", module_size) &&
                  writer.u8("mt_module.chunk_count", module_chunk_count);
        for (std::size_t index = 0U; ok && index < kModuleChunkCount; ++index)
        {
            char key[32]{};
            std::snprintf(key, sizeof(key), "mt_module.chunk.%u", static_cast<unsigned>(index));
            ok = writer.blob(key,
                             module_bytes + index * kModuleChunkBytes,
                             module_chunk_lengths[index]);
        }
        return ok;
    }
};
static_assert(sizeof(CompatibilityRecords) <= 3U * 1024U,
              "TMS compatibility staging must remain bounded");
} // namespace app::tms
