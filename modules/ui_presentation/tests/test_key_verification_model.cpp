#include "ui_presentation/common/fixed_text.h"
#include "ui_presentation/key_verification/key_verification_model.h"

#include <cassert>
#include <cstring>

namespace
{

class FakeKeyVerificationSource final
    : public ui::key_verification::IKeyVerificationPresentationSource
{
  public:
    bool buildKeyVerificationSnapshot(
        const ui::key_verification::KeyVerificationRequest& request,
        ui::key_verification::KeyVerificationSnapshot& out) const override
    {
        ++build_count;
        last_request = request;
        if (!available)
        {
            return false;
        }

        out = snapshot;
        out.header.valid = true;
        out.protocol = request.protocol;
        out.peer_id = request.peer_id;
        return true;
    }

    mutable int build_count = 0;
    mutable ui::key_verification::KeyVerificationRequest last_request{};
    bool available = true;
    ui::key_verification::KeyVerificationSnapshot snapshot{};
};

class FakeKeyVerificationSink final
    : public ui::key_verification::IKeyVerificationActionSink
{
  public:
    ui::UiActionResult accept(uint32_t peer_id) override
    {
        ++accept_count;
        last_peer_id = peer_id;
        return accept_result;
    }

    ui::UiActionResult reject(uint32_t peer_id) override
    {
        ++reject_count;
        last_peer_id = peer_id;
        return reject_result;
    }

    ui::UiActionResult refresh(uint32_t peer_id) override
    {
        ++refresh_count;
        last_peer_id = peer_id;
        return refresh_result;
    }

    ui::UiActionResult copyCode(uint32_t peer_id) override
    {
        ++copy_count;
        last_peer_id = peer_id;
        return copy_result;
    }

    ui::UiActionResult submitNumber(uint32_t peer_id,
                                    uint32_t number) override
    {
        ++submit_count;
        last_peer_id = peer_id;
        last_number = number;
        return submit_result;
    }

    int accept_count = 0;
    int reject_count = 0;
    int refresh_count = 0;
    int copy_count = 0;
    int submit_count = 0;
    uint32_t last_peer_id = 0;
    uint32_t last_number = 0;
    ui::UiActionResult accept_result = ui::UiActionResult::success();
    ui::UiActionResult reject_result = ui::UiActionResult::success();
    ui::UiActionResult refresh_result = ui::UiActionResult::success();
    ui::UiActionResult copy_result = ui::UiActionResult::success();
    ui::UiActionResult submit_result = ui::UiActionResult::success();
};

} // namespace

int main()
{
    using namespace ui::key_verification;

    FakeKeyVerificationSource source;
    source.snapshot.state = VerificationState::Pending;
    source.snapshot.prompt = VerificationPromptKind::CompareCode;
    source.snapshot.can_accept = true;
    ui::copyText(source.snapshot.peer_label, "Ada");
    ui::copyText(source.snapshot.verification_code, "ABCD-1234");
    ui::copyText(source.snapshot.status_line, "Compare verification code");

    FakeKeyVerificationSink sink;
    KeyVerificationModel model(source, sink);

    auto idle = model.snapshot();
    assert(idle.header.valid);
    assert(idle.state == VerificationState::Idle);
    assert(idle.prompt == VerificationPromptKind::None);
    assert(source.build_count == 0);

    auto result = model.accept();
    assert(!result.ok);
    assert(result.failure == ui::UiActionFailure::InvalidInput);
    assert(sink.accept_count == 0);

    model.selectPeer(VerificationProtocol::Meshtastic, 0x1234);
    const auto selected_request = model.request();
    assert(selected_request.isValid());
    assert(selected_request.protocol == VerificationProtocol::Meshtastic);
    assert(selected_request.peer_id == 0x1234);

    const auto snapshot = model.snapshot();
    assert(snapshot.header.valid);
    assert(snapshot.peer_id == 0x1234);
    assert(snapshot.protocol == VerificationProtocol::Meshtastic);
    assert(snapshot.state == VerificationState::Pending);
    assert(snapshot.prompt == VerificationPromptKind::CompareCode);
    assert(snapshot.can_accept);
    assert(std::strcmp(snapshot.peer_label.c_str(), "Ada") == 0);
    assert(std::strcmp(snapshot.verification_code.c_str(), "ABCD-1234") == 0);
    assert(source.build_count == 1);
    assert(source.last_request.peer_id == 0x1234);

    result = model.accept();
    assert(result.ok);
    assert(sink.accept_count == 1);
    assert(sink.last_peer_id == 0x1234);

    result = model.reject();
    assert(result.ok);
    assert(sink.reject_count == 1);
    result = model.refresh();
    assert(result.ok);
    assert(sink.refresh_count == 1);
    result = model.copyCode();
    assert(result.ok);
    assert(sink.copy_count == 1);
    result = model.submitNumber(123456);
    assert(result.ok);
    assert(sink.submit_count == 1);
    assert(sink.last_number == 123456);

    result = model.submitNumber(1000000);
    assert(!result.ok);
    assert(result.failure == ui::UiActionFailure::InvalidInput);
    assert(sink.submit_count == 1);

    source.available = false;
    const auto unavailable = model.snapshot();
    assert(!unavailable.header.valid);
    assert(unavailable.peer_id == 0x1234);
    assert(unavailable.state == VerificationState::Failed);
    assert(unavailable.failure ==
           VerificationFailureKind::RuntimeUnavailable);

    model.clearSelection();
    idle = model.snapshot();
    assert(idle.header.valid);
    assert(idle.state == VerificationState::Idle);

    return 0;
}
