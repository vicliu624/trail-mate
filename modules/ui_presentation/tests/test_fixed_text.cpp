#include "ui_presentation/common/fixed_text.h"
#include "ui_presentation/common/presentation_profile.h"
#include "ui_presentation/common/snapshot_header.h"
#include "ui_presentation/common/ui_action.h"
#include "ui_presentation/common/ui_action_result.h"

#include <cassert>
#include <cstring>

int main()
{
    ui::FixedText<8> text;
    assert(text.empty());

    ui::copyText(text, "trail");
    assert(!text.empty());
    assert(std::strcmp(text.c_str(), "trail") == 0);

    ui::copyText(text, "trail-mate");
    assert(std::strcmp(text.c_str(), "trail-m") == 0);

    ui::copyText(text, nullptr);
    assert(text.empty());

    ui::SnapshotHeader header{};
    assert(!header.valid);
    header.valid = true;
    header.version = 4;
    header.generated_at_ms = 50;
    assert(header.valid);
    assert(header.version == 4);
    assert(header.generated_at_ms == 50);

    auto ok = ui::UiActionResult::success();
    assert(ok.ok);
    assert(ok.failure == ui::UiActionFailure::None);

    auto failed = ui::UiActionResult::fail(ui::UiActionFailure::Unsupported);
    assert(!failed.ok);
    assert(failed.failure == ui::UiActionFailure::Unsupported);

    ui::UiAction action{};
    action.kind = ui::UiActionKind::Refresh;
    action.target = 2;
    assert(action.kind == ui::UiActionKind::Refresh);
    assert(action.target == 2);

    ui::PresentationProfile profile{};
    profile.kind = ui::PresentationProfileKind::TerminalPanel;
    profile.width = 80;
    profile.height = 24;
    profile.keyboard_input = true;
    assert(profile.kind == ui::PresentationProfileKind::TerminalPanel);
    assert(profile.width == 80);
    assert(profile.height == 24);
    assert(profile.keyboard_input);

    return 0;
}
