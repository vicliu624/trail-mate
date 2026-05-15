#include "ui_key_verification_runtime/key_verification_presentation_source.h"

#include "chat/domain/chat_types.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/usecase/contact_service.h"
#include "ui_presentation/common/fixed_text.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace ui_key_verification_runtime
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

void copySecurityNumber(
    uint32_t number,
    ::ui::key_verification::KeyVerificationSnapshot& out)
{
    char buffer[24] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%03u %03u",
                  static_cast<unsigned>(number / 1000U),
                  static_cast<unsigned>(number % 1000U));
    ::ui::copyText(out.verification_code, buffer);
}

const char* failureStatus(
    ::ui::key_verification::VerificationFailureKind failure)
{
    switch (failure)
    {
    case ::ui::key_verification::VerificationFailureKind::MissingPeerKey:
        return "Peer key missing";
    case ::ui::key_verification::VerificationFailureKind::MissingLocalIdentity:
        return "Local identity missing";
    case ::ui::key_verification::VerificationFailureKind::CodeMismatch:
        return "Verification code mismatch";
    case ::ui::key_verification::VerificationFailureKind::UnsupportedProtocol:
        return "Key verification unsupported";
    case ::ui::key_verification::VerificationFailureKind::RuntimeUnavailable:
        return "Key verification unavailable";
    case ::ui::key_verification::VerificationFailureKind::Unknown:
        return "Key verification failed";
    case ::ui::key_verification::VerificationFailureKind::None:
    default:
        return "";
    }
}

} // namespace

KeyVerificationPresentationSource::KeyVerificationPresentationSource(
    KeyVerificationSessionAdapter& session,
    const ::chat::contacts::ContactService* contacts,
    const ::chat::IMeshAdapter* mesh)
    : session_(session),
      contacts_(contacts),
      mesh_(mesh)
{
}

bool KeyVerificationPresentationSource::buildKeyVerificationSnapshot(
    const ::ui::key_verification::KeyVerificationRequest& request,
    ::ui::key_verification::KeyVerificationSnapshot& out) const
{
    if (!request.isValid())
    {
        out = {};
        out.header.valid = true;
        out.state = ::ui::key_verification::VerificationState::Idle;
        return true;
    }

    out = {};
    out.header.valid = true;
    out.header.version = 1;
    out.peer_id = request.peer_id;
    out.protocol = request.protocol !=
                           ::ui::key_verification::VerificationProtocol::Unknown
                       ? request.protocol
                       : currentProtocol();
    copyPeerLabel(request.peer_id, out);

    if (session_.peer_id != request.peer_id ||
        session_.state == ::ui::key_verification::VerificationState::Idle)
    {
        out.state = ::ui::key_verification::VerificationState::Idle;
        out.prompt = ::ui::key_verification::VerificationPromptKind::None;
        out.can_refresh = true;
        ::ui::copyText(out.status_line, "No active key verification");
        return true;
    }

    out.protocol = session_.protocol !=
                           ::ui::key_verification::VerificationProtocol::Unknown
                       ? session_.protocol
                       : out.protocol;
    out.state = session_.state;
    out.failure = session_.failure;
    out.prompt = session_.prompt;

    if (session_.prompt ==
        ::ui::key_verification::VerificationPromptKind::EnterNumber)
    {
        out.requires_number = true;
        out.can_reject = true;
    }
    else if (session_.prompt ==
             ::ui::key_verification::VerificationPromptKind::ShowNumber)
    {
        out.can_copy_code = true;
        copySecurityNumber(session_.security_number, out);
    }
    else if (session_.prompt ==
             ::ui::key_verification::VerificationPromptKind::CompareCode)
    {
        out.can_accept = true;
        out.can_reject = true;
        out.can_copy_code = true;
        ::ui::copyText(out.verification_code,
                       session_.verification_code[0] != '\0'
                           ? session_.verification_code
                           : "--------");
    }

    if (session_.state ==
        ::ui::key_verification::VerificationState::Failed)
    {
        out.can_refresh = true;
    }

    copyStatusLine(out);
    return true;
}

