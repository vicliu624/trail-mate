#pragma once

namespace platform::ui::network
{
struct Status
{
    bool supported = false;
    // An active non-loopback IP link. This does not prove Internet reachability.
    bool connected = false;
};
// Cached snapshot; transport ownership and link discovery belong to the platform.
Status status();
} // namespace platform::ui::network
