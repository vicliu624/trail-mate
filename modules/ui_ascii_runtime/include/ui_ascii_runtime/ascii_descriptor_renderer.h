#pragma once

#include "ui_ascii_runtime/ascii_runtime_entry_adoption.h"
#include "ui_presentation/menu/menu_model.h"

#include <cstddef>

namespace trailmate::linux_sim
{

struct AsciiRenderLine
{
    const char* text = nullptr;
    bool enabled = false;
};

class AsciiDescriptorRenderer
{
  public:
    bool render(const AsciiRuntimeEntryAdoption& adoption);

    std::size_t lineCount() const noexcept;
    const AsciiRenderLine* lines() const noexcept;
    const char* line(std::size_t index) const noexcept;

  private:
    static constexpr std::size_t kMaxLineLength = 96;

    char line_storage_[ui::menu::MenuModel::kMaxItems][kMaxLineLength]{};
    AsciiRenderLine lines_[ui::menu::MenuModel::kMaxItems]{};
    std::size_t line_count_ = 0;
};

} // namespace trailmate::linux_sim