void KeyVerificationPresentationSource::onNumberRequest(uint32_t peer_id,
                                                        uint64_t nonce)
{
    session_ = {};
    session_.peer_id = peer_id;
    session_.nonce = nonce;
    session_.protocol = currentProtocol();
    session_.state = ::ui::key_verification::VerificationState::Pending;
    session_.prompt =
        ::ui::key_verification::VerificationPromptKind::EnterNumber;
}

void KeyVerificationPresentationSource::onNumberInform(
    uint32_t peer_id,
    uint64_t nonce,
    uint32_t security_number)
{
    session_ = {};
    session_.peer_id = peer_id;
    session_.nonce = nonce;
    session_.security_number = security_number;
    session_.protocol = currentProtocol();
    session_.state = ::ui::key_verification::VerificationState::Pending;
    session_.prompt =
        ::ui::key_verification::VerificationPromptKind::ShowNumber;
}

void KeyVerificationPresentationSource::onFinal(
    uint32_t peer_id,
    uint64_t nonce,
    bool is_sender,
    const char* verification_code)
{
    session_ = {};
    session_.peer_id = peer_id;
    session_.nonce = nonce;
    session_.is_sender = is_sender;
    session_.protocol = currentProtocol();
    session_.state = ::ui::key_verification::VerificationState::Pending;
    session_.prompt =
        ::ui::key_verification::VerificationPromptKind::CompareCode;
    if (verification_code != nullptr)
    {
        std::strncpy(session_.verification_code,
                     verification_code,
                     sizeof(session_.verification_code) - 1U);
        session_.verification_code[sizeof(session_.verification_code) - 1U] =
            '\0';
    }
}

void KeyVerificationPresentationSource::onPeerKeyMissing(uint32_t peer_id)
{
    session_ = {};
    session_.peer_id = peer_id;
    session_.protocol = currentProtocol();
    session_.state = ::ui::key_verification::VerificationState::Failed;
    session_.failure =
        ::ui::key_verification::VerificationFailureKind::MissingPeerKey;
}

void KeyVerificationPresentationSource::clear()
{
    session_ = {};
}

::ui::key_verification::VerificationProtocol
KeyVerificationPresentationSource::currentProtocol() const
{
    if (mesh_ == nullptr)
    {
        return ::ui::key_verification::VerificationProtocol::Unknown;
    }
    return mapProtocol(mesh_->backendProtocol());
}

void KeyVerificationPresentationSource::copyPeerLabel(
    uint32_t peer_id,
    ::ui::key_verification::KeyVerificationSnapshot& out) const
{
    if (contacts_ != nullptr)
    {
        const std::string name = contacts_->getContactName(peer_id);
        if (!name.empty())
        {
            ::ui::copyText(out.peer_label, name.c_str());
            return;
        }
    }

    char fallback[16] = {};
    std::snprintf(fallback,
                  sizeof(fallback),
                  "%08lX",
                  static_cast<unsigned long>(peer_id));
    ::ui::copyText(out.peer_label, fallback);
}

void KeyVerificationPresentationSource::copyStatusLine(
    ::ui::key_verification::KeyVerificationSnapshot& out) const
{
    if (out.state == ::ui::key_verification::VerificationState::Verified)
    {
        ::ui::copyText(out.status_line, "Key trusted");
        return;
    }
    if (out.state == ::ui::key_verification::VerificationState::Rejected)
    {
        ::ui::copyText(out.status_line, "Verification rejected");
        return;
    }
    if (out.state == ::ui::key_verification::VerificationState::Failed)
    {
        ::ui::copyText(out.status_line, failureStatus(out.failure));
        return;
    }

    switch (out.prompt)
    {
    case ::ui::key_verification::VerificationPromptKind::EnterNumber:
        ::ui::copyText(out.status_line, "Enter verification number");
        break;
    case ::ui::key_verification::VerificationPromptKind::ShowNumber:
        ::ui::copyText(out.status_line, "Share this verification number");
        break;
    case ::ui::key_verification::VerificationPromptKind::CompareCode:
        ::ui::copyText(out.status_line, "Compare verification code");
        break;
    case ::ui::key_verification::VerificationPromptKind::None:
    default:
        ::ui::copyText(out.status_line, "Key verification pending");
        break;
    }
}

} // namespace ui_key_verification_runtime
