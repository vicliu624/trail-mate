#pragma once

#include "app/tms_config_codec.h"
#include "chat/domain/reticulum_network_validation.h"
#include <cstdio>
#include <cstring>

namespace app::tms
{
// Non-owning bounded network extension. The platform owns scratch configuration
// and presence masks; this codec performs no storage I/O or runtime activation.
class ReticulumNetworkRecords
{
    struct View
    {
        chat::reticulum::ReticulumNetworkConfig& network;
        uint64_t& network_seen;
        uint32_t& access_seen;
    } s_state;
    static constexpr std::size_t kNetworkInterfaceFieldCount = 8U;
    static constexpr std::size_t kNetworkPropagationFieldCount = 8U;
    static constexpr std::size_t kNetworkSeenCount =
        2U + ::chat::reticulum::kMaxNetworkInterfaces * kNetworkInterfaceFieldCount +
        kNetworkPropagationFieldCount;
    static_assert(kNetworkSeenCount < 64U,
                  "TMS Reticulum network presence mask must remain a scalar");
    static constexpr uint64_t kRequiredNetwork = (UINT64_C(1) << kNetworkSeenCount) - UINT64_C(1);

    static bool key_equals(const RecordReader& reader, const char* key)
    {
        return std::strcmp(reader.key(), key) == 0;
    }

  public:
    ReticulumNetworkRecords(chat::reticulum::ReticulumNetworkConfig& network,
                            uint64_t& network_seen, uint32_t& access_seen)
        : s_state{network, network_seen, access_seen} {}

    bool mark_network_once(std::size_t index)
    {
        if (index >= kNetworkSeenCount)
        {
            return false;
        }
        const uint64_t bit = UINT64_C(1) << index;
        if ((s_state.network_seen & bit) != 0U)
        {
            return false;
        }
        s_state.network_seen |= bit;
        return true;
    }

    bool read_network_interface_type(const tms::RecordReader& reader, std::size_t index)
    {
        uint8_t value = 0U;
        if (index >= ::chat::reticulum::kMaxNetworkInterfaces || !reader.u8(&value, 2U))
        {
            return false;
        }
        s_state.network.interfaces[index].type =
            static_cast<::chat::reticulum::NetworkInterfaceType>(value);
        return true;
    }

    bool read_network_delivery(const tms::RecordReader& reader)
    {
        uint8_t value = 0U;
        if (!reader.u8(&value, 2U))
        {
            return false;
        }
        s_state.network.propagation.delivery =
            static_cast<::chat::reticulum::LxmfDeliveryPreference>(value);
        return true;
    }

