#include <cstddef>
#include <cstdint>
#include <iostream>
#include <unordered_set>

#include "app/demo_app.h"
#include "app/input_event.h"
#include "core/canvas.h"
#include "core/color.h"
#include "core/display_profile.h"

int main()
{
    trailmate::cardputer_zero::app::DemoApp app{};
    trailmate::cardputer_zero::core::Canvas canvas{
        trailmate::cardputer_zero::core::kDisplayWidth,
        trailmate::cardputer_zero::core::kDisplayHeight};
    app.render(canvas);
    const auto baseline_pixels = canvas.pixels();

    app.handleInput(trailmate::cardputer_zero::app::makeCharacterInput('A', "A"));
    app.handleInput(trailmate::cardputer_zero::app::InputEvent{
        trailmate::cardputer_zero::app::InputKey::Next,
        "NEXT",
        '\0'});
    app.render(canvas);

    const auto background = canvas.pixel(0, 0);
    std::size_t changed_pixels = 0;
    std::size_t frame_delta = 0;
    std::unordered_set<std::uint32_t> palette{};

    for (std::size_t index = 0; index < canvas.pixels().size(); ++index)
    {
        const auto& pixel = canvas.pixels()[index];
        if (pixel != background)
        {
            ++changed_pixels;
        }
        palette.insert(trailmate::cardputer_zero::core::pack_key(pixel));
        if (pixel != baseline_pixels[index])
        {
            ++frame_delta;
        }
    }

    if (changed_pixels < 1000)
    {
        std::cerr << "Expected the demo screen to render more than a trivial number of pixels.\n";
        return 1;
    }

    if (palette.size() < 4)
    {
        std::cerr << "Expected multiple colors in the rendered demo screen.\n";
        return 1;
    }

    if (canvas.pixel(12, 12) == background)
    {
        std::cerr << "Expected the framed application panel to differ from the outer background.\n";
        return 1;
    }

    if (frame_delta == 0)
    {
        std::cerr << "Expected the rendered frame to change after simulated input.\n";
        return 1;
    }

    return 0;
}
