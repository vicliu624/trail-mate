#pragma once
#include "chat/domain/chat_types.h"

namespace chat::reticulum
{
inline bool integratedLoRaAllowed(const MeshConfig& config)
{
    return config.reticulum_lora_enabled &&
           (config.reticulum_interface_policy == ReticulumInterfacePolicy::All ||
            config.reticulum_interface_policy == ReticulumInterfacePolicy::LoRaOnly);
}
} // namespace chat::reticulum
