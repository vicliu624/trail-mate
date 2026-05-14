#pragma once

#include "ui/presentation_sources/legacy_key_verification_session.h"
#include "ui_presentation/key_verification/key_verification_source.h"

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

// LegacyKeyVerificationSource is a Phase 7.5 anti-corruption bridge.
//
// Pattern:
//   Read Projection / Legacy Runtime Adapter.
//
// It consumes existing key verification events and projects a portable
// KeyVerificationSnapshot. It must not render UI, submit numbers, trust keys,
// or expose raw protocol payloads to ChatUiController.
class LegacyKeyVerificationSource final
    : public ::ui::key_verification::IKeyVerificationPresentationSource
{
  public:
    LegacyKeyVerificationSource(
        LegacyKeyVerificationSession& session,
        const ::chat::contacts::ContactService* contacts = nullptr,
        const ::chat::IMeshAdapter* mesh = nullptr);

    bool buildKeyVerificationSnapshot(
        const ::ui::key_verification::KeyVerificationRequest& request,
        ::ui::key_verification::KeyVerificationSnapshot& out) const override;

    void onNumberRequest(uint32_t peer_id, uint64_t nonce);
    void onNumberInform(uint32_t peer_id,
                        uint64_t nonce,
                        uint32_t security_number);
    void onFinal(uint32_t peer_id,
                 uint64_t nonce,
                 bool is_sender,
                 const char* verification_code);
    void onPeerKeyMissing(uint32_t peer_id);
    void clear();

  private:
    ::ui::key_verification::VerificationProtocol currentProtocol() const;
    void copyPeerLabel(uint32_t peer_id,
                       ::ui::key_verification::KeyVerificationSnapshot& out) const;
    void copyStatusLine(
        ::ui::key_verification::KeyVerificationSnapshot& out) const;

    LegacyKeyVerificationSession& session_;
    const ::chat::contacts::ContactService* contacts_ = nullptr;
    const ::chat::IMeshAdapter* mesh_ = nullptr;
};

} // namespace ui::presentation_sources
