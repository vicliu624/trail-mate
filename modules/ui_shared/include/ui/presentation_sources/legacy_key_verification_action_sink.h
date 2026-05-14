#pragma once

#include "ui/presentation_sources/legacy_key_verification_session.h"
#include "ui_presentation/key_verification/key_verification_action_sink.h"

namespace chat
{
class IMeshAdapter;
namespace contacts
{
class ContactService;
}
} // namespace chat

namespace ui::presentation_sources
{

// LegacyKeyVerificationActionSink is a Phase 7.5 anti-corruption bridge.
//
// Pattern:
//   Command Sink / Legacy Runtime Adapter.
//
// It translates portable key verification actions into existing mesh/contact
// runtime calls. It must not render UI, build snapshots, or expose raw key
// material to controllers.
class LegacyKeyVerificationActionSink final
    : public ::ui::key_verification::IKeyVerificationActionSink
{
  public:
    LegacyKeyVerificationActionSink(
        LegacyKeyVerificationSession& session,
        ::chat::IMeshAdapter* mesh = nullptr,
        ::chat::contacts::ContactService* contacts = nullptr);

    ::ui::UiActionResult accept(uint32_t peer_id) override;
    ::ui::UiActionResult reject(uint32_t peer_id) override;
    ::ui::UiActionResult refresh(uint32_t peer_id) override;
    ::ui::UiActionResult copyCode(uint32_t peer_id) override;
    ::ui::UiActionResult submitNumber(uint32_t peer_id,
                                      uint32_t number) override;

  private:
    bool sessionMatches(uint32_t peer_id) const;

    LegacyKeyVerificationSession& session_;
    ::chat::IMeshAdapter* mesh_ = nullptr;
    ::chat::contacts::ContactService* contacts_ = nullptr;
};

} // namespace ui::presentation_sources
