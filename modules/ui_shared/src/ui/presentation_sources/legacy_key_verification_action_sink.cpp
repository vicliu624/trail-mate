#include "ui/presentation_sources/legacy_key_verification_action_sink.h"

#include "chat/domain/chat_types.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/usecase/contact_service.h"

namespace ui::presentation_sources
{

namespace
{

::ui::key_verification::VerificationProtocol mapProtocol(
    ::chat::MeshProtocol protocol)
{
    switch (protocol)
    {
    case ::chat::MeshProtocol::MeshCore:
        return ::ui::key_verification::VerificationProtocol::MeshCore;
    case ::chat::MeshProtocol::Meshtastic:
        return ::ui::key_verification::VerificationProtocol::Meshtastic;
    case ::chat::MeshProtocol::RNode:
    case ::chat::MeshProtocol::LXMF:
    default:
        return ::ui::key_verification::VerificationProtocol::Unknown;
    }
}

} // namespace

LegacyKeyVerificationActionSink::LegacyKeyVerificationActionSink(
    LegacyKeyVerificationSession& session,
    ::chat::IMeshAdapter* mesh,
    ::chat::contacts::ContactService* contacts)
    : session_(session),
      mesh_(mesh),
      contacts_(contacts)
{
}

::ui::UiActionResult LegacyKeyVerificationActionSink::accept(uint32_t peer_id)
{
    if (peer_id == 0 || !sessionMatches(peer_id))
    {
        return ::ui::UiActionResult::fail(::ui::UiActionFailure::InvalidInput);
    }
    if (session_.prompt !=
        ::ui::key_verification::VerificationPromptKind::CompareCode)
    {
        return ::ui::UiActionResult::fail(::ui::UiActionFailure::Rejected);
    }
    if (contacts_ == nullptr)
    {
        return ::ui::UiActionResult::fail(::ui::UiActionFailure::NotReady);
    }

    if (!contacts_->setNodeKeyManuallyVerified(peer_id, true))
    {
        session_.state = ::ui::key_verification::VerificationState::Failed;
        session_.failure =
            ::ui::key_verification::VerificationFailureKind::RuntimeUnavailable;
        return ::ui::UiActionResult::fail(::ui::UiActionFailure::Rejected);
    }

    session_.state = ::ui::key_verification::VerificationState::Verified;
    session_.failure = ::ui::key_verification::VerificationFailureKind::None;
    session_.prompt = ::ui::key_verification::VerificationPromptKind::None;
    return ::ui::UiActionResult::success();
}

::ui::UiActionResult LegacyKeyVerificationActionSink::reject(uint32_t peer_id)
{
    if (peer_id == 0)
    {
        return ::ui::UiActionResult::fail(::ui::UiActionFailure::InvalidInput);
    }

    if (session_.peer_id == 0 || session_.peer_id == peer_id)
    {
        session_.peer_id = peer_id;
        session_.state = ::ui::key_verification::VerificationState::Rejected;
        session_.failure = ::ui::key_verification::VerificationFailureKind::None;
        session_.prompt = ::ui::key_verification::VerificationPromptKind::None;
        return ::ui::UiActionResult::success();
    }

    return ::ui::UiActionResult::fail(::ui::UiActionFailure::InvalidInput);
}

::ui::UiActionResult LegacyKeyVerificationActionSink::refresh(uint32_t peer_id)
{
    if (peer_id == 0)
    {
        return ::ui::UiActionResult::fail(::ui::UiActionFailure::InvalidInput);
    }
    if (mesh_ == nullptr)
    {
        return ::ui::UiActionResult::fail(::ui::UiActionFailure::NotReady);
    }

    if (!mesh_->startKeyVerification(peer_id))
    {
        session_.peer_id = peer_id;
        session_.protocol = mapProtocol(mesh_->backendProtocol());
        session_.state = ::ui::key_verification::VerificationState::Failed;
        session_.failure =
            ::ui::key_verification::VerificationFailureKind::RuntimeUnavailable;
        session_.prompt = ::ui::key_verification::VerificationPromptKind::None;
        return ::ui::UiActionResult::fail(::ui::UiActionFailure::Rejected);
    }

    session_ = {};
    session_.peer_id = peer_id;
    session_.protocol = mapProtocol(mesh_->backendProtocol());
    session_.state = ::ui::key_verification::VerificationState::Pending;
    return ::ui::UiActionResult::success();
}

::ui::UiActionResult LegacyKeyVerificationActionSink::copyCode(uint32_t peer_id)
{
    if (peer_id == 0 || !sessionMatches(peer_id))
    {
        return ::ui::UiActionResult::fail(::ui::UiActionFailure::InvalidInput);
    }
    return ::ui::UiActionResult::fail(::ui::UiActionFailure::Unsupported);
}

::ui::UiActionResult LegacyKeyVerificationActionSink::submitNumber(
    uint32_t peer_id,
    uint32_t number)
{
    if (peer_id == 0 || number > 999999U || !sessionMatches(peer_id))
    {
        return ::ui::UiActionResult::fail(::ui::UiActionFailure::InvalidInput);
    }
    if (session_.prompt !=
            ::ui::key_verification::VerificationPromptKind::EnterNumber ||
        session_.nonce == 0)
    {
        return ::ui::UiActionResult::fail(::ui::UiActionFailure::Rejected);
    }
    if (mesh_ == nullptr)
    {
        return ::ui::UiActionResult::fail(::ui::UiActionFailure::NotReady);
    }

    if (!mesh_->submitKeyVerificationNumber(peer_id, session_.nonce, number))
    {
        return ::ui::UiActionResult::fail(::ui::UiActionFailure::Rejected);
    }

    session_.prompt = ::ui::key_verification::VerificationPromptKind::None;
    session_.state = ::ui::key_verification::VerificationState::Pending;
    return ::ui::UiActionResult::success();
}

bool LegacyKeyVerificationActionSink::sessionMatches(uint32_t peer_id) const
{
    return session_.peer_id == peer_id && peer_id != 0;
}

} // namespace ui::presentation_sources
