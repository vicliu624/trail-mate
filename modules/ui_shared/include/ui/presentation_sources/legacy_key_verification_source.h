#pragma once

#include "ui_key_verification_runtime/key_verification_presentation_source.h"

namespace ui::presentation_sources
{

using LegacyKeyVerificationSource
    [[deprecated("Use ui_key_verification_runtime::KeyVerificationPresentationSource")]] =
        ::ui_key_verification_runtime::KeyVerificationPresentationSource;

} // namespace ui::presentation_sources
