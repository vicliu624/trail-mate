#include "fake/fake_settings_action_sink.h"
#include "fake/fake_settings_source.h"
#include "ui_presentation/settings/settings_model.h"

#include <cassert>
#include <cstring>

int main()
{
    ui::tests::FakeSettingsSource source;
    source.snapshot_value.header.valid = true;
    source.snapshot_value.header.version = 9;
    source.snapshot_value.section_count = 1;
    ui::copyText(source.snapshot_value.sections[0].title, "GPS");
    source.snapshot_value.sections[0].option_count = 1;
    ui::copyText(source.snapshot_value.sections[0].options[0].key, "gps_enabled");
    ui::copyText(source.snapshot_value.sections[0].options[0].label, "GPS Enabled");
    ui::copyText(source.snapshot_value.sections[0].options[0].value_label, "ON");
    source.snapshot_value.sections[0].options[0].control =
        ui::settings::SettingControlKind::Toggle;

    ui::tests::FakeSettingsActionSink sink;
    ui::settings::SettingsModel model(source, sink);

    const auto snapshot = model.snapshot();
    assert(snapshot.header.valid);
    assert(snapshot.header.version == 9);
    assert(snapshot.section_count == 1);
    assert(std::strcmp(snapshot.sections[0].title.c_str(), "GPS") == 0);
    assert(snapshot.sections[0].option_count == 1);
    assert(std::strcmp(snapshot.sections[0].options[0].key.c_str(), "gps_enabled") == 0);

    ui::settings::SettingsPatchView patch;
    ui::copyText(patch.key, "gps_enabled");
    ui::copyText(patch.value, "1");

    const auto result = model.apply(patch);
    assert(result.ok);
    assert(sink.apply_count == 1);
    assert(std::strcmp(sink.last_patch.key.c_str(), "gps_enabled") == 0);
    assert(std::strcmp(sink.last_patch.value.c_str(), "1") == 0);

    source.available = false;
    const auto invalid = model.snapshot();
    assert(!invalid.header.valid);

    sink.result = ui::UiActionResult::fail(ui::UiActionFailure::Rejected);
    const auto rejected = model.apply(patch);
    assert(!rejected.ok);
    assert(rejected.failure == ui::UiActionFailure::Rejected);

    return 0;
}
