#pragma once

#include "ui_key_verification_runtime/key_verification_session_adapter.h"
#include "ui_presentation/key_verification/key_verification_source.h"

namespace chat
{
class IMeshAdapter;
namespace contacts
{
class ContactService;
}
} // namespace chat

namespace ui_key_verification_runtime
{

class KeyVerificationPresentationSource final
    : public ::ui::key_verification::IKeyVerificationPresentationSource
{
  public:
    KeyVerificationPresentationSource(
        KeyVerificationSessionAdapter& session,
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

    KeyVerificationSessionAdapter& session_;
    const ::chat::contacts::ContactService* contacts_ = nullptr;
    const ::chat::IMeshAdapter* mesh_ = nullptr;
};

} // namespace ui_key_verification_runtime
