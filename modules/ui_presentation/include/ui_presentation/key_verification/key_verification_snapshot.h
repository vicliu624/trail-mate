#pragma once

#include "ui_presentation/common/fixed_text.h"
#include "ui_presentation/common/snapshot_header.h"

#include <stdint.h>

namespace ui::key_verification
{

enum class VerificationProtocol : uint8_t
{
    Unknown,
    Meshtastic,
    MeshCore,
};

enum class VerificationState : uint8_t
{
    Idle,
    Pending,
    Verified,
    Rejected,
    Failed,
};

enum class VerificationFailureKind : uint8_t
{
    None,
    MissingPeerKey,
    MissingLocalIdentity,
    CodeMismatch,
    UnsupportedProtocol,
    RuntimeUnavailable,
    Unknown,
};

enum class VerificationPromptKind : uint8_t
{
    None,
    EnterNumber,
    ShowNumber,
    CompareCode,
};

struct KeyVerificationSnapshot
{
    ui::SnapshotHeader header;

    VerificationProtocol protocol = VerificationProtocol::Unknown;
    VerificationState state = VerificationState::Idle;
    VerificationFailureKind failure = VerificationFailureKind::None;
    VerificationPromptKind prompt = VerificationPromptKind::None;

    uint32_t peer_id = 0;

    ui::FixedText<48> peer_label;
    ui::FixedText<32> verification_code;
    ui::FixedText<96> status_line;

    bool can_accept = false;
    bool can_reject = false;
    bool can_refresh = false;
    bool can_copy_code = false;
    bool requires_number = false;
};

} // namespace ui::key_verification
