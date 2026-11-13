/*
 * Reticulum License - RNS/LXMF-derived portions.
 * Derived from https://github.com/markqvist/LXMF
 * Copyright (c) 2020-2026 Mark Qvist (LXMF-derived portions).
 * Full license: LICENSES/Reticulum-Derivatives.txt.
 * Original notices: LICENSES/Reticulum-RNS.txt and LICENSES/Reticulum-LXMF.txt.
 * Other contributions retain their applicable terms; see LICENSE.
 * Component scope: docs/licensing/reticulum-sources.csv.
 */

#pragma once
#include "chat/ports/stored_identity_lease.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_identity.h"
#include <esp_heap_caps.h>

namespace chat::lxmf
{
struct LocalIdentityMemory
{
    static void* allocate(size_t size) { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
    static void release(void* pointer) { heap_caps_free(pointer); }
};
using LocalIdentityLease = StoredIdentityLease<LxmfIdentity, LocalIdentityMemory>;
} // namespace chat::lxmf
