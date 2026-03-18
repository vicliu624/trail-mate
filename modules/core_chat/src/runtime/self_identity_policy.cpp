#include "chat/runtime/self_identity_policy.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace chat::runtime
{
namespace
{

size_t copyPrintableAscii(const char* input, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return 0;
    }

    out[0] = '\0';
    if (!input || input[0] == '\0')
    {
        return 0;
    }

    size_t written = 0;
    for (const char* cursor = input; *cursor != '\0' && written + 1 < out_len; ++cursor)
    {
        const unsigned char ch = static_cast<unsigned char>(*cursor);
        if (ch >= 0x20U && ch <= 0x7EU)
        {
            out[written++] = static_cast<char>(ch);
        }
    }
    out[written] = '\0';
    return written;
}

void trimTrailingSpaces(char* text)
{
    if (!text)
    {
        return;
    }

    size_t len = std::strlen(text);
    while (len > 0 && std::isspace(static_cast<unsigned char>(text[len - 1])) != 0)
    {
        text[--len] = '\0';
    }
}

} // namespace

void formatCompactNodeId(NodeId node_id, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }

    const uint16_t short_id = static_cast<uint16_t>(node_id & 0xFFFFU);
    std::snprintf(out, out_len, "%04X", static_cast<unsigned>(short_id));
}

void formatScreenNodeLabel(NodeId node_id, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }

    char compact[8] = {};
    formatCompactNodeId(node_id, compact, sizeof(compact));
    std::snprintf(out, out_len, "#%s", compact);
}

bool resolveEffectiveSelfIdentity(const SelfIdentityInput& input,
                                  EffectiveSelfIdentity* out_identity)
{
    if (!out_identity)
    {
        return false;
    }

    *out_identity = EffectiveSelfIdentity{};
    out_identity->node_id = input.node_id;

    char long_name[sizeof(out_identity->long_name)] = {};
    char short_name[sizeof(out_identity->short_name)] = {};
    char short_hex[8] = {};
    formatCompactNodeId(input.node_id, short_hex, sizeof(short_hex));

    const size_t long_len = copyPrintableAscii(input.configured_long_name,
                                               long_name,
                                               sizeof(long_name));
    size_t short_len = copyPrintableAscii(input.configured_short_name,
                                          short_name,
                                          sizeof(short_name));

    if (long_len == 0)
    {
        const char* prefix = (input.fallback_long_prefix && input.fallback_long_prefix[0] != '\0')
                                 ? input.fallback_long_prefix
                                 : "node";
        std::snprintf(long_name, sizeof(long_name), "%s-%s", prefix, short_hex);
    }

    if (short_len == 0 && input.allow_short_hex_fallback)
    {
        std::snprintf(short_name, sizeof(short_name), "%s", short_hex);
        short_len = std::strlen(short_name);
    }

    if (short_len > 4)
    {
        short_name[4] = '\0';
    }

    trimTrailingSpaces(long_name);
    trimTrailingSpaces(short_name);

    std::memcpy(out_identity->long_name, long_name, sizeof(out_identity->long_name));
    std::memcpy(out_identity->short_name, short_name, sizeof(out_identity->short_name));

    const char* ble_prefix = (input.fallback_ble_prefix && input.fallback_ble_prefix[0] != '\0')
                                 ? input.fallback_ble_prefix
                                 : "node";
    if (out_identity->long_name[0] != '\0')
    {
        std::snprintf(out_identity->ble_name,
                      sizeof(out_identity->ble_name),
                      "%s",
                      out_identity->long_name);
    }
    else
    {
        std::snprintf(out_identity->ble_name,
                      sizeof(out_identity->ble_name),
                      "%s-%s",
                      ble_prefix,
                      short_hex);
    }

    return out_identity->long_name[0] != '\0';
}

void buildBleVisibleName(const EffectiveSelfIdentity& identity,
                         MeshProtocol protocol,
                         char* out,
                         size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }

    out[0] = '\0';
    char compact[8] = {};
    formatCompactNodeId(identity.node_id, compact, sizeof(compact));

    if (protocol == MeshProtocol::Meshtastic)
    {
        std::snprintf(out, out_len, "Meshtastic_%s", compact);
        return;
    }

    const char* base = identity.short_name[0] != '\0'
                           ? identity.short_name
                           : (identity.long_name[0] != '\0' ? identity.long_name : compact);
    std::snprintf(out, out_len, "MeshCore-%s", base);
}

} // namespace chat::runtime
