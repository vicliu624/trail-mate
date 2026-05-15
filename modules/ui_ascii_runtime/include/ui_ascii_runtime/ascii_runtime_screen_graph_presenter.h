#pragma once

#include "product_composition/presentation_bundle.h"
#include "ui_ascii_runtime/ascii_screen_graph_bridge.h"

#include <cstddef>

namespace trailmate::linux_sim
{

class AsciiRuntimeScreenGraphPresenter
{
  public:
    bool load(const product_composition::PresentationBundle& presentation);

    std::size_t menuCount() const;
    std::size_t screenCount() const;

    const AsciiMenuLine* menuLines() const;
    const AsciiScreenDescriptor* screens() const;

  private:
    AsciiScreenGraphBridge bridge_{};
};

} // namespace trailmate::linux_sim
