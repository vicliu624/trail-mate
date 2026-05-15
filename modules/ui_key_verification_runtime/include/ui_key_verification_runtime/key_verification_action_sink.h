#pragma once

#include "ui_key_verification_runtime/key_verification_session_adapter.h"
#include "ui_presentation/key_verification/key_verification_action_sink.h"

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

class KeyVerificationActionSink final
    : public ::ui::key_verification::IKeyVerificationActionSink
{
  public:
    KeyVerificationActionSink(
        KeyVerificationSessionAdapter& session,
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

    KeyVerificationSessionAdapter& session_;
    ::chat::IMeshAdapter* mesh_ = nullptr;
    ::chat::contacts::ContactService* contacts_ = nullptr;
};

} // namespace ui_key_verification_runtime
