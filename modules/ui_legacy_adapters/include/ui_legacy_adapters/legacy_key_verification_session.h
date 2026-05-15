#pragma once

#include "ui_key_verification_runtime/key_verification_session_adapter.h"

namespace ui::presentation_sources
{

using LegacyKeyVerificationSession
    [[deprecated("Use ui_key_verification_runtime::KeyVerificationSessionAdapter")]] =
        ::ui_key_verification_runtime::KeyVerificationSessionAdapter;

} // namespace ui::presentation_sources
