#pragma once

#include "ui_presentation/key_verification/key_verification_snapshot.h"

#include <stdint.h>

namespace ui_key_verification_runtime
{

struct KeyVerificationSessionAdapter
{
    ::ui::key_verification::VerificationProtocol protocol =
        ::ui::key_verification::VerificationProtocol::Unknown;
    ::ui::key_verification::VerificationState state =
        ::ui::key_verification::VerificationState::Idle;
    ::ui::key_verification::VerificationFailureKind failure =
        ::ui::key_verification::VerificationFailureKind::None;
    ::ui::key_verification::VerificationPromptKind prompt =
        ::ui::key_verification::VerificationPromptKind::None;

    uint32_t peer_id = 0;
    uint64_t nonce = 0;
    uint32_t security_number = 0;
    bool is_sender = false;
    char verification_code[32]{};
};

} // namespace ui_key_verification_runtime
