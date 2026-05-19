#pragma once

#include <cstddef>

#include "device/authority_types.h"
#include "device/board_types.h"
#include "device/capability_types.h"
#include "device/platform_types.h"
#include "device/runtime_types.h"
#include "device/ui_types.h"

namespace device
{

struct TargetId
{
    const char* value = "";
};

struct ProductDescriptor
{
    const char* family = "";
    const char* form_factor = "";
    const char* interaction_class = "";
};

struct PlatformDescriptor
{
    PlatformKind kind = PlatformKind::None;
    const char* sdk = "";
    const char* memory_tier = "";
    const char* filesystem = "";
    const char* dynamic_allocation = "";
};

struct CapabilityBindingRef
{
    CapabilityKind capability = CapabilityKind::None;
    const char* board_provider = "";
    const char* platform_driver = "";
    const char* runtime_owner = "";
};

struct TargetManifestView
{
    TargetId target{};
    ProductDescriptor product{};
    PlatformDescriptor platform{};
    BoardPackageRef board{};
    RuntimePolicy runtime{};
    UiBinding ui{};
    const AuthorityBinding* authorities = nullptr;
    std::size_t authority_count = 0;
    const CapabilityStatus* capabilities = nullptr;
    std::size_t capability_count = 0;
    const CapabilityBindingRef* capability_bindings = nullptr;
    std::size_t capability_binding_count = 0;
};

} // namespace device
