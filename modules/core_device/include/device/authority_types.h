#pragma once

#include <cstdint>

#include "device/host_types.h"

namespace device
{

enum class AuthorityKind : std::uint8_t
{
    None,
    Identity,
    PeerKeyStore,
    NodeStore,
    MessageStore,
    Location,
    Time,
    Config,
    DeviceStatus,
    UiState,
};

struct AuthorityBinding
{
    AuthorityKind authority = AuthorityKind::None;
    HostKind owner = HostKind::None;
};

} // namespace device
