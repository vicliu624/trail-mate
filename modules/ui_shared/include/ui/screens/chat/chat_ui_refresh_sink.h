#pragma once

#include "chat/domain/chat_types.h"
#include "ui_presentation/key_verification/key_verification_snapshot.h"

namespace chat::ui
{

class IChatUiRefreshSink
{
  public:
    virtual ~IChatUiRefreshSink() = default;

    virtual void onRuntimeMessageArrived(chat::MessageId msg_id) = 0;
    virtual void onRuntimeSendResult(chat::MessageId msg_id) = 0;
    virtual void onRuntimeUnreadChanged() = 0;

    virtual void showKeyVerification(
        const ::ui::key_verification::KeyVerificationSnapshot& snapshot) = 0;
};

} // namespace chat::ui
