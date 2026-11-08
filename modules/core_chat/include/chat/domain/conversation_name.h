#pragma once

#include "chat/domain/chat_types.h"
#include <cstdio>
#include <string>

namespace chat
{
// Default metadata label when no contact name is available. Reticulum routes
// by its full destination hash; a zero numeric peer does not imply broadcast.
inline std::string fallbackConversationName(const ConversationId& conversation)
{
    if ((conversation.protocol == MeshProtocol::Reticulum || conversation.protocol == MeshProtocol::RNode) &&
        hasReticulumDestinationIdentity(conversation.reticulum_identity))
    {
        constexpr char hex[] = "0123456789ABCDEF";
        char label[3 + kReticulumPeerHashSize * 2 + 1] = "RT ";
        for (std::size_t i = 0; i < kReticulumPeerHashSize; ++i)
        {
            const auto byte = conversation.reticulum_identity.destination_hash[i];
            label[3 + i * 2] = hex[byte >> 4];
            label[4 + i * 2] = hex[byte & 0x0F];
        }
        return label;
    }
    if (conversation.peer == 0) return "Broadcast";
    char label[16]{};
    std::snprintf(label, sizeof(label), "%04lX", static_cast<unsigned long>(conversation.peer & 0xFFFFU));
    return label;
}
} // namespace chat
