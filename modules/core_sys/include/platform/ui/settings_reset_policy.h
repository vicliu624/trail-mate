#pragma once

namespace platform::ui::settings_reset
{
// Settings-owned namespaces shared by the factory-reset UI and platform
// repositories. Message/route databases are managed by their own UI actions.
inline constexpr const char* kLegacyNamespaces[] = {
    "chat", "gps", "settings", "aprs", "power", "a7682e",
    "chat_pki", "mc_peers", "lxmf_peers"};
} // namespace platform::ui::settings_reset
