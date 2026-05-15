#pragma once

#include "ui_key_verification_runtime/key_verification_action_sink.h"

namespace ui::presentation_sources
{

using LegacyKeyVerificationActionSink
    [[deprecated("Use ui_key_verification_runtime::KeyVerificationActionSink")]] =
        ::ui_key_verification_runtime::KeyVerificationActionSink;

} // namespace ui::presentation_sources
