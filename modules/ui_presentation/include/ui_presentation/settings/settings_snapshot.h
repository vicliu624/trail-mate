#pragma once

#include "ui_presentation/common/fixed_text.h"
#include "ui_presentation/common/snapshot_header.h"
#include "ui_presentation/common/ui_action_result.h"

#include <stddef.h>
#include <stdint.h>

namespace ui::settings
{

enum class SettingControlKind : uint8_t
{
    Toggle,
    Choice,
    Number,
    Text,
    Action,
};

struct SettingsOption
{
    ui::FixedText<32> key;
    ui::FixedText<48> label;
    ui::FixedText<48> value_label;
    SettingControlKind control = SettingControlKind::Action;
    bool enabled = true;
};

struct SettingsSection
{
    ui::FixedText<32> title;
    SettingsOption options[12]{};
    size_t option_count = 0;
};

struct SettingsSnapshot
{
    ui::SnapshotHeader header;
    SettingsSection sections[8]{};
    size_t section_count = 0;
};

struct SettingsPatchView
{
    ui::FixedText<32> key;
    ui::FixedText<64> value;
};

} // namespace ui::settings
