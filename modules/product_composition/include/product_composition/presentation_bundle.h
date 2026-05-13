#pragma once

#include "ui_presentation/workspace/presentation_workspace.h"

namespace product_composition
{

struct PresentationBundle
{
    ui::workspace::PresentationWorkspace workspace;

    // Source/sink/model lifetime is owned by the target composition root.
    // This bundle only exposes the presentation graph to app shells.
};

inline bool hasInteractivePresentation(const PresentationBundle& bundle)
{
    return ui::workspace::hasInteractiveWorkspaceModels(bundle.workspace);
}

} // namespace product_composition
