#pragma once

#include "ui_presentation/key_verification/key_verification_action_sink.h"
#include "ui_presentation/key_verification/key_verification_source.h"

namespace ui::key_verification
{

class KeyVerificationModel
{
  public:
    KeyVerificationModel(IKeyVerificationPresentationSource& source,
                         IKeyVerificationActionSink& sink);

    void selectPeer(VerificationProtocol protocol, uint32_t peer_id);
    void clearSelection();

    KeyVerificationRequest request() const;
    KeyVerificationSnapshot snapshot() const;

    ui::UiActionResult accept();
    ui::UiActionResult reject();
    ui::UiActionResult refresh();
    ui::UiActionResult copyCode();
    ui::UiActionResult submitNumber(uint32_t number);

  private:
    ui::UiActionResult requireSelected() const;

    IKeyVerificationPresentationSource& source_;
    IKeyVerificationActionSink& sink_;

    VerificationProtocol protocol_ = VerificationProtocol::Unknown;
    uint32_t peer_id_ = 0;
};

} // namespace ui::key_verification
