#pragma once

#include "ui_presentation/common/ui_action_result.h"

#include <stdint.h>

namespace ui::key_verification
{

class IKeyVerificationActionSink
{
  public:
    virtual ~IKeyVerificationActionSink() = default;

    virtual ui::UiActionResult accept(uint32_t peer_id) = 0;
    virtual ui::UiActionResult reject(uint32_t peer_id) = 0;
    virtual ui::UiActionResult refresh(uint32_t peer_id) = 0;
    virtual ui::UiActionResult copyCode(uint32_t peer_id) = 0;
    virtual ui::UiActionResult submitNumber(uint32_t peer_id,
                                            uint32_t number) = 0;
};

} // namespace ui::key_verification
