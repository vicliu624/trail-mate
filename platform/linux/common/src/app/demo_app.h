#pragma once

#include <chrono>
#include <string>

#include "app/input_event.h"
#include "core/canvas.h"

namespace trailmate::cardputer_zero::app
{

class DemoApp
{
  public:
    DemoApp();

    void handleInput(const InputEvent& event) noexcept;
    void render(core::Canvas& canvas) const noexcept;

  private:
    [[nodiscard]] bool blinkOn() const noexcept;
    [[nodiscard]] bool hasTypedText() const noexcept;
    [[nodiscard]] std::string visibleInput() const;
    [[nodiscard]] std::string visibleStatus() const;
    [[nodiscard]] std::string visibleLastKey() const;

    std::chrono::steady_clock::time_point started_at_{};
    std::string typed_text_{};
    std::string last_key_label_{"NONE"};
    std::string status_text_{"SIM FIRST LINUX BRINGUP"};
};

} // namespace trailmate::cardputer_zero::app
