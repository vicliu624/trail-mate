#include "ui_legacy_adapters/legacy_key_verification_action_sink.h"
#include "ui_legacy_adapters/legacy_key_verification_session.h"
#include "ui_legacy_adapters/legacy_key_verification_source.h"

#include <type_traits>

int main()
{
    static_assert(
        std::is_same<ui::presentation_sources::LegacyKeyVerificationSession,
                     ui_key_verification_runtime::KeyVerificationSessionAdapter>::value,
        "LegacyKeyVerificationSession must stay an alias only");
    static_assert(
        std::is_same<ui::presentation_sources::LegacyKeyVerificationSource,
                     ui_key_verification_runtime::KeyVerificationPresentationSource>::value,
        "LegacyKeyVerificationSource must stay an alias only");
    static_assert(
        std::is_same<ui::presentation_sources::LegacyKeyVerificationActionSink,
                     ui_key_verification_runtime::KeyVerificationActionSink>::value,
        "LegacyKeyVerificationActionSink must stay an alias only");
    return 0;
}
