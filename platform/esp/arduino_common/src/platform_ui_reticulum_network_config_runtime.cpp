#include "platform/ui/reticulum_network_config_runtime.h"

#include "platform/esp/arduino_common/storage/sd_card_runtime.h"

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

#include "cJSON.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace platform::ui::reticulum_network_config
{
namespace
{

using ::platform::esp::arduino_common::storage::SdRuntimeFile;
using NetworkConfig = chat::reticulum::ReticulumNetworkConfig;
using InterfaceConfig = chat::reticulum::NetworkInterfaceConfig;

constexpr const char* kWorkingConfigPath = "/trailmate/config.tms";
constexpr const char* kLegacyConfigPath = "/trailmate/reticulum/config.json";
constexpr const char* kLegacyTempPath = "/trailmate/reticulum/config.tmp";
constexpr const char* kLegacySchema = "trail-mate.reticulum";
constexpr std::size_t kLegacyMaxBytes = 2U * 1024U;
constexpr const char* kDefaultTcpHosts[] = {
    "sydney.reticulum.au", "node.reticulumnet.nl", "rmap.world"};

// This is the one long-lived Reticulum network configuration. It is allocated
// in PSRAM because the Reticulum packet path reads it throughout normal
// operation. No JSON document or second working configuration remains live.
NetworkConfig* s_active = nullptr;
Status s_status{};
bool s_initialized = false;
bool s_has_explicit_config = false;

void copy_text(char* out, std::size_t out_len, const char* value)
{
    if (!out || out_len == 0U)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", value ? value : "");
}

void set_status(const char* message, const char* detail = kWorkingConfigPath)
{
    copy_text(s_status.message, sizeof(s_status.message), message);
    copy_text(s_status.detail, sizeof(s_status.detail), detail);
    s_status.reload_deferred = false;
}

bool ensure_active()
{
    if (s_active)
    {
        return true;
    }
#if defined(ESP_PLATFORM)
    void* const raw = heap_caps_malloc(sizeof(NetworkConfig),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    void* const raw = std::malloc(sizeof(NetworkConfig));
#endif
    s_active = raw ? new (raw) NetworkConfig{} : nullptr;
    if (!s_active)
    {
        s_status.supported = false;
        set_status("Reticulum config memory unavailable");
        std::printf("[Reticulum][Config] active allocation_failed memory=psram bytes=%u\n",
                    static_cast<unsigned>(sizeof(NetworkConfig)));
        return false;
    }
    s_status.supported = true;
    return true;
}

bool sd_available()
{
    return ::platform::esp::arduino_common::storage::sd_card_ready();
}

bool bounded_text(const char* value, std::size_t capacity, bool allow_empty)
{
    if (!value || !std::memchr(value, '\0', capacity))
    {
        return false;
    }
    return allow_empty || value[0] != '\0';
}

bool valid_port(uint16_t value)
{
    return value != 0U;
}

bool zero_hash(const uint8_t* hash, std::size_t length)
{
    if (!hash)
    {
        return true;
    }
    for (std::size_t index = 0U; index < length; ++index)
    {
        if (hash[index] != 0U)
        {
            return false;
        }
    }
    return true;
}

void reset_config(NetworkConfig* config)
{
    if (!config)
    {
        return;
    }
    config->version = NetworkConfig::kSchemaVersion;
    config->interface_count = 0U;
    for (auto& interface_config : config->interfaces)
    {
        interface_config = InterfaceConfig{};
    }
    config->propagation = chat::reticulum::LxmfPropagationClientConfig{};
}

bool append_default_interface(NetworkConfig* config,
                              chat::reticulum::NetworkInterfaceType type,
                              const char* id,
                              InterfaceConfig** out = nullptr)
{
    if (!config || config->interface_count >= chat::reticulum::kMaxNetworkInterfaces)
    {
        return false;
    }
    InterfaceConfig* const interface_config = &config->interfaces[config->interface_count++];
    *interface_config = InterfaceConfig{};
    interface_config->type = type;
    interface_config->enabled = true;
    std::snprintf(interface_config->id, sizeof(interface_config->id), "%s", id ? id : "");
    if (out)
    {
        *out = interface_config;
    }
    return true;
}

bool build_defaults(const chat::MeshConfig& legacy_config, NetworkConfig* out)
{
    if (!out)
    {
        return false;
    }
    reset_config(out);
    const bool allow_lora = legacy_config.reticulum_lora_enabled &&
                            legacy_config.reticulum_interface_policy !=
                                chat::ReticulumInterfacePolicy::WifiGatewayOnly;
    const bool allow_ip = legacy_config.reticulum_wifi_gateway_enabled &&
                          legacy_config.reticulum_interface_policy !=
                              chat::ReticulumInterfacePolicy::LoRaOnly;
    if (allow_lora &&
        !append_default_interface(out,
                                  chat::reticulum::NetworkInterfaceType::IntegratedLoRa,
                                  "integrated-lora"))
    {
        return false;
    }
    if (allow_ip)
    {
        if (!append_default_interface(out,
                                      chat::reticulum::NetworkInterfaceType::Auto,
                                      "local-wifi"))
        {
            return false;
        }
        if (legacy_config.reticulum_wifi_gateway_host[0] != '\0')
        {
            InterfaceConfig* tcp = nullptr;
            if (!append_default_interface(out,
                                          chat::reticulum::NetworkInterfaceType::TcpClient,
                                          "primary-tcp",
                                          &tcp))
            {
                return false;
            }
            std::snprintf(tcp->target_host,
                          sizeof(tcp->target_host),
                          "%s",
                          legacy_config.reticulum_wifi_gateway_host);
            tcp->target_port = legacy_config.reticulum_wifi_gateway_port != 0U
                                   ? legacy_config.reticulum_wifi_gateway_port
                                   : 4242U;
        }
        else
        {
            // Factory seeds only: an explicit TMS configuration (including
            // removed or disabled entries) always takes precedence.
            const auto& hosts = kDefaultTcpHosts;
            static_assert(sizeof(hosts) / sizeof(hosts[0]) <= chat::reticulum::kMaxTcpClientInterfaces);
            for (size_t i = 0; i < sizeof(hosts) / sizeof(hosts[0]); ++i)
            {
                char id[24];
                std::snprintf(id, sizeof(id), "public-tcp-%u", static_cast<unsigned>(i + 1));
                InterfaceConfig* tcp = nullptr;
                if (!append_default_interface(out, chat::reticulum::NetworkInterfaceType::TcpClient, id, &tcp))
                {
                    return false;
                }
                std::snprintf(tcp->target_host, sizeof(tcp->target_host), "%s", hosts[i]);
                tcp->target_port = 4242U;
            }
        }
    }
    return true;
}

bool validate_config(const NetworkConfig& config)
{
    if (config.version != NetworkConfig::kSchemaVersion ||
        config.interface_count > chat::reticulum::kMaxNetworkInterfaces ||
        static_cast<uint8_t>(config.propagation.delivery) >
            static_cast<uint8_t>(chat::reticulum::LxmfDeliveryPreference::Automatic) ||
        config.propagation.sync_interval_s < 60U ||
        config.propagation.sync_interval_s > 24U * 60U * 60U ||
        config.propagation.max_messages_per_sync == 0U ||
        config.propagation.max_messages_per_sync > 64U ||
        (config.propagation.automatic_node &&
         !zero_hash(config.propagation.node_hash, sizeof(config.propagation.node_hash))) ||
        (!config.propagation.automatic_node &&
         zero_hash(config.propagation.node_hash, sizeof(config.propagation.node_hash))))
    {
        return false;
    }

    uint8_t lora_count = 0U;
    uint8_t auto_count = 0U;
    uint8_t tcp_count = 0U;
    for (std::size_t index = 0U; index < config.interface_count; ++index)
    {
        const InterfaceConfig& interface_config = config.interfaces[index];
        if (!bounded_text(interface_config.id, sizeof(interface_config.id), false))
        {
            return false;
        }
        for (std::size_t prior = 0U; prior < index; ++prior)
        {
            if (std::strcmp(interface_config.id, config.interfaces[prior].id) == 0)
            {
                return false;
            }
        }
        switch (interface_config.type)
        {
        case chat::reticulum::NetworkInterfaceType::IntegratedLoRa:
            if (++lora_count > 1U)
            {
                return false;
            }
            break;
        case chat::reticulum::NetworkInterfaceType::Auto:
            if (++auto_count > 1U ||
                !bounded_text(interface_config.group_id,
                              sizeof(interface_config.group_id),
                              false) ||
                !valid_port(interface_config.discovery_port) ||
                !valid_port(interface_config.data_port))
            {
                return false;
            }
            break;
        case chat::reticulum::NetworkInterfaceType::TcpClient:
            if (++tcp_count > chat::reticulum::kMaxTcpClientInterfaces ||
                !bounded_text(interface_config.target_host,
                              sizeof(interface_config.target_host),
                              false) ||
                !valid_port(interface_config.target_port))
            {
                return false;
            }
            break;
        default:
            return false;
        }
    }
    return true;
}

void activate(Source source, bool explicit_config)
{
    s_has_explicit_config = explicit_config;
    s_status.source = source;
    s_status.valid = s_active && validate_config(*s_active);
    s_status.configured_interfaces = s_active ? s_active->interface_count : 0U;
    ++s_status.generation;
}

cJSON* object_item(cJSON* object, const char* key)
{
    return cJSON_IsObject(object) ? cJSON_GetObjectItemCaseSensitive(object, key) : nullptr;
}

const char* json_string(cJSON* object, const char* key)
{
    cJSON* const item = object_item(object, key);
    return cJSON_IsString(item) ? item->valuestring : nullptr;
}

int json_int(cJSON* object, const char* key, int fallback)
{
    cJSON* const item = object_item(object, key);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

bool json_bool(cJSON* object, const char* key, bool fallback)
{
    cJSON* const item = object_item(object, key);
    return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
}

int hex_nibble(char value)
{
    if (value >= '0' && value <= '9')
    {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f')
    {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F')
    {
        return value - 'A' + 10;
    }
    return -1;
}

bool parse_hash(const char* text, uint8_t* out, std::size_t out_len)
{
    if (!text || !out || std::strlen(text) != out_len * 2U)
    {
        return false;
    }
    for (std::size_t index = 0U; index < out_len; ++index)
    {
        const int high = hex_nibble(text[index * 2U]);
        const int low = hex_nibble(text[index * 2U + 1U]);
        if (high < 0 || low < 0)
        {
            return false;
        }
        out[index] = static_cast<uint8_t>((high << 4) | low);
    }
    return true;
}

bool parse_legacy_interface(cJSON* object,
                            InterfaceConfig* out,
                            uint8_t* lora_count,
                            uint8_t* auto_count,
                            uint8_t* tcp_count)
{
    if (!object || !out || !lora_count || !auto_count || !tcp_count)
    {
        return false;
    }
    const char* const id = json_string(object, "id");
    const char* const type = json_string(object, "type");
    if (!id || !type || id[0] == '\0' || std::strlen(id) > chat::reticulum::kInterfaceIdMaxLen)
    {
        return false;
    }
    *out = InterfaceConfig{};
    std::snprintf(out->id, sizeof(out->id), "%s", id);
    out->enabled = json_bool(object, "enabled", true);
    if (std::strcmp(type, "IntegratedLoRaInterface") == 0)
    {
        out->type = chat::reticulum::NetworkInterfaceType::IntegratedLoRa;
        return ++(*lora_count) <= 1U;
    }
    if (std::strcmp(type, "AutoInterface") == 0)
    {
        const char* const group_id = json_string(object, "group_id");
        const char* const scope = json_string(object, "discovery_scope");
        const int discovery_port = json_int(object, "discovery_port", 29716);
        const int data_port = json_int(object, "data_port", 42671);
        if (++(*auto_count) > 1U ||
            (group_id && (group_id[0] == '\0' ||
                          std::strlen(group_id) > chat::reticulum::kAutoInterfaceGroupMaxLen)) ||
            (scope && std::strcmp(scope, "link") != 0) || discovery_port <= 0 ||
            discovery_port > 65535 || data_port <= 0 || data_port > 65535)
        {
            return false;
        }
        out->type = chat::reticulum::NetworkInterfaceType::Auto;
        if (group_id)
        {
            std::snprintf(out->group_id, sizeof(out->group_id), "%s", group_id);
        }
        out->discovery_port = static_cast<uint16_t>(discovery_port);
        out->data_port = static_cast<uint16_t>(data_port);
        return true;
    }
    if (std::strcmp(type, "TCPClientInterface") == 0)
    {
        const char* const host = json_string(object, "target_host");
        const int port = json_int(object, "target_port", 4242);
        if (++(*tcp_count) > chat::reticulum::kMaxTcpClientInterfaces || !host ||
            host[0] == '\0' || std::strlen(host) > chat::reticulum::kInterfaceHostMaxLen ||
            port <= 0 || port > 65535)
        {
            return false;
        }
        out->type = chat::reticulum::NetworkInterfaceType::TcpClient;
        std::snprintf(out->target_host, sizeof(out->target_host), "%s", host);
        out->target_port = static_cast<uint16_t>(port);
        return true;
    }
    return false;
}

bool parse_legacy_document(const char* data, std::size_t length)
{
    const char* parse_end = nullptr;
    cJSON* const root = cJSON_ParseWithLengthOpts(data, length, &parse_end, false);
    while (parse_end && parse_end < data + length &&
           (*parse_end == ' ' || *parse_end == '\t' || *parse_end == '\r' ||
            *parse_end == '\n'))
    {
        ++parse_end;
    }
    if (!root || !cJSON_IsObject(root) || parse_end != data + length ||
        !s_active || std::strcmp(json_string(root, "schema") ? json_string(root, "schema") : "", kLegacySchema) != 0 ||
        json_int(root, "version", 0) != NetworkConfig::kSchemaVersion)
    {
        cJSON_Delete(root);
        return false;
    }

    cJSON* const interfaces = object_item(root, "interfaces");
    const int count = cJSON_IsArray(interfaces) ? cJSON_GetArraySize(interfaces) : 0;
    if (count <= 0 || count > static_cast<int>(chat::reticulum::kMaxNetworkInterfaces))
    {
        cJSON_Delete(root);
        return false;
    }
    reset_config(s_active);
    uint8_t lora_count = 0U;
    uint8_t auto_count = 0U;
    uint8_t tcp_count = 0U;
    for (int index = 0; index < count; ++index)
    {
        if (!parse_legacy_interface(cJSON_GetArrayItem(interfaces, index),
                                    &s_active->interfaces[index],
                                    &lora_count,
                                    &auto_count,
                                    &tcp_count))
        {
            cJSON_Delete(root);
            return false;
        }
    }
    s_active->interface_count = static_cast<uint8_t>(count);

    cJSON* const lxmf = object_item(root, "lxmf");
    cJSON* const propagation = object_item(lxmf, "propagation");
    if (lxmf && !cJSON_IsObject(lxmf))
    {
        cJSON_Delete(root);
        return false;
    }
    if (propagation)
    {
        if (!cJSON_IsObject(propagation))
        {
            cJSON_Delete(root);
            return false;
        }
        auto& out = s_active->propagation;
        out.enabled = json_bool(propagation, "enabled", out.enabled);
        out.service_enabled = json_bool(propagation, "service_enabled", out.service_enabled);
        out.sync_on_start = json_bool(propagation, "sync_on_start", out.sync_on_start);
        const char* const delivery = json_string(propagation, "delivery_method");
        if (delivery && std::strcmp(delivery, "direct") == 0)
        {
            out.delivery = chat::reticulum::LxmfDeliveryPreference::Direct;
        }
        else if (delivery && std::strcmp(delivery, "propagated") == 0)
        {
            out.delivery = chat::reticulum::LxmfDeliveryPreference::Propagated;
        }
        else if (delivery && std::strcmp(delivery, "auto") != 0)
        {
            cJSON_Delete(root);
            return false;
        }
        const char* const node = json_string(propagation, "propagation_node");
        if (node && std::strcmp(node, "auto") != 0)
        {
            if (!parse_hash(node, out.node_hash, sizeof(out.node_hash)))
            {
                cJSON_Delete(root);
                return false;
            }
            out.automatic_node = false;
        }
        else
        {
            out.automatic_node = true;
            std::memset(out.node_hash, 0, sizeof(out.node_hash));
        }
        const int interval = json_int(propagation,
                                      "sync_interval_seconds",
                                      static_cast<int>(out.sync_interval_s));
        const int maximum = json_int(propagation,
                                     "max_messages_per_sync",
                                     static_cast<int>(out.max_messages_per_sync));
        if (interval < 60 || interval > 24 * 60 * 60 || maximum < 1 || maximum > 64)
        {
            cJSON_Delete(root);
            return false;
        }
        out.sync_interval_s = static_cast<uint32_t>(interval);
        out.max_messages_per_sync = static_cast<uint8_t>(maximum);
    }
    const bool valid = validate_config(*s_active);
    cJSON_Delete(root);
    return valid;
}

char* allocate_legacy_buffer(std::size_t size)
{
#if defined(ESP_PLATFORM)
    return static_cast<char*>(
        heap_caps_malloc(size + 1U, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
    return static_cast<char*>(std::malloc(size + 1U));
#endif
}

void free_legacy_buffer(char* buffer)
{
#if defined(ESP_PLATFORM)
    heap_caps_free(buffer);
#else
    std::free(buffer);
#endif
}

} // namespace

void initialize(const chat::MeshConfig& legacy_config)
{
    if (s_initialized)
    {
        return;
    }
    if (!ensure_active())
    {
        return;
    }
    if (!s_has_explicit_config)
    {
        if (!build_defaults(legacy_config, s_active))
        {
            s_status.valid = false;
            set_status("Reticulum defaults unavailable");
            s_initialized = true;
            return;
        }
        activate(Source::Defaults, false);
        set_status("Using Reticulum defaults");
    }
    s_initialized = true;
}

void poll(const chat::MeshConfig& legacy_config)
{
    initialize(legacy_config);
}

const NetworkConfig& active()
{
    if (!ensure_active())
    {
        std::printf("[Reticulum][Config] active configuration unavailable\n");
        std::abort();
    }
    return *s_active;
}

bool validateForTms(const NetworkConfig& config)
{
    return validate_config(config);
}

bool setFromTms(const NetworkConfig& config)
{
    if (!ensure_active() || !validate_config(config))
    {
        return false;
    }
    *s_active = config;
    activate(Source::Tms, true);
    set_status("Reticulum configuration loaded from TMS");
    return true;
}

bool updateTcpEndpoint(std::size_t slot, const char* host, uint16_t port)
{
    if (!host || slot >= chat::reticulum::kMaxTcpClientInterfaces || !valid_port(port) ||
        !ensure_active() || !validate_config(*s_active)) return false;
    for (std::size_t i = 0; host[i]; ++i)
        if (i >= chat::reticulum::kInterfaceHostMaxLen || static_cast<unsigned char>(host[i]) <= 32 ||
            host[i] == '/' || host[i] == '\\') return false;
    std::size_t ordinal = 0;
    std::size_t index = 0;
    for (; index < s_active->interface_count; ++index)
    {
        if (s_active->interfaces[index].type != chat::reticulum::NetworkInterfaceType::TcpClient) continue;
        if (ordinal++ == slot) break;
    }
    const bool append = index == s_active->interface_count;
    if (append && (ordinal != slot || index >= chat::reticulum::kMaxNetworkInterfaces || !host[0])) return false;
    if (!host[0])
    {
        for (std::size_t i = index + 1; i < s_active->interface_count; ++i)
            s_active->interfaces[i - 1] = s_active->interfaces[i];
        s_active->interfaces[--s_active->interface_count] = InterfaceConfig{};
    }
    else
    {
        const InterfaceConfig previous = s_active->interfaces[index];
        auto& entry = s_active->interfaces[index];
        if (append)
        {
            entry = InterfaceConfig{};
            entry.type = chat::reticulum::NetworkInterfaceType::TcpClient;
            entry.enabled = true;
            // Select an unused ID even after earlier entries were removed.
            for (unsigned suffix = 1; suffix <= chat::reticulum::kMaxNetworkInterfaces + 1; ++suffix)
            {
                std::snprintf(entry.id, sizeof(entry.id), "user-tcp-%u", suffix);
                bool duplicate = false;
                for (std::size_t i = 0; i < index; ++i)
                    duplicate |= std::strcmp(entry.id, s_active->interfaces[i].id) == 0;
                if (!duplicate) break;
            }
            ++s_active->interface_count;
        }
        std::snprintf(entry.target_host, sizeof(entry.target_host), "%s", host);
        entry.target_port = port;
        if (!validate_config(*s_active))
        {
            entry = previous;
            if (append) --s_active->interface_count;
            return false;
        }
    }
    activate(Source::Tms, true);
    set_status("Reticulum TCP configuration updated");
    return true;
}

bool restoreDefaultTcpEndpoints()
{
    if (!ensure_active() || !validate_config(*s_active)) return false;
#if defined(ESP_PLATFORM)
    void* raw = heap_caps_malloc(sizeof(NetworkConfig), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    void* raw = std::malloc(sizeof(NetworkConfig));
#endif
    if (!raw) return false;
    auto* candidate = new (raw) NetworkConfig(*s_active);
    size_t kept = 0;
    for (size_t i = 0; i < candidate->interface_count; ++i)
    {
        if (candidate->interfaces[i].type != chat::reticulum::NetworkInterfaceType::TcpClient)
            candidate->interfaces[kept++] = candidate->interfaces[i];
    }
    candidate->interface_count = kept;
    for (size_t i = kept; i < chat::reticulum::kMaxNetworkInterfaces; ++i)
        candidate->interfaces[i] = InterfaceConfig{};
    bool valid = true;
    for (size_t i = 0; i < sizeof(kDefaultTcpHosts) / sizeof(kDefaultTcpHosts[0]); ++i)
    {
        char id[24];
        // Avoid collisions with preserved non-TCP interface IDs.
        unsigned suffix = 1;
        bool duplicate;
        do
        {
            std::snprintf(id, sizeof(id), "public-tcp-%u", suffix++);
            duplicate = false;
            for (size_t j = 0; j < candidate->interface_count; ++j)
                duplicate |= std::strcmp(id, candidate->interfaces[j].id) == 0;
        } while (duplicate);
        InterfaceConfig* tcp = nullptr;
        if (!append_default_interface(candidate, chat::reticulum::NetworkInterfaceType::TcpClient, id, &tcp))
        {
            valid = false;
            break;
        }
        std::snprintf(tcp->target_host, sizeof(tcp->target_host), "%s", kDefaultTcpHosts[i]);
        tcp->target_port = 4242;
    }
    valid = valid && validate_config(*candidate);
    if (valid) *s_active = *candidate;
    candidate->~NetworkConfig();
    std::free(raw);
    if (!valid) return false;
    activate(Source::Tms, true);
    set_status("Built-in Reticulum TCP entries restored");
    return true;
}

bool snapshotForTms(const chat::MeshConfig& legacy_config, NetworkConfig* out)
{
    if (!out || !ensure_active())
    {
        return false;
    }
    if (s_has_explicit_config)
    {
        *out = *s_active;
        return true;
    }
    return build_defaults(legacy_config, out);
}

LegacyImportResult importLegacy(const chat::MeshConfig&)
{
    if (!sd_available())
    {
        return LegacyImportResult::Unavailable;
    }
    if (!::platform::esp::arduino_common::storage::sd_exists(kLegacyConfigPath))
    {
        return LegacyImportResult::NotPresent;
    }
    if (!ensure_active())
    {
        return LegacyImportResult::Invalid;
    }

    SdRuntimeFile file;
    if (!file.open(kLegacyConfigPath, "r"))
    {
        return LegacyImportResult::Invalid;
    }
    const std::size_t size = static_cast<std::size_t>(file.size());
    if (size == 0U || size > kLegacyMaxBytes)
    {
        file.close();
        return LegacyImportResult::Invalid;
    }
    char* const buffer = allocate_legacy_buffer(size);
    if (!buffer)
    {
        file.close();
        set_status("Reticulum legacy migration memory unavailable", kLegacyConfigPath);
        return LegacyImportResult::Invalid;
    }
    const bool read = file.read_bytes(buffer, size) == size;
    file.close();
    buffer[size] = '\0';
    const bool parsed = read && parse_legacy_document(buffer, size);
    free_legacy_buffer(buffer);
    if (!parsed)
    {
        set_status("Invalid legacy Reticulum configuration", kLegacyConfigPath);
        return LegacyImportResult::Invalid;
    }
    activate(Source::LegacyJson, true);
    set_status("Legacy Reticulum configuration imported", kLegacyConfigPath);
    return LegacyImportResult::Imported;
}

bool discardLegacySource()
{
    if (!sd_available())
    {
        return false;
    }
    const bool removed_config =
        !::platform::esp::arduino_common::storage::sd_exists(kLegacyConfigPath) ||
        ::platform::esp::arduino_common::storage::sd_remove(kLegacyConfigPath);
    const bool removed_temp =
        !::platform::esp::arduino_common::storage::sd_exists(kLegacyTempPath) ||
        ::platform::esp::arduino_common::storage::sd_remove(kLegacyTempPath);
    return removed_config && removed_temp;
}

Status status()
{
    return s_status;
}

bool reload(const chat::MeshConfig& legacy_config)
{
    initialize(legacy_config);
    return s_status.valid;
}

bool export_template(const chat::MeshConfig&)
{
    set_status("Reticulum configuration is stored in config.tms");
    return false;
}

bool reset(const chat::MeshConfig& legacy_config)
{
    if (!ensure_active() || !build_defaults(legacy_config, s_active))
    {
        return false;
    }
    activate(Source::Defaults, false);
    s_initialized = true;
    set_status("Reticulum configuration reset");
    return true;
}

const char* config_path()
{
    return kWorkingConfigPath;
}

const char* source_name(Source source)
{
    switch (source)
    {
    case Source::Tms:
        return "TMS";
    case Source::LegacyJson:
        return "Legacy JSON";
    case Source::Defaults:
    default:
        return "Defaults";
    }
}

} // namespace platform::ui::reticulum_network_config
