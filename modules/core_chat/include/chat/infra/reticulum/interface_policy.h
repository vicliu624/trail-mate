/*
 * Reticulum License - RNS/LXMF-derived portions.
 * Derived from https://github.com/markqvist/Reticulum
 * Copyright (c) 2016-2026 Mark Qvist (Reticulum-derived portions).
 * Full license: LICENSES/Reticulum-Derivatives.txt.
 * Original notices: LICENSES/Reticulum-RNS.txt and LICENSES/Reticulum-LXMF.txt.
 * Other contributions retain their applicable terms; see LICENSE.
 * Component scope: docs/licensing/reticulum-sources.csv.
 */

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
