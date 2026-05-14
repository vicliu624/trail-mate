#include "ui_presentation/key_verification/key_verification_model.h"

namespace ui::key_verification
{

KeyVerificationModel::KeyVerificationModel(
    IKeyVerificationPresentationSource& source,
    IKeyVerificationActionSink& sink)
    : source_(source),
      sink_(sink)
{
}

void KeyVerificationModel::selectPeer(VerificationProtocol protocol,
                                      uint32_t peer_id)
{
    protocol_ = protocol;
    peer_id_ = peer_id;
}

void KeyVerificationModel::clearSelection()
{
    protocol_ = VerificationProtocol::Unknown;
    peer_id_ = 0;
}

KeyVerificationRequest KeyVerificationModel::request() const
{
    KeyVerificationRequest out{};
    out.protocol = protocol_;
    out.peer_id = peer_id_;
    return out;
}

KeyVerificationSnapshot KeyVerificationModel::snapshot() const
{
    KeyVerificationSnapshot out{};
    if (peer_id_ == 0)
    {
        out.header.valid = true;
        out.state = VerificationState::Idle;
        out.prompt = VerificationPromptKind::None;
        return out;
    }

    if (!source_.buildKeyVerificationSnapshot(request(), out))
    {
        out = {};
        out.header.valid = false;
        out.peer_id = peer_id_;
        out.protocol = protocol_;
        out.state = VerificationState::Failed;
        out.failure = VerificationFailureKind::RuntimeUnavailable;
    }
    return out;
}

ui::UiActionResult KeyVerificationModel::accept()
{
    const auto selected = requireSelected();
    if (!selected.ok)
    {
        return selected;
    }
    return sink_.accept(peer_id_);
}

ui::UiActionResult KeyVerificationModel::reject()
{
    const auto selected = requireSelected();
    if (!selected.ok)
    {
        return selected;
    }
    return sink_.reject(peer_id_);
}

ui::UiActionResult KeyVerificationModel::refresh()
{
    const auto selected = requireSelected();
    if (!selected.ok)
    {
        return selected;
    }
    return sink_.refresh(peer_id_);
}

ui::UiActionResult KeyVerificationModel::copyCode()
{
    const auto selected = requireSelected();
    if (!selected.ok)
    {
        return selected;
    }
    return sink_.copyCode(peer_id_);
}

ui::UiActionResult KeyVerificationModel::submitNumber(uint32_t number)
{
    const auto selected = requireSelected();
    if (!selected.ok)
    {
        return selected;
    }
    if (number > 999999U)
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
    }
    return sink_.submitNumber(peer_id_, number);
}

ui::UiActionResult KeyVerificationModel::requireSelected() const
{
    if (peer_id_ == 0)
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
    }
    return ui::UiActionResult::success();
}

} // namespace ui::key_verification