    tms::RecordConsumeResult consume_network_record(const tms::RecordReader& reader)
    {
        if (key_equals(reader, "rt.net.version"))
        {
            uint16_t version = 0U;
            return mark_network_once(0U) && reader.u16(&version) && version == 1U
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        if (key_equals(reader, "rt.net.interface_count"))
        {
            return mark_network_once(1U) &&
                           reader.u8(&s_state.network.interface_count,
                                     static_cast<uint8_t>(::chat::reticulum::kMaxNetworkInterfaces))
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }

        for (std::size_t index = 0U; index < ::chat::reticulum::kMaxNetworkInterfaces; ++index)
        {
            const std::size_t base = 2U + index * kNetworkInterfaceFieldCount;
            char key[48]{};
            const char* access_fields[] = {"network_name", "passphrase", "ifac_size"};
            for (unsigned field = 0; field < 3; ++field)
            {
                std::snprintf(key, sizeof(key), "rt.net.interface.%u.%s", static_cast<unsigned>(index), access_fields[field]);
                if (!key_equals(reader, key)) continue;
                const uint32_t bit = 1U << (index * 3U + field);
                if (s_state.access_seen & bit) return tms::RecordConsumeResult::Invalid;
                s_state.access_seen |= bit;
                auto& access = s_state.network.interfaces[index].access;
                const bool valid = field == 0   ? reader.text(access.network_name, sizeof(access.network_name))
                                   : field == 1 ? reader.text(access.passphrase, sizeof(access.passphrase))
                                                : reader.u16(&access.ifac_size_bits, 512U);
                return valid ? tms::RecordConsumeResult::Accepted : tms::RecordConsumeResult::Invalid;
            }
            std::snprintf(key, sizeof(key), "rt.net.interface.%u.id", static_cast<unsigned>(index));
            if (key_equals(reader, key))
            {
                return mark_network_once(base) &&
                               reader.text(s_state.network.interfaces[index].id,
                                           sizeof(s_state.network.interfaces[index].id))
                           ? tms::RecordConsumeResult::Accepted
                           : tms::RecordConsumeResult::Invalid;
            }
            std::snprintf(key, sizeof(key), "rt.net.interface.%u.type", static_cast<unsigned>(index));
            if (key_equals(reader, key))
            {
                return mark_network_once(base + 1U) && read_network_interface_type(reader, index)
                           ? tms::RecordConsumeResult::Accepted
                           : tms::RecordConsumeResult::Invalid;
            }
            std::snprintf(key, sizeof(key), "rt.net.interface.%u.enabled", static_cast<unsigned>(index));
            if (key_equals(reader, key))
            {
                return mark_network_once(base + 2U) &&
                               reader.boolean(&s_state.network.interfaces[index].enabled)
                           ? tms::RecordConsumeResult::Accepted
                           : tms::RecordConsumeResult::Invalid;
            }
            std::snprintf(key, sizeof(key), "rt.net.interface.%u.target_host", static_cast<unsigned>(index));
            if (key_equals(reader, key))
            {
                return mark_network_once(base + 3U) &&
                               reader.text(s_state.network.interfaces[index].target_host,
                                           sizeof(s_state.network.interfaces[index].target_host))
                           ? tms::RecordConsumeResult::Accepted
                           : tms::RecordConsumeResult::Invalid;
            }
            std::snprintf(key, sizeof(key), "rt.net.interface.%u.target_port", static_cast<unsigned>(index));
            if (key_equals(reader, key))
            {
                return mark_network_once(base + 4U) &&
                               reader.u16(&s_state.network.interfaces[index].target_port)
                           ? tms::RecordConsumeResult::Accepted
                           : tms::RecordConsumeResult::Invalid;
            }
            std::snprintf(key, sizeof(key), "rt.net.interface.%u.group_id", static_cast<unsigned>(index));
            if (key_equals(reader, key))
            {
                return mark_network_once(base + 5U) &&
                               reader.text(s_state.network.interfaces[index].group_id,
                                           sizeof(s_state.network.interfaces[index].group_id))
                           ? tms::RecordConsumeResult::Accepted
                           : tms::RecordConsumeResult::Invalid;
            }
            std::snprintf(key,
                          sizeof(key),
                          "rt.net.interface.%u.discovery_port",
                          static_cast<unsigned>(index));
            if (key_equals(reader, key))
            {
                return mark_network_once(base + 6U) &&
                               reader.u16(&s_state.network.interfaces[index].discovery_port)
                           ? tms::RecordConsumeResult::Accepted
                           : tms::RecordConsumeResult::Invalid;
            }
            std::snprintf(key, sizeof(key), "rt.net.interface.%u.data_port", static_cast<unsigned>(index));
            if (key_equals(reader, key))
            {
                return mark_network_once(base + 7U) &&
                               reader.u16(&s_state.network.interfaces[index].data_port)
                           ? tms::RecordConsumeResult::Accepted
                           : tms::RecordConsumeResult::Invalid;
            }
        }

        constexpr std::size_t kPropagationBase =
            2U + ::chat::reticulum::kMaxNetworkInterfaces * kNetworkInterfaceFieldCount;
        if (key_equals(reader, "rt.net.propagation.enabled"))
        {
            return mark_network_once(kPropagationBase) &&
                           reader.boolean(&s_state.network.propagation.enabled)
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        if (key_equals(reader, "rt.net.propagation.service_enabled"))
        {
            return mark_network_once(kPropagationBase + 1U) &&
                           reader.boolean(&s_state.network.propagation.service_enabled)
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        if (key_equals(reader, "rt.net.propagation.delivery"))
        {
            return mark_network_once(kPropagationBase + 2U) && read_network_delivery(reader)
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        if (key_equals(reader, "rt.net.propagation.automatic_node"))
        {
            return mark_network_once(kPropagationBase + 3U) &&
                           reader.boolean(&s_state.network.propagation.automatic_node)
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        if (key_equals(reader, "rt.net.propagation.node_hash"))
        {
            std::size_t length = 0U;
            return mark_network_once(kPropagationBase + 4U) &&
                           reader.blob(s_state.network.propagation.node_hash,
                                       sizeof(s_state.network.propagation.node_hash),
                                       &length) &&
                           length == sizeof(s_state.network.propagation.node_hash)
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        if (key_equals(reader, "rt.net.propagation.sync_on_start"))
        {
            return mark_network_once(kPropagationBase + 5U) &&
                           reader.boolean(&s_state.network.propagation.sync_on_start)
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        if (key_equals(reader, "rt.net.propagation.sync_interval_s"))
        {
            return mark_network_once(kPropagationBase + 6U) &&
                           reader.u32(&s_state.network.propagation.sync_interval_s)
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        if (key_equals(reader, "rt.net.propagation.max_messages_per_sync"))
        {
            return mark_network_once(kPropagationBase + 7U) &&
                           reader.u8(&s_state.network.propagation.max_messages_per_sync, 64U)
                       ? tms::RecordConsumeResult::Accepted
                       : tms::RecordConsumeResult::Invalid;
        }
        return tms::RecordConsumeResult::Unhandled;
    }

    bool valid_network_records(uint16_t schema_version)
    {
        for (std::size_t index = 0; index < ::chat::reticulum::kMaxNetworkInterfaces; ++index)
        {
            const auto fields = (s_state.access_seen >> (index * 3U)) & 7U;
            if (fields && (fields != 7U || index >= s_state.network.interface_count)) return false;
        }
        if (s_state.network_seen == 0U)
        {
            return schema_version != tms::kSchemaVersion;
        }
        return s_state.network_seen == kRequiredNetwork &&
               ::chat::reticulum::validateNetworkConfig(s_state.network);
    }

    bool write_network_records(tms::RecordWriter& writer)
    {
        if (!writer.u16("rt.net.version", 1U) ||
            !writer.u8("rt.net.interface_count", s_state.network.interface_count))
        {
            return false;
        }
        for (std::size_t index = 0U; index < ::chat::reticulum::kMaxNetworkInterfaces; ++index)
        {
            const auto& interface_config = s_state.network.interfaces[index];
            char key[48]{};
            std::snprintf(key, sizeof(key), "rt.net.interface.%u.id", static_cast<unsigned>(index));
            if (!writer.text(key, interface_config.id))
            {
                return false;
            }
            std::snprintf(key, sizeof(key), "rt.net.interface.%u.type", static_cast<unsigned>(index));
            if (!writer.u8(key, static_cast<uint8_t>(interface_config.type)))
            {
                return false;
            }
            std::snprintf(key, sizeof(key), "rt.net.interface.%u.enabled", static_cast<unsigned>(index));
            if (!writer.boolean(key, interface_config.enabled))
            {
                return false;
            }
            std::snprintf(key, sizeof(key), "rt.net.interface.%u.target_host", static_cast<unsigned>(index));
            if (!writer.text(key, interface_config.target_host))
            {
                return false;
            }
            std::snprintf(key, sizeof(key), "rt.net.interface.%u.target_port", static_cast<unsigned>(index));
            if (!writer.u16(key, interface_config.target_port))
            {
                return false;
            }
            std::snprintf(key, sizeof(key), "rt.net.interface.%u.group_id", static_cast<unsigned>(index));
            if (!writer.text(key, interface_config.group_id))
            {
                return false;
            }
            std::snprintf(key,
                          sizeof(key),
                          "rt.net.interface.%u.discovery_port",
                          static_cast<unsigned>(index));
            if (!writer.u16(key, interface_config.discovery_port))
            {
                return false;
            }
            std::snprintf(key, sizeof(key), "rt.net.interface.%u.data_port", static_cast<unsigned>(index));
            if (!writer.u16(key, interface_config.data_port))
            {
                return false;
            }
            if (interface_config.access.enabled())
            {
                std::snprintf(key, sizeof(key), "rt.net.interface.%u.network_name", static_cast<unsigned>(index));
                if (!writer.text(key, interface_config.access.network_name)) return false;
                std::snprintf(key, sizeof(key), "rt.net.interface.%u.passphrase", static_cast<unsigned>(index));
                if (!writer.text(key, interface_config.access.passphrase)) return false;
                std::snprintf(key, sizeof(key), "rt.net.interface.%u.ifac_size", static_cast<unsigned>(index));
                if (!writer.u16(key, interface_config.access.ifac_size_bits)) return false;
            }
        }
        const auto& propagation = s_state.network.propagation;
        return writer.boolean("rt.net.propagation.enabled", propagation.enabled) &&
               writer.boolean("rt.net.propagation.service_enabled", propagation.service_enabled) &&
               writer.u8("rt.net.propagation.delivery",
                         static_cast<uint8_t>(propagation.delivery)) &&
               writer.boolean("rt.net.propagation.automatic_node", propagation.automatic_node) &&
               writer.blob("rt.net.propagation.node_hash",
                           propagation.node_hash,
                           sizeof(propagation.node_hash)) &&
               writer.boolean("rt.net.propagation.sync_on_start", propagation.sync_on_start) &&
               writer.u32("rt.net.propagation.sync_interval_s", propagation.sync_interval_s) &&
               writer.u8("rt.net.propagation.max_messages_per_sync",
                         propagation.max_messages_per_sync);
    }
};
} // namespace app::tms
