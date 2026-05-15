#include "ui_ascii_runtime/ascii_descriptor_renderer.h"

#include <cstdio>

namespace trailmate::linux_sim
{

namespace
{

const char* bindingForScreen(const AsciiRuntimeScreenGraphPresenter& presenter,
                             ui::menu::MenuScreenId screen_id)
{
    for (std::size_t index = 0; index < presenter.screenCount(); ++index)
    {
        const AsciiScreenDescriptor& screen = presenter.screens()[index];
        if (screen.screen_id == screen_id && screen.available &&
            screen.binding_id != nullptr)
        {
            return screen.binding_id;
        }
    }
    return "unbound";
}

} // namespace

bool AsciiDescriptorRenderer::render(
    const AsciiRuntimeEntryAdoption& adoption)
{
    line_count_ = 0;
    if (!adoption.ready())
    {
        return false;
    }

    const AsciiRuntimeScreenGraphPresenter& presenter = adoption.presenter();
    const std::size_t count = presenter.menuCount();
    for (std::size_t index = 0;
         index < count && line_count_ < ui::menu::MenuModel::kMaxItems; ++index)
    {
        const AsciiMenuLine& item = presenter.menuLines()[index];
        const char* label = item.label != nullptr ? item.label : "(untitled)";
        const char* binding = bindingForScreen(presenter, item.screen_id);
        const char* state = item.enabled ? "enabled" : "disabled";
        std::snprintf(line_storage_[line_count_],
                      kMaxLineLength,
                      "%s | %s | %s",
                      label,
                      binding,
                      state);
        lines_[line_count_].text = line_storage_[line_count_];
        lines_[line_count_].enabled = item.enabled;
        ++line_count_;
    }

    return line_count_ > 0;
}

std::size_t AsciiDescriptorRenderer::lineCount() const noexcept
{
    return line_count_;
}

const AsciiRenderLine* AsciiDescriptorRenderer::lines() const noexcept
{
    return lines_;
}

const char* AsciiDescriptorRenderer::line(std::size_t index) const noexcept
{
    return index < line_count_ ? lines_[index].text : nullptr;
}

} // namespace trailmate::linux_sim
