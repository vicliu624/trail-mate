#pragma once

#include "ui_presentation/key_verification/key_verification_snapshot.h"

namespace ui::key_verification
{

struct KeyVerificationRequest
{
    VerificationProtocol protocol = VerificationProtocol::Unknown;
    uint32_t peer_id = 0;

    bool isValid() const
    {
        return peer_id != 0;
    }
};

class IKeyVerificationPresentationSource
{
  public:
    virtual ~IKeyVerificationPresentationSource() = default;

    virtual bool buildKeyVerificationSnapshot(
        const KeyVerificationRequest& request,
        KeyVerificationSnapshot& out) const = 0;
};

} // namespace ui::key_verification
