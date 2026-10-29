#pragma once
#include "chat/domain/chat_types.h"
#include "chat/domain/reticulum_network_config.h"

namespace platform::linux_runtime
{
// Application/UI-thread binding. The owner unbinds before destruction; a
// retiring owner cannot detach a replacement application's binding.
struct ReticulumNetworkConfigBinding
{
    void* owner = nullptr;
    bool (*save)(void*, const chat::reticulum::ReticulumNetworkConfig&) = nullptr;
    bool (*pending)(void*) = nullptr;
};
void bind_reticulum_network_config(ReticulumNetworkConfigBinding,
                                   const chat::reticulum::ReticulumNetworkConfig&, bool explicit_config);
void publish_reticulum_network_config(void* owner,
                                      const chat::reticulum::ReticulumNetworkConfig&, bool explicit_config);
void unbind_reticulum_network_config(void* owner);
} // namespace platform::linux_runtime
