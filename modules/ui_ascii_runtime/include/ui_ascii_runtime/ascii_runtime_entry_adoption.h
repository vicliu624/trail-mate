#pragma once

#include "product_composition/presentation_bundle.h"
#include "ui_ascii_runtime/ascii_runtime_screen_graph_presenter.h"

#include <cstddef>

namespace trailmate::linux_sim
{

class AsciiRuntimeEntryAdoption
{
  public:
    bool load(const product_composition::PresentationBundle& presentation);

    bool ready() const;
    std::size_t menuCount() const;
    std::size_t screenCount() const;

    const AsciiRuntimeScreenGraphPresenter& presenter() const;

  private:
    AsciiRuntimeScreenGraphPresenter presenter_{};
    bool ready_ = false;
};

} // namespace trailmate::linux_sim
