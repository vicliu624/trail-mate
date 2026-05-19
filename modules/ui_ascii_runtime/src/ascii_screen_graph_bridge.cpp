#include "ui_ascii_runtime/ascii_screen_graph_bridge.h"

namespace trailmate::linux_sim
{
namespace
{

using MenuRuntimeAdapterContract = AsciiMenuRuntimeAdapter;
using ScreenHostAdapterContract = AsciiScreenHostAdapter;

void resetGraph(AsciiScreenGraph& graph)
{
    graph.menu_count = 0;
    graph.screen_count = 0;
}

} // namespace

bool AsciiScreenGraphBridge::load(
    const product_composition::PresentationBundle& presentation)
{
    resetGraph(graph_);

    if (!product_composition::hasUxMenu(presentation) ||
        !product_composition::hasScreenBindings(presentation))
    {
        return false;
    }

    if (!menu_adapter_.buildLines(*presentation.ux_menu,
                                  graph_.menu_lines,
                                  ui::menu::MenuModel::kMaxItems,
                                  graph_.menu_count))
    {
        resetGraph(graph_);
        return false;
    }

    for (std::size_t index = 0; index < graph_.menu_count; ++index)
    {
        const AsciiMenuLine& line = graph_.menu_lines[index];
        if (!line.route.valid)
        {
            continue;
        }

        AsciiScreenDescriptor screen;
        if (!screen_host_.resolve(line.route, screen))
        {
            continue;
        }

        if (presentation.screen_bindings->find(screen.screen_id) == nullptr)
        {
            continue;
        }

        graph_.screens[graph_.screen_count] = screen;
        ++graph_.screen_count;
    }

    return graph_.menu_count > 0 && graph_.screen_count > 0;
}

std::size_t AsciiScreenGraphBridge::menuCount() const
{
    return graph_.menu_count;
}

std::size_t AsciiScreenGraphBridge::screenCount() const
{
    return graph_.screen_count;
}

const AsciiMenuLine* AsciiScreenGraphBridge::menuItems() const
{
    return graph_.menu_lines;
}

const AsciiScreenDescriptor* AsciiScreenGraphBridge::screenItems() const
{
    return graph_.screens;
}

const AsciiScreenGraph& AsciiScreenGraphBridge::graph() const
{
    return graph_;
}

} // namespace trailmate::linux_sim
