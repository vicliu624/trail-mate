#pragma once

#include "ui_presentation/menu/menu_model.h"
#include "ui_presentation/workspace/presentation_workspace.h"

namespace product_composition
{

struct PresentationBundle
{
    ui::workspace::PresentationWorkspace workspace;
    const ui::menu::MenuModel* ux_menu = nullptr;

    // Source/sink/model lifetime is owned by the target composition root.
    // This bundle only exposes the presentation graph to app shells.
};

inline bool hasInteractivePresentation(const PresentationBundle& bundle)
{
    return ui::workspace::hasInteractiveWorkspaceModels(bundle.workspace);
}

inline bool hasUxMenu(const PresentationBundle& bundle)
{
    return bundle.ux_menu != nullptr && bundle.ux_menu->size() > 0;
}

} // namespace product_composition
