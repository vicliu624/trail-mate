#pragma once

#include "product_composition/presentation_bundle.h"
#include "ui_ascii_runtime/ascii_menu_runtime_adapter.h"
#include "ui_ascii_runtime/ascii_screen_host_adapter.h"

#include <cstddef>

namespace trailmate::linux_sim
{

struct AsciiScreenGraph
{
    AsciiMenuLine menu_lines[ui::menu::MenuModel::kMaxItems]{};
    AsciiScreenDescriptor screens[ui::menu::MenuModel::kMaxItems]{};
    std::size_t menu_count = 0;
    std::size_t screen_count = 0;
};

class AsciiScreenGraphBridge
{
  public:
    bool load(const product_composition::PresentationBundle& presentation);

    std::size_t menuCount() const;
    std::size_t screenCount() const;

    const AsciiMenuLine* menuItems() const;
    const AsciiScreenDescriptor* screenItems() const;
    const AsciiScreenGraph& graph() const;

  private:
    AsciiMenuRuntimeAdapter menu_adapter_{};
    AsciiScreenHostAdapter screen_host_{};
    AsciiScreenGraph graph_{};
};

} // namespace trailmate::linux_sim
